#include "word_bridge.hpp"

#include "document_path.hpp"
#include "word_document.hpp"
#include "word_table_gaps.hpp"
#include "word_units.hpp"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQuickItem>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QThread>
#include <QtConcurrentRun>

#include <algorithm>

namespace
{
    bool word_structural_text(const QString& text)
    {
        return text.contains('\n') || text.contains('\r') || text.contains(QChar::ParagraphSeparator) ||
            text.contains(QChar::ObjectReplacementCharacter);
    }
}

namespace mirrorfly
{
    WordBridge::PreparedEditor::~PreparedEditor()
    {
        if (document && document->thread() != QThread::currentThread())
            document.release()->deleteLater();
    }

    PdfExportSource WordBridge::pdfSource() const
    {
        PdfExportSource result;
        if (!active_ || locked() || !editor_)
            result.error = "Word 编辑器尚未准备好导出。";
        else
        {
            const auto snapshot = extract_word_document(*editor_);
            if (!snapshot.success)
                result.error = snapshot.error;
            else
                result.content = snapshot.document;
        }
        result.title = documentName().toStdString();
        result.source_path = path_.toStdString();
        return result;
    }

    WordBridge::WordBridge(QObject* parent) : DocumentView(parent)
    {
        connect(&worker_, &QFutureWatcher<OperationResult>::finished, this, &WordBridge::completeOperation);
    }

    WordBridge::~WordBridge()
    {
        // Worker-created Qt objects must finish their handoff before the GUI application can shut down.
        if (worker_.isRunning())
            worker_.waitForFinished();
    }

    bool WordBridge::active() const
    {
        return active_;
    }

    bool WordBridge::modified() const
    {
        return modified_;
    }

    bool WordBridge::locked() const
    {
        return operation_ != Operation::None || confirmation_open_ || save_dialog_open_;
    }

    bool WordBridge::readOnly() const
    {
        return read_only_;
    }

    qreal WordBridge::loadingProgress() const
    {
        return loading_progress_;
    }

    QString WordBridge::loadingStage() const
    {
        return loading_stage_;
    }

    void WordBridge::setLoadingProgress(const QString& stage, qreal progress)
    {
        const auto value = std::max(loading_progress_, std::clamp(progress, 0.0, 1.0));
        if (stage == loading_stage_ && value == loading_progress_)
            return;
        loading_stage_ = stage;
        loading_progress_ = value;
        emit loadingChanged();
    }

    int WordBridge::revision() const
    {
        return revision_;
    }

    QString WordBridge::documentName() const
    {
        return path_.isEmpty() ? QStringLiteral("未命名.docx") : QFileInfo(path_).fileName();
    }

    QString WordBridge::message() const
    {
        return message_;
    }

    QString WordBridge::documentPath() const
    {
        return path_;
    }

    void WordBridge::setMessage(const QString& message)
    {
        message_ = message;
        emit messageChanged();
    }

    void WordBridge::clearMessage()
    {
        setMessage({});
    }

    QUrl WordBridge::saveUrl() const
    {
        if (!path_.isEmpty() && !read_only_)
        {
            return QUrl::fromLocalFile(path_);
        }
        const auto directory = path_.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            : QFileInfo(path_).absolutePath();
        const auto name = path_.isEmpty()
            ? QStringLiteral("未命名.docx")
            : QFileInfo(path_).completeBaseName() + QStringLiteral("-编辑副本.docx");
        return QUrl::fromLocalFile(QDir(directory).filePath(name));
    }

    QStringList WordBridge::chineseFonts() const
    {
        return QFontDatabase::families(QFontDatabase::SimplifiedChinese);
    }

    QVariantMap WordBridge::statistics() const
    {
        return {{"characters", editor_ ? editor_->characterCount() - 1 : 0},
            {"paragraphs", editor_ ? editor_->blockCount() : 0},
            {"canUndo", editor_ && editor_->isUndoAvailable()},
            {"canRedo", editor_ && editor_->isRedoAvailable()}};
    }

