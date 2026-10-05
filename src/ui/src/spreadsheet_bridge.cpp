#include "spreadsheet_bridge.hpp"

#include "document_path.hpp"
#include "spreadsheet_input.hpp"
#include "spreadsheet_limits.hpp"
#include <QClipboard>
#include <algorithm>
#include <cmath>
#include <set>

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputMethod>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrentRun>

namespace mirrorfly
{
    SpreadsheetBridge::SpreadsheetBridge(QObject* parent) : DocumentView(parent), model_(this)
    {
        connect(this, &SpreadsheetBridge::stateChanged, this, [this]()
        {
            ++revision_;
        });
        connect(this, &SpreadsheetBridge::documentChanged, this, [this]()
        {
            ++revision_;
        });
        connect(this, &SpreadsheetBridge::cellChanged, this, [this]()
        {
            ++revision_;
        });
        connect(&worker_, &QFutureWatcher<SpreadsheetResult>::finished, this,
            &SpreadsheetBridge::completeOperation);
    }

    bool SpreadsheetBridge::active() const
    {
        return active_;
    }

    bool SpreadsheetBridge::modified() const
    {
        return active_ && generation_ != saved_generation_;
    }

    bool SpreadsheetBridge::busy() const
    {
        return operation_ != Operation::None;
    }

    bool SpreadsheetBridge::locked() const
    {
        return busy() || confirmation_open_ || save_dialog_open_;
    }

    bool SpreadsheetBridge::canUndo() const
    {
        return !locked() && !undo_.empty();
    }

    bool SpreadsheetBridge::canRedo() const
    {
        return !locked() && !redo_.empty();
    }

    QString SpreadsheetBridge::error() const
    {
        return error_;
    }

    QString SpreadsheetBridge::documentName() const
    {
        return path_.isEmpty() ? QStringLiteral("未命名.xlsx") : QFileInfo(path_).fileName();
    }

    QString SpreadsheetBridge::documentPath() const
    {
        return path_;
    }

