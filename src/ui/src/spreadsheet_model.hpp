#pragma once

#include <mirrorfly/spreadsheet.hpp>

#include <QAbstractTableModel>

namespace mirrorfly
{
    class SpreadsheetModel final : public QAbstractTableModel
    {
        Q_OBJECT

    public:
        explicit SpreadsheetModel(QObject* parent = nullptr);
        int rowCount(const QModelIndex& parent = {}) const override;
        int columnCount(const QModelIndex& parent = {}) const override;
        QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
        QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
        QHash<int, QByteArray> roleNames() const override;
        void setDocument(const SpreadsheetDocument* document, std::size_t sheet,
            std::shared_ptr<const SpreadsheetCalculation> calculation = {});
        void refresh();
        void refresh(SpreadsheetRange range);
        bool rowVisible(std::uint32_t row) const;
        QVariantMap displayFormat(std::size_t sheet, SpreadsheetAddress address) const;

    private:
        const SpreadsheetDocument* document_ = nullptr;
        std::size_t sheet_ = 0;
        const SpreadsheetCalculation& calculation() const;
        mutable std::shared_ptr<const SpreadsheetCalculation> calculation_;
    };
}