    QVariantMap WordBridge::snapshot() const
    {
        const QVariantMap current_statistics = statistics();
        QByteArray encoded = editor_ ? editor_->toPlainText().toUtf8() : QByteArray{};
        const bool truncated = encoded.size() > static_cast<qsizetype>(maximum_word_text_bytes);
        if (truncated)
        {
            qsizetype length = static_cast<qsizetype>(maximum_word_text_bytes);
            while (length > 0 && (static_cast<unsigned char>(encoded[length]) & 0xc0U) == 0x80U)
            {
                --length;
            }
            encoded.truncate(length);
        }
        return {{"plainText", QString::fromUtf8(encoded)}, {"truncated", truncated}, {"active", active_},
            {"locked", locked()}, {"readOnly", read_only_}, {"modified", modified_},
            {"formatReady", formatReady()}, {"paragraphs", current_statistics.value("paragraphs")},
            {"statistics", current_statistics}, {"revision", revision_}};
    }

    void WordBridge::loadEditor(QQuickTextDocument* wrapper)
    {
        if (!wrapper)
        {
            return;
        }
        auto next = prepared_editor_ ? std::move(prepared_editor_->document) : nullptr;
        prepared_editor_.reset();
        if (!next)
        {
            const auto* target = wrapper->textDocument();
            next = create_word_document(document_, target ? target->textWidth() : -1, true,
                target ? target->defaultTextOption() : QTextOption{});
        }
        if (editor_)
        {
            disconnect(editor_->documentLayout(), nullptr, this, nullptr);
            disconnect(editor_, nullptr, this, nullptr);
        }
        QPointer<QTextDocument> previous = wrapper->textDocument();
        next->setParent(wrapper);
        editor_ = next.release();
        accepted_block_count_ = editor_->blockCount();
        // Qt 6.8 viewport node reuse can omit earlier text/frames when scrolling backwards.
        // The hidden input item does not virtualize nodes; WordViewport paints only the visible region.
        if (auto* item = qobject_cast<QQuickItem*>(wrapper->parent()))
        {
            item->setFlag(QQuickItem::ItemObservesViewport, false);
            item->setFlag(QQuickItem::ItemHasContents, false);
        }
        wrapper->setTextDocument(editor_);
        if (previous && previous->parent() == wrapper)
        {
            delete previous.data();
        }
        connect(editor_, &QTextDocument::modificationChanged, this, [this](bool modified)
        {
            modified_ = active_ && !read_only_ && modified;
            emit stateChanged();
        });
        connect(editor_, &QTextDocument::contentsChange, this, [this](int position, int removed, int added)
        {
            if (!guarding_editor_)
            {
                last_change_position_ = position;
                last_change_removed_ = removed;
                last_change_added_ = added;
            }
        });
        connect(editor_, &QTextDocument::contentsChanged, this, [this]()
        {
            if (word_image_refresh_in_progress(*editor_))
                return;
            guardEditorBudget();
            emit editorChanged();
        });
        connect(editor_, &QTextDocument::undoAvailable, this, &WordBridge::editorChanged);
        connect(editor_, &QTextDocument::redoAvailable, this, &WordBridge::editorChanged);
        connect(editor_->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            &WordBridge::editorChanged);
        emit editorChanged();
        if (awaiting_editor_)
        {
            awaiting_editor_ = false;
            setLoadingProgress(QStringLiteral("文档已准备完成"), 1);
            emit loadReady();
        }
    }

    QVariantMap WordBridge::inspect(int position) const
    {
        return editor_ ? inspect_word_document(*editor_, position) : QVariantMap{};
    }

    bool WordBridge::format(int start, int end, const QString& action, const QVariant& value)
    {
        if (!active_ || locked() || read_only_ || !editor_)
            return false;
        const bool result = format_word_document(*editor_, start, end, action, value);
        if (!result && action == "sort")
            setMessage(QStringLiteral("排序未执行。数字排序要求每个段落只包含一个有效数字。"));
        else if (!result && (action == "leftIndent" || action == "firstLineIndent"))
            setMessage(QStringLiteral("缩进未应用：悬挂缩进的绝对值不能超过左缩进，请先增大左缩进。"));
        else if (!result && action == "cellBorder")
            setMessage(
                QStringLiteral("边框未应用：请选择单个单元格，检查方向、线型、颜色和八分之一磅刻度。"));
        else if (!result && action.startsWith("cell"))
            setMessage(QStringLiteral("单元格样式未应用：请选择单个单元格内的文字，内边距范围为 0–144 pt。"));
        else if (!result)
            setMessage(QStringLiteral("格式未应用，请检查选区和参数范围。"));
        else
            setMessage({});
        return result;
    }

