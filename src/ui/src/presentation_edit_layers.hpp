#pragma once

#include "presentation_scene.hpp"

#include <functional>

namespace mirrorfly
{
    // Paint callbacks use viewport coordinates; the selected object is never part of either cache.
    class PresentationEditLayers
    {
    public:
        using PaintRange = std::function<void(QPainter&, int first, int last, bool background)>;
        bool paint(QPainter& painter, const RenderPresentationPtr& document, int slide, int selected,
            int editing, const QVariantMap& theme, const QSizeF& viewport, const PaintRange& draw,
            bool allow_rebuild = true);

    private:
        std::weak_ptr<const RenderPresentation> document_;
        std::uint64_t revision_ = 0;
        QVariantMap theme_;
        QSizeF viewport_;
        QSize pixels_;
        int slide_ = -1;
        int selected_ = -1;
        int editing_ = -1;
        int count_ = 0;
        QImage below_;
        QImage above_;
    };
}
