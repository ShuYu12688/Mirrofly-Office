#include "document_view.hpp"

#include <cmath>

namespace mirrorfly
{
    DocumentView::DocumentView(QObject* parent) : QObject(parent)
    {
    }

    qreal DocumentView::zoom() const
    {
        return zoom_;
    }

    bool DocumentView::setZoom(qreal value)
    {
        if (!std::isfinite(value) || value < 0.25 || value > 4)
        {
            return false;
        }
        if (!qFuzzyCompare(zoom_, value))
        {
            zoom_ = value;
            emit zoomChanged();
        }
        return true;
    }
}
