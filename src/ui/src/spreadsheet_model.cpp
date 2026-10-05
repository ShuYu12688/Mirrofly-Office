#include "spreadsheet_model.hpp"
#include "spreadsheet_limits.hpp"

#include <QDate>
#include <QLocale>
#include <QVariantMap>

namespace mirrorfly
{
    SpreadsheetModel::SpreadsheetModel(QObject* parent) : QAbstractTableModel(parent)
    {
    }

    int SpreadsheetModel::rowCount(const QModelIndex& parent) const
    {
        return !parent.isValid() && document_ && sheet_ < document_->sheets.size()
            ? static_cast<int>(document_->sheets[sheet_].rows)
            : 0;
    }

    int SpreadsheetModel::columnCount(const QModelIndex& parent) const
    {
        return !parent.isValid() && document_ && sheet_ < document_->sheets.size()
            ? static_cast<int>(document_->sheets[sheet_].columns)
            : 0;
    }

    QVariant SpreadsheetModel::data(const QModelIndex& index, int role) const
    {
        if (!document_ || !index.isValid() || index.row() >= rowCount() || index.column() >= columnCount())
        {
            return {};
        }
        auto cell = spreadsheet_cell_properties(*document_, sheet_,
            {static_cast<std::uint32_t>(index.row()), static_cast<std::uint32_t>(index.column())});
        if (role == Qt::DisplayRole)
        {
            if (cell.formula_supported)
            {
                const auto& calculated = calculation();
                const auto values = calculated.values.find(sheet_);
                const SpreadsheetAddress address{
                    static_cast<std::uint32_t>(index.row()), static_cast<std::uint32_t>(index.column())};
                if (values == calculated.values.end() || !values->second.count(address))
                    return QStringLiteral("计算超限");
                cell.value = values->second.at(address);
            }
            if (cell.formula_cell && !cell.formula_supported &&
                (document_->caches_stale || cell.value.text.empty()))
                return QStringLiteral("待重算");
            const auto format = spreadsheet_cell_format(*document_, sheet_,
                {static_cast<std::uint32_t>(index.row()), static_cast<std::uint32_t>(index.column())});
            const auto kind = format.at("number");
            if (cell.value.kind == SpreadsheetValueKind::Number &&
                (kind == "1" || kind == "2" || kind == "3" || kind == "4" || kind == "9" || kind == "10" ||
                    kind == "14" || kind == "currency"))
            {
                const double value = QString::fromStdString(cell.value.text).toDouble();
                const int digits = format.count("decimals")
                    ? QString::fromStdString(format.at("decimals")).toInt()
                    : (kind == "1" || kind == "3" || kind == "9" ? 0 : 2);
                if (kind == "14")
                {
                    if (value < 0 || value > 2958465)
                        return QStringLiteral("日期超出范围");
                    if (document_->date_1904)
                        return QDate(1904, 1, 1).addDays(static_cast<int>(value)).toString("yyyy-MM-dd");
                    if (static_cast<int>(value) == 60)
                        return QStringLiteral("1900-02-29");
                    return QDate(1899, 12, 31)
                        .addDays(static_cast<int>(value) - (value >= 60 ? 1 : 0))
                        .toString("yyyy-MM-dd");
                }
                if (kind == "3" || kind == "4" || kind == "currency")
                    return (kind == "currency" ? QStringLiteral("¥") : QString{}) +
                        QLocale(QLocale::English).toString(value, 'f', digits);
                if (kind == "9" || kind == "10")
                    return QString::number(value * 100, 'f', digits) + "%";
                return QString::number(value, 'f', digits);
            }
            return QString::fromStdString(cell.value.text).left(maximum_cell_preview_units);
        }
        if (role == Qt::UserRole + 1)
        {
            return cell.formula_cell;
        }
        if (role == Qt::UserRole + 2)
        {
            return cell.editable;
        }
        if (role == Qt::UserRole + 3)
        {
            return displayFormat(sheet_,
                {static_cast<std::uint32_t>(index.row()), static_cast<std::uint32_t>(index.column())});
        }
        return {};
    }

    QVariant SpreadsheetModel::headerData(int section, Qt::Orientation orientation, int role) const
    {
        if (role != Qt::DisplayRole || section < 0)
        {
            return {};
        }
        if (orientation == Qt::Vertical)
        {
            return QString::number(section + 1);
        }
        auto address = QString::fromStdString(spreadsheet_address({0, static_cast<std::uint32_t>(section)}));
        address.chop(1);
        return address;
    }

    QHash<int, QByteArray> SpreadsheetModel::roleNames() const
    {
        return {{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "formulaCell"},
            {Qt::UserRole + 2, "editableCell"}, {Qt::UserRole + 3, "cellFormat"}};
    }

    void SpreadsheetModel::setDocument(const SpreadsheetDocument* document, std::size_t sheet,
        std::shared_ptr<const SpreadsheetCalculation> calculation)
    {
        beginResetModel();
        document_ = document;
        sheet_ = sheet;
        calculation_ = std::move(calculation);
        endResetModel();
    }

    QVariantMap SpreadsheetModel::displayFormat(std::size_t sheet, SpreadsheetAddress address) const
    {
        QVariantMap result;
        if (!document_ || sheet >= document_->sheets.size())
            return result;
        for (const auto& [key, value] : spreadsheet_cell_format(*document_, sheet, address))
            result.insert(QString::fromStdString(key), QString::fromStdString(value));
        const auto fill = spreadsheet_conditional_fill(*document_, sheet, address, &calculation());
        if (!fill.empty())
            result.insert("fill", QString::fromStdString(fill));
        return result;
    }

    void SpreadsheetModel::refresh()
    {
        beginResetModel();
        calculation_.reset();
        endResetModel();
    }

    void SpreadsheetModel::refresh(SpreadsheetRange range)
    {
        calculation_.reset();
        emit dataChanged(index(static_cast<int>(range.first.row), static_cast<int>(range.first.column)),
            index(static_cast<int>(range.last.row), static_cast<int>(range.last.column)));
    }

    const SpreadsheetCalculation& SpreadsheetModel::calculation() const
    {
        if (!calculation_)
            calculation_ = std::make_shared<const SpreadsheetCalculation>(
                document_ ? spreadsheet_calculate_all(*document_) : SpreadsheetCalculation{});
        return *calculation_;
    }

    bool SpreadsheetModel::rowVisible(std::uint32_t row) const
    {
        return document_ && spreadsheet_row_visible(*document_, sheet_, row, &calculation());
    }
}