    void WordBridge::undo()
    {
        if (active_ && !locked() && !read_only_ && editor_)
        {
            editor_->undo();
        }
    }

    void WordBridge::redo()
    {
        if (active_ && !locked() && !read_only_ && editor_)
        {
            editor_->redo();
        }
    }

    QVariantMap WordBridge::find(const QString& query, int from, bool backward)
    {
        if (!editor_ || query.isEmpty() || query.size() > 1024)
        {
            return {};
        }
        const auto flags = backward ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
        auto cursor = editor_->find(query, from, flags);
        if (cursor.isNull())
        {
            cursor = editor_->find(query, backward ? editor_->characterCount() - 1 : 0, flags);
        }
        if (cursor.isNull())
        {
            setMessage(QStringLiteral("没有找到匹配内容。"));
            return {};
        }

        setMessage({});
        return {{"start", cursor.selectionStart()}, {"end", cursor.selectionEnd()}};
    }

    bool WordBridge::insertText(int start, int end, const QString& text)
    {
        if (!active_ || locked() || read_only_ || !editor_ || start < 0 || end < start ||
            end >= editor_->characterCount() || !text.isValidUtf16() ||
            text.toUtf8().size() > static_cast<qsizetype>(maximum_word_text_bytes))
        {
            return false;
        }
        QTextCursor cursor(editor_);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        const auto selected = cursor.selectedText();
        if (word_table_gap(cursor))
        {
            setMessage(QStringLiteral("此处是原表格的布局空缺，不是可编辑单元格。"));
            return false;
        }
        const bool structural = word_structural_text(text) || word_structural_text(selected);
        if (!structural)
        {
            const auto selected_bytes = selected.toUtf8().size();
            const auto inserted_bytes = text.toUtf8().size();
            const auto block_bytes = cursor.block().text().toUtf8().size() - selected_bytes + inserted_bytes;
            if (block_bytes > static_cast<qsizetype>(maximum_word_paragraph_bytes))
            {
                return false;
            }
            const auto characters = editor_->characterCount() - selected.size() + text.size();
            if (static_cast<std::size_t>(characters) > maximum_word_text_bytes / 3)
            {
                const auto bytes = editor_->toPlainText().toUtf8().size() - selected_bytes + inserted_bytes;
                if (bytes > static_cast<qsizetype>(maximum_word_text_bytes))
                {
                    return false;
                }
            }
        }
        else
        {
            std::unique_ptr<QTextDocument> candidate(editor_->clone());
            QTextCursor candidate_cursor(candidate.get());
            candidate_cursor.setPosition(start);
            candidate_cursor.setPosition(end, QTextCursor::KeepAnchor);
            candidate_cursor.insertText(text);
            const auto candidate_result = extract_word_document(*candidate, editor_);
            if (!candidate_result.success)
            {
                setMessage(QString::fromStdString(candidate_result.error));
                return false;
            }
        }
        cursor.beginEditBlock();
        cursor.insertText(text);
        cursor.endEditBlock();
        return true;
    }

