#include "word_viewport.hpp"

#include <QAbstractTextDocumentLayout>
#include <QTextCursor>
#include <algorithm>
#include <cmath>

namespace
{
    bool compact_lines(const QTextBlock& block)
    {
        const auto format = block.blockFormat();
        return format.lineHeightType() == QTextBlockFormat::FixedHeight ||
            (format.lineHeightType() == QTextBlockFormat::ProportionalHeight && format.lineHeight() < 100);
    }
}

namespace mirrorfly
{
    WordViewport::WordViewport(QQuickItem* parent) : QQuickPaintedItem(parent)
    {
        setAntialiasing(true);
        setAcceptedMouseButtons(Qt::NoButton);
    }

    QQuickTextDocument* WordViewport::textDocument() const
    {
        return wrapper_;
    }
    qreal WordViewport::documentTop() const
    {
        return top_;
    }
    QVariantMap WordViewport::selection() const
    {
        return selection_;
    }

    QRectF WordViewport::lastPaintedDocumentArea() const
    {
        return last_painted_document_area_;
    }

    qreal WordViewport::renderScale() const
    {
        return scale_;
    }

    void WordViewport::setRenderScale(qreal scale)
    {
        if (!std::isfinite(scale) || scale <= 0 || scale_ == scale)
            return;
        scale_ = scale;
        emit viewChanged();
        invalidate();
    }

    void WordViewport::setTextDocument(QQuickTextDocument* wrapper)
    {
        if (wrapper_ == wrapper)
            return;
        if (wrapper_)
            disconnect(wrapper_, nullptr, this, nullptr);
        wrapper_ = wrapper;
        if (wrapper_)
        {
            connect(wrapper_, &QQuickTextDocument::textDocumentChanged, this, &WordViewport::attachDocument);
            connect(wrapper_, &QObject::destroyed, this, &WordViewport::attachDocument);
        }
        attachDocument();
        emit documentChanged();
    }

    void WordViewport::attachDocument()
    {
        if (document_)
        {
            disconnect(document_, nullptr, this, nullptr);
            disconnect(document_->documentLayout(), nullptr, this, nullptr);
        }
        document_ = wrapper_ ? wrapper_->textDocument() : nullptr;
        compact_blocks_.clear();
        if (document_)
        {
            trackCompactBlocks(0, document_->characterCount());
            connect(document_, &QTextDocument::contentsChange, this, [this](int position, int, int added)
            {
                // Fixed/tight lines may paint beyond their layout bounds. Keep old membership until
                // after the edit so shrinking text, removing a line or changing its rule clears old ink.
                if (trackCompactBlocks(position, added))
                    invalidate();
            });
            connect(document_->documentLayout(), &QAbstractTextDocumentLayout::update, this,
                &WordViewport::invalidateDocumentRect);
            connect(document_->documentLayout(), &QAbstractTextDocumentLayout::updateBlock, this,
                &WordViewport::invalidateBlock);
        }
        invalidate();
    }

    bool WordViewport::trackCompactBlocks(int position, int added)
    {
        const auto first = document_->findBlock(position);
        const auto last = position + added;
        bool affected = false;
        const auto changed = [&](const QTextBlock& block)
        {
            const bool matches = !block.isValid() ||
                (first.isValid() && block.position() >= first.position() && block.position() <= last);
            affected = affected || matches;
            return matches;
        };
        compact_blocks_.erase(
            std::remove_if(compact_blocks_.begin(), compact_blocks_.end(), changed), compact_blocks_.end());
        for (auto block = first; block.isValid() && block.position() <= last; block = block.next())
        {
            if (compact_lines(block))
            {
                compact_blocks_.append(block);
                affected = true;
            }
        }
        return affected;
    }

    void WordViewport::setDocumentTop(qreal top)
    {
        if (!std::isfinite(top) || top_ == top)
            return;
        top_ = top;
        emit viewChanged();
        if (pixels_.isNull() || full_dirty_)
        {
            invalidate();
            return;
        }
        polish();
        update();
    }

    void WordViewport::setSelection(const QVariantMap& selection)
    {
        if (selection_ == selection)
            return;
        const auto dirty = selectionBounds(selection_).united(selectionBounds(selection));
        selection_ = selection;
        emit viewChanged();
        if (dirty.isValid() && !dirty.isEmpty())
            invalidateDocumentRect(dirty);
        else
            update();
    }

    void WordViewport::invalidate()
    {
        full_dirty_ = true;
        dirty_document_ = {};
        polish();
        update();
    }

    void WordViewport::invalidateDocumentRect(const QRectF& area)
    {
        if (!document_ || area.isNull() || !area.isValid())
        {
            invalidate();
            return;
        }
        const QRectF visible(0, top_, width(), height());
        const auto expanded = area.adjusted(-2, -2, 2, 2).intersected(visible);
        if (expanded.isEmpty())
        {
            return;
        }
        dirty_document_ = dirty_document_.isEmpty() ? expanded : dirty_document_.united(expanded);
        polish();
        // The cached raster is already region-limited. Publish it as one texture so Quick's
        // independently rounded dirty clip cannot cut a resampled glyph at the upload boundary.
        update();
    }

    void WordViewport::invalidateBlock(const QTextBlock& block)
    {
        if (!document_ || !block.isValid())
        {
            invalidate();
            return;
        }
        invalidateDocumentRect(document_->documentLayout()->blockBoundingRect(block));
    }

    QRectF WordViewport::selectionBounds(const QVariantMap& selection) const
    {
        if (!document_)
        {
            return {};
        }
        const auto start = selection.value("start", -1).toInt();
        const auto end = selection.value("end", -1).toInt();
        if (start < 0 || end <= start || start >= document_->characterCount())
        {
            return {};
        }
        auto block = document_->findBlock(start);
        const auto last = document_->findBlock(std::min(end - 1, document_->characterCount() - 1));
        QRectF bounds;
        while (block.isValid())
        {
            if (compact_lines(block))
                return QRectF(0, top_, width(), height());
            const auto area = document_->documentLayout()->blockBoundingRect(block);
            bounds = bounds.isEmpty() ? area : bounds.united(area);
            if (block == last)
            {
                break;
            }
            block = block.next();
        }
        return bounds;
    }

    void WordViewport::geometryChange(const QRectF& next, const QRectF& previous)
    {
        QQuickPaintedItem::geometryChange(next, previous);
        if (next.size() != previous.size())
            invalidate();
    }

    void WordViewport::itemChange(ItemChange change, const ItemChangeData& data)
    {
        QQuickPaintedItem::itemChange(change, data);
        if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange)
            invalidate();
    }

}