    QUrl SpreadsheetBridge::saveUrl() const
    {
        const QDir directory(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        return QUrl::fromLocalFile(path_.isEmpty() ? directory.filePath(documentName()) : path_);
    }

    QStringList SpreadsheetBridge::sheetNames() const
    {
        QStringList names;
        for (const auto& sheet : document_.sheets)
        {
            names.push_back(QString::fromStdString(sheet.name));
        }
        return names;
    }

    int SpreadsheetBridge::currentSheet() const
    {
        return sheet_;
    }

    QAbstractItemModel* SpreadsheetBridge::model()
    {
        return &model_;
    }

    QVariantMap SpreadsheetBridge::cellInfo() const
    {
        const auto cell = spreadsheet_cell(document_, static_cast<std::size_t>(sheet_), cell_);
        QString kind = QStringLiteral("text");
        if (cell.value.kind == SpreadsheetValueKind::Number)
        {
            kind = QStringLiteral("number");
        }
        else if (cell.value.kind == SpreadsheetValueKind::Boolean)
        {
            kind = QStringLiteral("boolean");
        }
        else if (cell.value.kind == SpreadsheetValueKind::Empty)
        {
            kind = QStringLiteral("auto");
        }
        auto input = QString::fromStdString(cell.formula_cell ? "=" + cell.formula : cell.value.text);
        const bool oversized = input.size() > maximum_cell_input_units;
        if (oversized)
        {
            input = input.left(maximum_cell_preview_units) + QStringLiteral("…");
        }
        const auto reason = oversized
            ? QStringLiteral("此单元格内容较长，显示前 256 字符并保留原值；当前只读。")
            : QString::fromStdString(cell.read_only_reason);
        return {{QStringLiteral("row"), static_cast<int>(cell_.row)},
            {QStringLiteral("column"), static_cast<int>(cell_.column)},
            {QStringLiteral("address"), QString::fromStdString(spreadsheet_address(cell_))},
            {QStringLiteral("inputText"), input}, {QStringLiteral("kind"), kind},
            {QStringLiteral("canEdit"), active_ && cell.editable && !oversized},
            {QStringLiteral("isFormula"), cell.formula_cell}, {QStringLiteral("reason"), reason}};
    }

    QString SpreadsheetBridge::compatibilitySummary() const
    {
        QString summary = QStringLiteral(
            "支持 XLSX 多工作表、区域编辑、字体与底色、边框、基本数字格式、行高列宽、内容排序和模板。"
            "保留未修改的原包部件；日期、自定义数字格式和主题颜色可能按原始值或默认颜色显示。"
            "支持合并、数值条件格式、多列筛选、冻结、表格对象及单元格样式；复杂导入规则保留。"
            "未支持的公式与受保护内容只读。"
            "支持算术、相对/绝对引用及 "
            "SUM、AVERAGE、COUNT、MIN、MAX，汇总随源值重算。行列插入删除同步调整已支持的引用和区域。"
            "排序随内容移动直接格式。暂不提供完整公式引擎或图表编辑。");
        if (document_.read_only)
        {
            summary +=
                QStringLiteral("\n\n此工作簿只读：") + QString::fromStdString(document_.read_only_reason);
        }
        if (document_.caches_stale)
        {
            summary += QStringLiteral(
                "\n\n内容或行可见性已修改；未支持的公式显示为“待重算”，保存后由兼容的表格软件计算。");
        }
        return summary;
    }

    void SpreadsheetBridge::setError(const QString& error)
    {
        error_ = error;
        emit stateChanged();
    }

    void SpreadsheetBridge::clearError()
    {
        setError({});
    }

    bool SpreadsheetBridge::requestOpen(const QUrl& url)
    {
        if (locked() || !url.isLocalFile() || url.toLocalFile().isEmpty())
        {
            return false;
        }
        requestAction(Action::Open, url);
        return true;
    }

    void SpreadsheetBridge::requestNew()
    {
        requestAction(Action::New);
    }

    void SpreadsheetBridge::requestHome()
    {
        requestAction(Action::Home);
    }

    void SpreadsheetBridge::requestHandoff(const QUrl& destination)
    {
        if (destination.isValid() && !destination.isEmpty())
        {
            requestAction(Action::Handoff, destination);
        }
    }

    void SpreadsheetBridge::finishHandoff(bool accepted)
    {
        if (operation_ != Operation::Handoff)
        {
            return;
        }
        operation_ = Operation::None;
        if (accepted)
        {
            resetDocument();
        }
        emit stateChanged();
    }

    bool SpreadsheetBridge::requestWindowClose()
    {
        if (locked())
        {
            return false;
        }
        if (!modified())
        {
            return true;
        }
        requestAction(Action::Quit);
        return false;
    }

    void SpreadsheetBridge::requestAction(Action action, const QUrl& destination)
    {
        if (locked())
        {
            return;
        }
        pending_action_ = action;
        pending_url_ = destination;
        if (modified())
        {
            confirmation_open_ = true;
            emit stateChanged();
            emit confirmUnsavedRequested();
            return;
        }
        performPendingAction();
    }

    void SpreadsheetBridge::resetDocument()
    {
        model_.setDocument(nullptr, 0);
        document_ = {};
        pending_cut_.reset();
        active_ = false;
        path_.clear();
        disk_revision_.clear();
        undo_.clear();
        redo_.clear();
        generation_ = 0;
        saved_generation_ = 0;
        next_generation_ = 1;
        sheet_ = 0;
        cell_ = {};
        anchor_ = {};
        error_.clear();
        emit documentChanged();
        emit cellChanged();
    }

    void SpreadsheetBridge::performPendingAction()
    {
        const auto action = pending_action_;
        const auto destination = pending_url_;
        pending_action_ = Action::None;
        pending_url_ = {};
        switch (action)
        {
        case Action::Open:
        {
            operation_ = Operation::Load;
            clearError();
            const auto path = destination.toLocalFile().toUtf8().toStdString();
            worker_.setFuture(QtConcurrent::run([path]()
            {
                return load_spreadsheet_file(path);
            }));
            break;
        }
        case Action::New:
            resetDocument();
            document_ = make_spreadsheet();
            active_ = true;
            model_.setDocument(&document_, 0);
            emit documentChanged();
            emit cellChanged();
            emit stateChanged();
            emit documentActivated();
            return;
        case Action::Home:
            resetDocument();
            break;
        case Action::Handoff:
            operation_ = Operation::Handoff;
            emit stateChanged();
            emit handoffRequested(destination);
            return;
        case Action::Quit:
            emit windowCloseAllowed();
            return;
        case Action::None:
            return;
        }
        emit stateChanged();
    }

    void SpreadsheetBridge::resolveUnsaved(const QString& decision)
    {
        if (!confirmation_open_)
        {
            return;
        }
        confirmation_open_ = false;
        emit stateChanged();
        if (decision == QStringLiteral("save"))
        {
            save();
        }
        else if (decision == QStringLiteral("discard"))
        {
            performPendingAction();
        }
        else
        {
            pending_action_ = Action::None;
            pending_url_ = {};
        }
    }

    void SpreadsheetBridge::save()
    {
        if (!active_ || locked())
        {
            return;
        }
        if (path_.isEmpty())
        {
            saveAs();
        }
        else
        {
            beginSave(path_);
        }
    }

    bool SpreadsheetBridge::saveTo(const QUrl& destination)
    {
        if (!active_ || locked() || !new_document_destination(destination, QStringList{"xlsx"}))
        {
            setError(QStringLiteral("请等待编辑完成，并选择已有目录下尚不存在的目标文件；不能覆盖原件。"));
            return false;
        }
        beginSave(QFileInfo(destination.toLocalFile()).absoluteFilePath(), true);
        return operation_ == Operation::Save;
    }

    void SpreadsheetBridge::saveAs()
    {
        if (active_ && !locked())
        {
            save_dialog_open_ = true;
            emit stateChanged();
            emit saveDialogRequested();
        }
    }

    void SpreadsheetBridge::selectSaveFile(const QUrl& url)
    {
        if (!save_dialog_open_)
        {
            return;
        }
        save_dialog_open_ = false;
        if (!url.isLocalFile() || url.toLocalFile().isEmpty())
        {
            cancelSaveDialog();
            return;
        }
        beginSave(url.toLocalFile());
    }

    void SpreadsheetBridge::cancelSaveDialog()
    {
        save_dialog_open_ = false;
        pending_action_ = Action::None;
        pending_url_ = {};
        emit stateChanged();
    }

    void SpreadsheetBridge::beginSave(const QString& path, bool new_file)
    {
        operation_ = Operation::Save;
        clearError();
        const auto snapshot = document_;
        const auto utf8_path = path.toUtf8().toStdString();
        auto expected = same_document_path(path_, path) ? disk_revision_ : std::string{};
        if (new_file)
            expected = "missing";
        worker_.setFuture(QtConcurrent::run([snapshot, utf8_path, expected]()
        {
            SpreadsheetResult result;
            const auto package = serialize_spreadsheet(snapshot);
            if (package.error != SpreadsheetError::None)
            {
                result.error = package.error;
                result.message = package.message;
                return result;
            }
            const auto saved = save_spreadsheet_file(utf8_path, package.parts, expected);
            result.error = saved.error;
            result.message = saved.message;
            result.path = saved.path;
            result.revision = saved.revision;
            return result;
        }));
    }

    void SpreadsheetBridge::completeOperation()
    {
        const auto operation = operation_;
        SpreadsheetResult result;
        try
        {
            result = worker_.result();
        }
        catch (...)
        {
            result.error =
                operation == Operation::Load ? SpreadsheetError::ReadFailed : SpreadsheetError::WriteFailed;
        }
        operation_ = Operation::None;
        if (result.error != SpreadsheetError::None)
        {
            pending_action_ = Action::None;
            pending_url_ = {};
            const auto message = result.message.empty() ? QStringLiteral("文件操作未完成，当前内容已保留。")
                                                        : QString::fromStdString(result.message);
            setError(result.error == SpreadsheetError::ChangedOnDisk
                    ? QStringLiteral("文件已被其他程序修改或移走。请另存为，当前编辑内容已保留。")
                    : message);
            if (operation == Operation::Load)
            {
                emit openCompleted(false);
            }
            return;
        }
        if (operation == Operation::Load)
        {
            resetDocument();
            document_ = std::move(result.document);
            active_ = true;
            while (
                static_cast<std::size_t>(sheet_) < document_.sheets.size() && document_.sheets[sheet_].hidden)
                ++sheet_;
            model_.setDocument(&document_, static_cast<std::size_t>(sheet_));
        }
        path_ = QString::fromStdString(result.path);
        disk_revision_ = std::move(result.revision);
        saved_generation_ = generation_;
        emit documentChanged();
        emit cellChanged();
        emit stateChanged();
        emit fileRecorded(path_);
        if (operation == Operation::Load)
        {
            emit openCompleted(true);
            emit documentActivated();
        }
        else
        {
            performPendingAction();
        }
    }

    void SpreadsheetBridge::selectSheet(int index)
    {
        if (!active_ || locked() || index < 0 || index >= static_cast<int>(document_.sheets.size()) ||
            index == sheet_ || document_.sheets[index].hidden)
        {
            return;
        }
        sheet_ = index;
        cell_ = {};
        anchor_ = {};
        model_.setDocument(&document_, static_cast<std::size_t>(sheet_));
        emit documentChanged();
        emit cellChanged();
    }

    void SpreadsheetBridge::selectCell(int row, int column, bool extend)
    {
        if (!active_ || locked() || row < 0 || column < 0 || row >= model_.rowCount() ||
            column >= model_.columnCount())
        {
            return;
        }
        if (extend &&
            (static_cast<std::uint64_t>(std::abs(row - static_cast<int>(anchor_.row))) + 1) *
                    (static_cast<std::uint64_t>(std::abs(column - static_cast<int>(anchor_.column))) + 1) >
                maximum_spreadsheet_batch_cells)
        {
            setError(QStringLiteral("单次选择最多 4096 个单元格。"));
            return;
        }
        cell_ = {static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
        if (!extend)
            anchor_ = cell_;
        emit cellChanged();
    }

    SpreadsheetRange SpreadsheetBridge::selectedRange() const
    {
        return {{std::min(anchor_.row, cell_.row), std::min(anchor_.column, cell_.column)},
            {std::max(anchor_.row, cell_.row), std::max(anchor_.column, cell_.column)}};
    }

    QVariantMap SpreadsheetBridge::rangeInfo() const
    {
        const auto range = selectedRange();
        int nonempty = 0;
        int numbers = 0;
        double sum = 0;
        if (active_)
        {
            for (auto row = range.first.row; row <= range.last.row; ++row)
            {
                for (auto column = range.first.column; column <= range.last.column; ++column)
                {
                    const auto cell =
                        spreadsheet_cell(document_, static_cast<std::size_t>(sheet_), {row, column});
                    nonempty += cell.value.kind != SpreadsheetValueKind::Empty || cell.formula_cell ? 1 : 0;
                    if (cell.value.kind == SpreadsheetValueKind::Number &&
                        (!cell.formula_cell || cell.formula_supported))
                    {
                        bool valid = false;
                        const double number = QString::fromStdString(cell.value.text).toDouble(&valid);
                        if (valid && std::isfinite(number))
                        {
                            sum += number;
                            ++numbers;
                        }
                    }
                }
            }
        }
        const auto first = QString::fromStdString(spreadsheet_address(range.first));
        const auto last = QString::fromStdString(spreadsheet_address(range.last));
        const auto average =
            numbers > 0 && std::isfinite(sum) ? QString::number(sum / numbers, 'g', 12) : QStringLiteral("—");
        return {{"firstRow", range.first.row}, {"firstColumn", range.first.column},
            {"lastRow", range.last.row}, {"lastColumn", range.last.column},
            {"address", first == last ? first : first + ':' + last},
            {"count", (range.last.row - range.first.row + 1) * (range.last.column - range.first.column + 1)},
            {"nonempty", nonempty}, {"numbers", numbers},
            {"sum", std::isfinite(sum) ? QString::number(sum, 'g', 12) : QStringLiteral("超出范围")},
            {"average", average}};
    }

    bool SpreadsheetBridge::selectAddress(const QString& text)
    {
        if (!active_ || locked())
            return false;
        const auto parts = text.trimmed().toUpper().split(':');
        const auto first = parse_spreadsheet_address(parts.front().toStdString());
        const auto last = parse_spreadsheet_address(parts.back().toStdString());
        if (parts.size() > 2 || !first || !last ||
            std::max(first->row, last->row) >= static_cast<std::uint32_t>(model_.rowCount()) ||
            std::max(first->column, last->column) >= static_cast<std::uint32_t>(model_.columnCount()) ||
            (static_cast<std::uint64_t>(std::max(first->row, last->row) - std::min(first->row, last->row)) +
                1) * (std::max(first->column, last->column) - std::min(first->column, last->column) + 1) >
                maximum_spreadsheet_batch_cells)
        {
            setError(QStringLiteral("请输入网格内的地址或区域，例如 C20、A1:C10；最多 4096 格。"));
            return false;
        }
        anchor_ = *first;
        cell_ = *last;
        clearError();
        emit cellChanged();
        return true;
    }

    bool SpreadsheetBridge::findCell(const QString& query, bool backwards)
    {
        if (!active_ || locked() || query.isEmpty() || query.size() > maximum_cell_input_units)
        {
            return false;
        }
        const auto sheet = static_cast<std::size_t>(sheet_);
        std::set<SpreadsheetAddress> addresses;
        for (const auto& entry : document_.sheets[sheet].cells)
        {
            addresses.insert(entry.first);
        }
        const auto edits = document_.edits.find(sheet);
        if (edits != document_.edits.end())
        {
            for (const auto& entry : edits->second)
            {
                addresses.insert(entry.first);
            }
        }
        std::vector<SpreadsheetAddress> matches;
        for (const auto address : addresses)
        {
            const auto cell = spreadsheet_cell(document_, sheet, address);
            const auto text =
                QString::fromStdString(cell.formula_cell ? "=" + cell.formula : cell.value.text);
            if (text.contains(query, Qt::CaseInsensitive))
            {
                matches.push_back(address);
            }
        }
        if (matches.empty())
        {
            setError(QStringLiteral("当前工作表没有找到匹配内容。"));
            return false;
        }
        auto selected = matches.front();
        if (backwards)
        {
            selected = matches.back();
            for (auto iterator = matches.rbegin(); iterator != matches.rend(); ++iterator)
            {
                if (*iterator < cell_)
                {
                    selected = *iterator;
                    break;
                }
            }
        }
        else
        {
            for (const auto address : matches)
            {
                if (cell_ < address)
                {
                    selected = address;
                    break;
                }
            }
        }
        selectCell(static_cast<int>(selected.row), static_cast<int>(selected.column));
        clearError();
        return true;
    }

    QString SpreadsheetBridge::selectionText()
    {
        if (!active_ || locked())
            return {};
        const auto range = selectedRange();
        QStringList rows;
        qsizetype units = 0;
        for (auto row = range.first.row; row <= range.last.row; ++row)
        {
            QStringList cells;
            for (auto column = range.first.column; column <= range.last.column; ++column)
            {
                const auto cell =
                    spreadsheet_cell(document_, static_cast<std::size_t>(sheet_), {row, column});
                const auto raw =
                    QString::fromStdString(cell.formula_cell ? "=" + cell.formula : cell.value.text);
                const auto quoted = spreadsheet_quote_cell(raw);
                units += quoted.toUtf8().size() + 1;
                if (units > 1024 * 1024)
                {
                    setError(QStringLiteral("复制内容超过 1 MiB，请缩小区域。"));
                    return {};
                }
                cells.append(quoted);
            }
            rows.append(cells.join(QChar(9)));
        }
        // A final record terminator distinguishes an empty last row from the previous row's terminator.
        return rows.join(QChar(10)) + QChar(10);
    }

    void SpreadsheetBridge::copyCell()
    {
        copyCells(false);
    }

    bool SpreadsheetBridge::pasteCell()
    {
        return pasteCells("all");
    }

    bool SpreadsheetBridge::pasteText(const QString& text)
    {
        if (!active_ || locked())
            return false;
        QVector<QStringList> rows;
        QString error;
        if (!spreadsheet_paste_rows(text, rows, error))
        {
            setError(error);
            return false;
        }
        if (rows.isEmpty())
            return false;
        const auto range = selectedRange();
        const bool fill = rows.size() == 1 && rows.front().size() == 1;
        const auto height =
            fill ? range.last.row - range.first.row + 1 : static_cast<std::uint32_t>(rows.size());
        const auto width = fill ? range.last.column - range.first.column + 1
                                : static_cast<std::uint32_t>(rows.front().size());
        if (range.first.row + height > static_cast<std::uint32_t>(model_.rowCount()) ||
            range.first.column + width > static_cast<std::uint32_t>(model_.columnCount()))
        {
            setError(QStringLiteral("粘贴区域超出当前网格。"));
            return false;
        }
        std::vector<SpreadsheetEditCommand> commands;
        for (std::uint32_t row = 0; row < height; ++row)
        {
            for (std::uint32_t column = 0; column < width; ++column)
            {
                SpreadsheetValue value;
                const auto& input = fill ? rows.front().front() : rows[row][column];
                if (!spreadsheet_input_value(input, QStringLiteral("auto"), value, error))
                {
                    setError(error);
                    return false;
                }
                commands.push_back({static_cast<std::size_t>(sheet_),
                    {range.first.row + row, range.first.column + column}, value});
            }
        }
        if (!commitCells(commands))
            return false;
        anchor_ = range.first;
        cell_ = {range.first.row + height - 1, range.first.column + width - 1};
        emit cellChanged();
        return true;
    }

    bool SpreadsheetBridge::clearSelection()
    {
        if (!active_ || locked())
            return false;
        const auto range = selectedRange();
        std::vector<SpreadsheetEditCommand> commands;
        for (auto row = range.first.row; row <= range.last.row; ++row)
        {
            for (auto column = range.first.column; column <= range.last.column; ++column)
                commands.push_back({static_cast<std::size_t>(sheet_), {row, column}, {}});
        }
        return commitCells(commands);
    }

    bool SpreadsheetBridge::setCellValue(int row, int column, const QString& text, const QString& kind)
    {
        if (!active_ || locked() || row < 0 || column < 0 || row >= model_.rowCount() ||
            column >= model_.columnCount())
            return false;
        SpreadsheetValue value;
        QString error;
        if (!spreadsheet_input_value(text, kind, value, error))
        {
            setError(error);
            return false;
        }
        return commitCells({{static_cast<std::size_t>(sheet_),
            {static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)}, value}});
    }

    bool SpreadsheetBridge::commitCells(const std::vector<SpreadsheetEditCommand>& commands)
    {
        return commitChanges(commands, {}, {});
    }

    std::size_t SpreadsheetBridge::historyBytes(const HistoryEntry& entry)
    {
        std::size_t bytes = sizeof(HistoryEntry) + entry.document_bytes;
        if (entry.structure_before)
            bytes += entry.structure_before->name.size();
        if (entry.structure_after)
            bytes += entry.structure_after->name.size();
        for (const auto* value : {&entry.features_before, &entry.features_after})
            if (*value)
            {
                bytes += (*value)->merges.size() * sizeof(SpreadsheetRange) +
                    (*value)->conditions.size() * 256 +
                    ((*value)->hidden_rows.size() + (*value)->hidden_columns.size()) * 64;
                for (const auto& filter : (*value)->filters)
                {
                    bytes += sizeof(SpreadsheetFilter) + filter.comparison.size();
                    for (const auto& text : filter.values)
                        bytes += sizeof(std::string) + text.size();
                }
                for (const auto& table : (*value)->tables)
                {
                    bytes += sizeof(SpreadsheetTable) + table.name.size() + table.path.size();
                    for (const auto& [name, format] : table.style)
                    {
                        bytes += name.size() + 64;
                        for (const auto& [key, text] : format)
                            bytes += key.size() + text.size() + 64;
                    }
                }
            }
        for (const auto& cell : entry.changes)
            bytes += cell.before.text.size() + cell.after.text.size() + sizeof(CellChange);
        for (const auto* formats : {&entry.formats_before, &entry.formats_after})
            for (const auto& command : *formats)
            {
                bytes += sizeof(SpreadsheetFormatCommand);
                for (const auto& [key, value] : command.format)
                    bytes += key.size() + value.size() + 64;
            }
        return bytes +
            (entry.dimensions_before.size() + entry.dimensions_after.size()) *
            sizeof(SpreadsheetDimensionCommand);
    }

    bool SpreadsheetBridge::commitChanges(const std::vector<SpreadsheetEditCommand>& commands,
        const std::vector<SpreadsheetFormatCommand>& formats,
        const std::vector<SpreadsheetDimensionCommand>& dimensions)
    {
        if (!active_ || locked() || (commands.empty() && formats.empty() && dimensions.empty()) ||
            commands.size() > maximum_spreadsheet_batch_cells ||
            formats.size() > maximum_spreadsheet_batch_cells ||
            dimensions.size() > maximum_spreadsheet_batch_cells)
            return false;
        HistoryEntry entry;
        entry.sheet = static_cast<std::size_t>(sheet_);
        entry.address = selectedRange().first;
        entry.last = selectedRange().last;
        if (!commands.empty())
        {
            entry.address = commands.front().address;
            entry.last = commands.back().address;
        }
        entry.before_generation = generation_;
        entry.after_generation = next_generation_;
        for (const auto& command : commands)
        {
            const auto before = spreadsheet_source_value(document_, command.sheet_index, command.address);
            if (before.kind == command.value.kind && before.text == command.value.text)
                continue;
            if (QString::fromStdString(before.text).size() > maximum_cell_input_units)
            {
                setError(QStringLiteral("区域包含超长只读内容，请缩小选择。"));
                return false;
            }
            entry.changes.push_back({command.address, before, command.value});
        }
        for (const auto& command : formats)
        {
            SpreadsheetFormat before;
            const auto sheet = document_.format_edits.find(command.sheet_index);
            if (sheet != document_.format_edits.end())
            {
                const auto found = sheet->second.find(command.address);
                if (found != sheet->second.end())
                    before = found->second;
            }
            if (before == command.format)
                continue;
            entry.formats_before.push_back({command.sheet_index, command.address, std::move(before)});
            entry.formats_after.push_back(command);
        }
        for (const auto& command : dimensions)
        {
            auto before = command;
            before.size = 0;
            const auto& edits = command.column ? document_.column_edits : document_.row_edits;
            const auto sheet = edits.find(command.sheet_index);
            if (sheet != edits.end())
            {
                const auto found = sheet->second.find(command.index);
                if (found != sheet->second.end())
                    before.size = found->second;
            }
            if (before.size == command.size)
                continue;
            entry.dimensions_before.push_back(before);
            entry.dimensions_after.push_back(command);
        }
        if (historyBytes(entry) > maximum_spreadsheet_history_bytes)
        {
            setError(QStringLiteral("本次修改的撤销内容超过 32 MiB，请缩小区域。"));
            return false;
        }
        // Allocate the history node before committing the document.
        try
        {
            undo_.push_back(std::move(entry));
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("撤销记录内存不足，未进行修改。"));
            return false;
        }
        const auto result = apply_spreadsheet_transaction(document_, commands, formats, dimensions);
        if (result.error != SpreadsheetError::None)
        {
            undo_.pop_back();
            setError(QString::fromStdString(result.message));
            return false;
        }
        clearError();
        if (!result.changed)
        {
            undo_.pop_back();
            return true;
        }
        generation_ = next_generation_++;
        redo_.clear();
        trimHistory();
        ++layout_revision_;
        // Formula invalidation can affect cells outside the edited range.
        if (!commands.empty())
            model_.refresh();
        else
            model_.refresh(selectedRange());
        emit documentChanged();
        emit cellChanged();
        emit stateChanged();
        return true;
    }