    void WordBridge::guardEditorBudget()
    {
        const bool image_change = editor_ && take_word_image_structure_change(*editor_);
        if (guarding_editor_ || !editor_ || !active_ || read_only_)
        {
            return;
        }
        QString error;
        const bool structural_change = editor_->blockCount() != accepted_block_count_ || image_change;
        if (editor_->blockCount() > static_cast<int>(maximum_word_paragraphs))
        {
            error = QStringLiteral("正文最多 32768 段，刚才的输入已撤销。");
        }
        else if (static_cast<std::size_t>(editor_->characterCount()) > maximum_word_text_bytes / 3 &&
            editor_->toPlainText().toUtf8().size() > static_cast<qsizetype>(maximum_word_text_bytes))
        {
            error = QStringLiteral("正文最多 2 MiB，刚才的输入已撤销。");
        }
        else
        {
            const int last = last_change_position_ + last_change_added_;
            for (auto block = editor_->findBlock(last_change_position_);
                block.isValid() && block.position() <= last; block = block.next())
            {
                if (word_table_gap(QTextCursor(block)) && !block.text().isEmpty())
                {
                    error = QStringLiteral("此处是原表格的布局空缺，刚才的输入已撤销。");
                    break;
                }
                if (block.text().toUtf8().size() > static_cast<qsizetype>(maximum_word_paragraph_bytes))
                {
                    error = QStringLiteral("每段最多 8 KiB，刚才的输入已撤销。");
                    break;
                }
            }
        }
        if (error.isEmpty() && structural_change)
        {
            const auto checked = extract_word_document(*editor_);
            if (!checked.success)
                error = QString::fromStdString(checked.error) + QStringLiteral(" 刚才的结构修改已撤销。");
        }
        if (!error.isEmpty() && editor_->isUndoAvailable())
        {
            guarding_editor_ = true;
            if (!structural_change && last_change_removed_ == 0 && last_change_added_ > 0 &&
                last_change_position_ + last_change_added_ < editor_->characterCount())
            {
                // Remove only the rejected insertion; Qt may have merged earlier valid typing into undo.
                QTextCursor rollback(editor_);
                rollback.setPosition(last_change_position_);
                rollback.setPosition(last_change_position_ + last_change_added_, QTextCursor::KeepAnchor);
                rollback.beginEditBlock();
                rollback.removeSelectedText();
                rollback.endEditBlock();
            }
            else
                editor_->undo();
            guarding_editor_ = false;
            setMessage(error);
        }
        accepted_block_count_ = editor_->blockCount();
    }

    bool WordBridge::replace(int start, int end, const QString& expected, const QString& replacement)
    {
        if (!editor_ || expected.isEmpty() || start < 0 || end <= start || end >= editor_->characterCount())
        {
            return false;
        }
        QTextCursor cursor(editor_);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        if (cursor.selectedText().compare(expected, Qt::CaseInsensitive) != 0)
        {
            return false;
        }
        return insertText(start, end, replacement);
    }

    bool WordBridge::pastePlain(int start, int end)
    {
        return insertText(start, end, QGuiApplication::clipboard()->text());
    }

    int WordBridge::insertParagraph(int start, int end)
    {
        if (!active_ || locked() || read_only_ || !editor_ || start < 0 || end < start ||
            end >= editor_->characterCount())
        {
            return -1;
        }
        QTextCursor cursor(editor_);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        auto block = cursor.blockFormat();
        auto character = cursor.charFormat();
        block.setHeadingLevel(0);
        block.setBottomMargin(word_points_to_pixels(8));
        character.setFontPointSize(12);
        character.setFontWeight(QFont::Normal);
        character.setFontItalic(false);
        character.setFontUnderline(false);
        cursor.beginEditBlock();
        cursor.insertBlock(block, character);
        cursor.endEditBlock();
        return cursor.position();
    }

    bool WordBridge::insertTemplate(int position, const QString& kind)
    {
        if (!active_ || locked() || read_only_ || !editor_ || position < 0 ||
            position >= editor_->characterCount())
        {
            return false;
        }
        WordTemplateKind template_kind;
        if (kind == "source")
        {
            template_kind = WordTemplateKind::SourceRecord;
        }
        else if (kind == "meeting")
        {
            template_kind = WordTemplateKind::MeetingMinutes;
        }
        else if (kind == "weekly")
        {
            template_kind = WordTemplateKind::WeeklyReport;
        }
        else
        {
            return false;
        }
        auto current = extract_word_document(*editor_);
        if (!current.success)
        {
            setMessage(QString::fromStdString(current.error));
            return false;
        }
        const auto block = editor_->findBlock(position);
        const bool replace_empty = editor_->blockCount() == 1 && block.text().isEmpty();
        const std::size_t paragraph_index =
            replace_empty ? 0 : static_cast<std::size_t>(block.blockNumber() + 1);
        const auto result = insert_word_template(current.document, paragraph_index, template_kind);
        if (!result.success)
        {
            setMessage(QString::fromStdString(result.error));
            return false;
        }
        const auto first = current.document.paragraphs.begin() + static_cast<std::ptrdiff_t>(paragraph_index);
        const std::vector<WordParagraph> paragraphs(first, first + result.inserted_paragraphs);
        if (!insert_word_paragraphs(*editor_, paragraph_index, paragraphs))
        {
            setMessage(QStringLiteral("无法在当前位置插入模板，当前文档未改变。"));
            return false;
        }
        setMessage({});
        return true;
    }

