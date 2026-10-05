#pragma once

#include <QAbstractListModel>
#include <QVariantList>

namespace mirrorfly
{
    class OfficeAiTimeline final : public QAbstractListModel
    {
    public:
        using QAbstractListModel::QAbstractListModel;
        int rowCount(const QModelIndex& parent = {}) const override;
        QVariant data(const QModelIndex& index, int role) const override;
        QHash<int, QByteArray> roleNames() const override;
        void append(const QVariantMap& row);
        void update(int index, const QVariantMap& row);
        void removeFirst();
        int size() const;
        QVariant at(int index) const;

    private:
        QVariantList rows_;
    };
}
