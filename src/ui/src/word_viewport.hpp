#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace mirrorfly
{
    class WordViewport : public QQuickPaintedItem
    {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(
            QQuickTextDocument* textDocument READ textDocument WRITE setTextDocument NOTIFY documentChanged)
        Q_PROPERTY(qreal documentTop READ documentTop WRITE setDocumentTop NOTIFY viewChanged)
        Q_PROPERTY(qreal renderScale READ renderScale WRITE setRenderScale NOTIFY viewChanged)
        Q_PROPERTY(QVariantMap selection READ selection WRITE setSelection NOTIFY viewChanged)

    public:
        explicit WordViewport(QQuickItem* parent = nullptr);
        QQuickTextDocument* textDocument() const;
        void setTextDocument(QQuickTextDocument* wrapper);
        qreal documentTop() const;
        void setDocumentTop(qreal top);
        qreal renderScale() const;
        void setRenderScale(qreal scale);
        QVariantMap selection() const;
        void setSelection(const QVariantMap& selection);
        QRectF lastPaintedDocumentArea() const;
        void paint(QPainter* painter) override;

    signals:
        void documentChanged();
        void viewChanged();

    protected:
        void updatePolish() override;
        void geometryChange(const QRectF& next, const QRectF& previous) override;
        void itemChange(ItemChange change, const ItemChangeData& data) override;

    private:
        void attachDocument();
        void invalidate();
        void invalidateDocumentRect(const QRectF& area);
        void invalidateBlock(const QTextBlock& block);
        bool trackCompactBlocks(int position, int added);
        QRectF selectionBounds(const QVariantMap& selection) const;
        QPointer<QQuickTextDocument> wrapper_;
        QPointer<QTextDocument> document_;
        qreal top_ = 0;
        qreal painted_top_ = 0;
        qreal scale_ = 1;
        QVariantMap selection_;
        QImage pixels_;
        bool full_dirty_ = true;
        QRectF dirty_document_;
        QRectF last_painted_document_area_;
        QList<QTextBlock> compact_blocks_;
    };
}