    bool WordBridge::requestOpen(const QUrl& url)
    {
        if (!url.isLocalFile() || !is_word_path(url.toLocalFile().toStdString()) || locked())
        {
            return false;
        }
        requestAction(Action::Open, url.toLocalFile());
        return true;
    }

    void WordBridge::requestNew()
    {
        requestAction(Action::New);
    }

    void WordBridge::requestHome()
    {
        requestAction(Action::Home);
    }

    void WordBridge::requestHandoff(const QUrl& destination)
    {
        requestAction(Action::Handoff, destination.toString(QUrl::FullyEncoded));
    }

    void WordBridge::finishHandoff(bool success)
    {
        if (operation_ != Operation::Handoff)
        {
            return;
        }
        operation_ = Operation::None;
        if (success)
        {
            pending_action_ = Action::Home;
            performPendingAction();
        }

        else
        {
            emit stateChanged();
        }
    }

    bool WordBridge::requestWindowClose()
    {
        if (locked())
        {
            return false;
        }
        if (!modified_)
        {
            return true;
        }
        requestAction(Action::Quit);
        return false;
    }

    void WordBridge::requestAction(Action action, const QString& destination)
    {
        if (locked())
        {
            return;
        }
        pending_action_ = action;
        pending_path_ = destination;
        if (modified_)
        {
            confirmation_open_ = true;
            emit stateChanged();
            emit confirmUnsavedRequested();
        }

        else
        {
            performPendingAction();
        }
    }

    void WordBridge::cancelPending()
    {
        pending_action_ = Action::None;
        pending_path_.clear();
    }

    void WordBridge::performPendingAction()
    {
        const auto action = pending_action_;
        const auto path = pending_path_;
        cancelPending();
        if (action == Action::Open)
        {
            beginLoad(path);
        }
        else if (action == Action::New || action == Action::Home)
        {
            prepared_editor_.reset();
            awaiting_editor_ = false;
            ++load_generation_;
            active_ = action == Action::New;
            modified_ = false;
            read_only_ = false;
            document_ = {};
            path_.clear();
            source_path_.clear();
            disk_revision_.clear();
            ++revision_;
            setMessage({});
            emit documentChanged();
            emit stateChanged();
            if (active_)
            {
                emit documentActivated();
            }
        }

        else if (action == Action::Handoff)
        {
            operation_ = Operation::Handoff;
            emit stateChanged();
            emit handoffRequested(QUrl(path));
        }

        else if (action == Action::Quit)
        {
            emit windowCloseAllowed();
        }
    }

    void WordBridge::resolveUnsaved(const QString& decision)
    {
        if (!confirmation_open_)
        {
            return;
        }
        confirmation_open_ = false;
        emit stateChanged();
        if (decision == "save")
        {
            save();
        }
        else if (decision == "discard")
        {
            performPendingAction();
        }
        else
        {
            cancelPending();
        }
    }

