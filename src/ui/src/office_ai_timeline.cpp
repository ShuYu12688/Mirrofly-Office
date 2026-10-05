#include "office_ai_timeline.hpp"

namespace mirrorfly
{
    int OfficeAiTimeline::rowCount(const QModelIndex& parent) const
    {
        return parent.isValid() ? 0 : size();
    }

    QVariant OfficeAiTimeline::data(const QModelIndex& index, int role) const
    {
        return index.isValid() && index.row() >= 0 && index.row() < size() && role == Qt::UserRole
            ? rows_.at(index.row())
            : QVariant{};
    }

    QHash<int, QByteArray> OfficeAiTimeline::roleNames() const
    {
        return {{Qt::UserRole, "modelData"}};
    }

    int OfficeAiTimeline::size() const
    {
        return static_cast<int>(rows_.size());
    }

    QVariant OfficeAiTimeline::at(int index) const
    {
        return rows_.at(index);
    }

    void OfficeAiTimeline::append(const QVariantMap& row)
    {
        beginInsertRows({}, size(), size());
        rows_.append(row);
        endInsertRows();
    }

    void OfficeAiTimeline::update(int row, const QVariantMap& value)
    {
        if (row < 0 || row >= size())
            return;
        rows_[row] = value;
        emit dataChanged(index(row), index(row), {Qt::UserRole});
    }

    void OfficeAiTimeline::removeFirst()
    {
        if (rows_.isEmpty())
            return;
        beginRemoveRows({}, 0, 0);
        rows_.removeFirst();
        endRemoveRows();
    }
}