    void SpreadsheetBridge::commitTextInput()
    {
        if (auto* input = QGuiApplication::inputMethod())
        {
            input->commit();
        }
    }

    void SpreadsheetBridge::trimHistory()
    {
        std::size_t bytes = 0;
        for (const auto& entry : undo_)
            bytes += historyBytes(entry);
        while (!undo_.empty() && (undo_.size() > 128 || bytes > maximum_spreadsheet_history_bytes))
        {
            bytes -= historyBytes(undo_.front());
            undo_.pop_front();
        }
    }

    void SpreadsheetBridge::restoreHistory(bool redo)
    {
        auto& source = redo ? redo_ : undo_;
        auto& target = redo ? undo_ : redo_;
        if (locked() || source.empty())
        {
            return;
        }
        const auto entry = source.back();
        std::vector<SpreadsheetEditCommand> commands;
        for (const auto& cell : entry.changes)
            commands.push_back({entry.sheet, cell.address, redo ? cell.after : cell.before});
        try
        {
            target.push_back(entry);
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("历史恢复内存不足，原内容已保留。"));
            return;
        }
        const auto structure = redo ? entry.structure_after : entry.structure_before;
        SpreadsheetEditResult result;
        if (entry.document_before && entry.document_after)
        {
            try
            {
                auto restored = redo ? *entry.document_after : *entry.document_before;
                document_ = std::move(restored);
                result.changed = true;
            }
            catch (const std::bad_alloc&)
            {
                target.pop_back();
                setError(QStringLiteral("历史恢复内存不足，原工作簿已保留。"));
                return;
            }
        }
        else if (entry.features_before && entry.features_after)
            result = apply_spreadsheet_features(
                document_, entry.sheet, redo ? *entry.features_after : *entry.features_before);
        else if (structure)
            result = apply_spreadsheet_sheet_command(document_, *structure);
        else
            result = apply_spreadsheet_transaction(document_, commands,
                redo ? entry.formats_after : entry.formats_before,
                redo ? entry.dimensions_after : entry.dimensions_before);
        if (result.error != SpreadsheetError::None)
        {
            target.pop_back();
            setError(QString::fromStdString(result.message));
            return;
        }
        source.pop_back();
        generation_ = redo ? entry.after_generation : entry.before_generation;
        ++layout_revision_;
        const auto restored_sheet = entry.document_before && !redo ? entry.previous_sheet : entry.sheet;
        sheet_ = static_cast<int>(std::min(restored_sheet, document_.sheets.size() - 1));
        cell_ = entry.last;
        anchor_ = entry.address;
        const auto& restored_sheet_data = document_.sheets[sheet_];
        cell_.row = std::min(cell_.row, restored_sheet_data.rows - 1);
        cell_.column = std::min(cell_.column, restored_sheet_data.columns - 1);
        anchor_.row = std::min(anchor_.row, restored_sheet_data.rows - 1);
        anchor_.column = std::min(anchor_.column, restored_sheet_data.columns - 1);
        model_.setDocument(&document_, static_cast<std::size_t>(sheet_));
        clearError();
        emit documentChanged();
        emit cellChanged();
    }

    void SpreadsheetBridge::undo()
    {
        restoreHistory(false);
    }

    void SpreadsheetBridge::redo()
    {
        restoreHistory(true);
    }
}