    void WordBridge::beginLoad(const QString& path)
    {
        operation_ = Operation::Load;
        awaiting_editor_ = false;
        loading_progress_ = 0;
        loading_stage_ = QStringLiteral("正在读取 Word 文档");
        const auto generation = ++load_generation_;
        setMessage({});
        emit stateChanged();
        emit loadingChanged();
        emit loadStarted();
        const auto file = path.toStdString();
        auto* destination_thread = thread();
        const auto width = editor_ ? editor_->textWidth() : 720;
        const auto text_option = editor_ ? editor_->defaultTextOption() : QTextOption{};
        const QPointer<WordBridge> guard(this);
        worker_.setFuture(
            QtConcurrent::run([file, destination_thread, width, text_option, guard, generation]()
        {
            const auto post = [guard, generation](const QString& stage, qreal value)
            {
                if (!guard)
                    return;
                QMetaObject::invokeMethod(guard, [guard, generation, stage, value]()
                {
                    if (guard && guard->load_generation_ == generation &&
                        guard->operation_ == Operation::Load)
                        guard->setLoadingProgress(stage, value);
                }, Qt::QueuedConnection);
            };
            const auto fraction = [](std::size_t completed, std::size_t total)
            {
                return total ? std::clamp(qreal(completed) / total, 0.0, 1.0) : 1.0;
            };
            OperationResult result;
            result.file =
                load_word_file(file, [&](WordLoadStage stage, std::size_t completed, std::size_t total)
            {
                const auto value = fraction(completed, total);
                switch (stage)
                {
                case WordLoadStage::Reading:
                    post(QStringLiteral("正在读取 Word 文档"), value * 0.08);
                    break;
                case WordLoadStage::Validating:
                    post(QStringLiteral("正在校验文档结构"), 0.08 + value * 0.04);
                    break;
                case WordLoadStage::Extracting:
                    post(QStringLiteral("正在解压文档资源 %1/%2").arg(completed).arg(total),
                        0.12 + value * 0.18);
                    break;
                case WordLoadStage::Parsing:
                    post(QStringLiteral("正在解析正文、表格与样式"), 0.30 + value * 0.15);
                    break;
                }
            });
            if (result.file.success)
            {
                result.editor = std::make_shared<PreparedEditor>();
                result.editor->document = create_word_document(result.file.document, width, true, text_option,
                    [&](std::size_t completed, std::size_t total)
                {
                    post(QStringLiteral("正在整理正文与表格 %1/%2").arg(completed).arg(total),
                        0.45 + fraction(completed, total) * 0.25);
                });
                if (!prepare_word_document_images(*result.editor->document,
                        [&](std::size_t completed, std::size_t total)
                {
                    post(QStringLiteral("正在缓存图片预览 %1/%2").arg(completed).arg(total),
                        0.70 + fraction(completed, total) * 0.24);
                }))
                {
                    result.file.success = false;
                    result.file.error = "图片预览准备失败，当前文档已保留。";
                    result.editor.reset();
                    return result;
                }
                post(QStringLiteral("正在完成文档排版"), 0.94);
                result.editor->document->documentLayout()->documentSize();
                post(QStringLiteral("正在准备编辑区"), 0.98);
                if (!result.editor->document->moveToThread(destination_thread))
                {
                    result.file.success = false;
                    result.file.error = "编辑区准备失败，当前文档已保留。";
                    result.editor.reset();
                }
            }
            return result;
        }));
    }

    void WordBridge::openSaveDialog()
    {
        save_dialog_open_ = true;
        emit stateChanged();
        emit saveDialogRequested();
    }

    void WordBridge::requestEditableCopy()
    {
        if (!active_ || locked() || !read_only_)
        {
            return;
        }
        copy_pending_ = true;
        openSaveDialog();
    }

    bool WordBridge::createEditableCopyTo(const QUrl& destination)
    {
        if (!active_ || locked() || !read_only_ ||
            !new_document_destination(destination, QStringList{"docx"}) ||
            same_document_path(source_path_, destination.toLocalFile()))
        {
            setMessage(QStringLiteral("请为只读 Word 指定已有目录下尚不存在的 DOCX 副本路径。"));
            return false;
        }
        copy_pending_ = true;
        beginSave(QFileInfo(destination.toLocalFile()).absoluteFilePath(), true);
        return operation_ == Operation::Save;
    }

    void WordBridge::save()
    {
        if (!active_ || locked() || read_only_)
        {
            return;
        }
        if (path_.isEmpty())
        {
            openSaveDialog();
        }
        else
        {
            beginSave(path_);
        }
    }

    bool WordBridge::saveTo(const QUrl& destination)
    {
        if (!active_ || locked() || read_only_ || !new_document_destination(destination, QStringList{"docx"}))
        {
            setMessage(QStringLiteral("请等待编辑完成，并选择已有目录下尚不存在的目标文件；不能覆盖原件。"));
            return false;
        }
        beginSave(QFileInfo(destination.toLocalFile()).absoluteFilePath(), true);
        return operation_ == Operation::Save;
    }

    void WordBridge::saveAs()
    {
        if (active_ && !locked() && !read_only_)
        {
            openSaveDialog();
        }
    }

    void WordBridge::selectSaveFile(const QUrl& url)
    {
        if (!save_dialog_open_)
        {
            return;
        }
        save_dialog_open_ = false;
        emit stateChanged();
        if (!url.isLocalFile() || url.toLocalFile().isEmpty())
        {
            cancelSaveDialog();
        }
        else
        {
            beginSave(url.toLocalFile());
        }
    }

    void WordBridge::cancelSaveDialog()
    {
        save_dialog_open_ = false;
        copy_pending_ = false;
        cancelPending();
        emit stateChanged();
    }

    void WordBridge::beginSave(const QString& path, bool new_file)
    {
        if (read_only_ && !copy_pending_)
        {
            return;
        }
        if (same_document_path(source_path_, path))
        {
            cancelSaveDialog();
            setMessage(QStringLiteral("请保存到新的文件名；导入源文件始终保留。"));
            return;
        }

        WordResult extracted;
        if (read_only_ && copy_pending_)
        {
            extracted.success = true;
            extracted.error.clear();
            extracted.document = document_;
        }
        else if (editor_)
            extracted = extract_word_document(*editor_);
        if (!extracted.success)
        {
            cancelSaveDialog();
            setMessage(
                QString::fromStdString(extracted.error.empty() ? "编辑区尚未准备好。" : extracted.error));
            return;
        }

        document_ = extracted.document;
        operation_ = Operation::Save;
        setMessage({});
        emit stateChanged();
        const bool copying = copy_pending_;
        const auto generation = ++load_generation_;
        if (copying)
        {
            loading_progress_ = 0;
            loading_stage_ = QStringLiteral("正在准备 Word 可编辑副本");
            emit loadingChanged();
            emit loadStarted();
        }
        const auto file = path.toStdString();
        const auto document = document_;
        auto expected = same_document_path(path_, path) ? disk_revision_ : std::string{};
        if (new_file)
            expected = "missing";
        const QPointer<WordBridge> guard(this);
        worker_.setFuture(QtConcurrent::run([file, document, expected, copying, guard, generation]()
        {
            const auto progress = [copying, guard, generation](auto stage, auto completed, auto total)
            {
                if (!copying || !guard)
                    return;
                const auto value = total ? qreal(completed) / total : 1.0;
                const auto amount =
                    stage == OfficeSaveStage::Committing ? 0.95 + 0.04 * value : 0.20 + 0.75 * value;
                const auto text = stage == OfficeSaveStage::Committing
                    ? QStringLiteral("正在提交 Word 副本")
                    : QStringLiteral("正在写入 Word 副本 %1/%2").arg(completed).arg(total);
                QMetaObject::invokeMethod(guard, [guard, generation, text, amount]()
                {
                    if (guard && guard->load_generation_ == generation && guard->copy_pending_ &&
                        guard->operation_ == Operation::Save)
                        guard->setLoadingProgress(text, amount);
                }, Qt::QueuedConnection);
            };
            return OperationResult{save_word_file(file, document, expected, progress), {}};
        }));
    }

    void WordBridge::completeOperation()
    {
        WordResult result;
        const auto operation = operation_;
        const bool copying = operation == Operation::Save && copy_pending_;
        try
        {
            auto completed = worker_.result();
            result = std::move(completed.file);
            if (operation == Operation::Load && result.success)
            {
                prepared_editor_ = std::move(completed.editor);
                awaiting_editor_ = true;
            }
        }

        catch (...)
        {
            result.error = "文件操作失败，当前文档已保留。";
        }

        operation_ = Operation::None;
        if (!result.success)
        {
            copy_pending_ = false;
            cancelPending();
            setMessage(QString::fromStdString(result.error));
            emit stateChanged();
            if (operation == Operation::Load)
            {
                emit openCompleted(false);
            }
            if (copying)
                emit copyCompleted(false);
            return;
        }

        path_ = QString::fromStdString(result.path);
        disk_revision_ = result.revision;
        if (operation == Operation::Load)
        {
            document_ = std::move(result.document);
            source_path_ = path_;
            active_ = true;
            read_only_ = true;
            modified_ = false;
            ++revision_;
        }

        else
        {
            read_only_ = false;
            copy_pending_ = false;
            modified_ = false;
            if (editor_)
            {
                editor_->setModified(false);
            }
            setMessage(QStringLiteral("DOCX 已保存。"));
        }

        emit documentChanged();
        emit stateChanged();
        emit fileRecorded(path_);
        if (operation == Operation::Load)
        {
            emit documentActivated();
            emit openCompleted(true);
        }

        else
        {
            if (copying)
            {
                setLoadingProgress(QStringLiteral("Word 副本已准备完成"), 1);
                emit loadReady();
                emit copyCompleted(true);
            }
            performPendingAction();
        }
    }
}
