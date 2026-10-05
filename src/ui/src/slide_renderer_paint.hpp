#pragma once

#include "presentation_scene.hpp"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QVariantMap>

#include <string>
#include <vector>

namespace mirrorfly::slide_paint
{
    bool shape_has_deferred_asset(const PresentationShape& shape);
    bool slide_has_deferred_assets(const PresentationSlide& slide);
    QString utf8(const std::string& value);
    bool animation_space(char32_t character);
    QColor document_color(const std::string& value, double opacity);
    QBrush fill_brush(
        const PresentationFill& fill, const QRectF& bounds, const RenderPresentationPtr& document);
    QFont run_font(const RenderPresentationPtr& document, const PresentationRun& run, double scale,
        bool east_asian_first = false);
    double shape_effect_padding(const PresentationShape& shape);
    PresentationShape preview_shape(const PresentationShape& shape, const QVariantMap& preview);
    QRectF shape_scene_bounds(const PresentationShape& shape);
    bool paint_shape_patch(QPainter& painter, const RenderPresentationPtr& document, int slide_index,
        const QVariantMap& theme, const QSizeF& viewport, const PresentationShape& previous,
        int replacement_index, const PresentationShape* replacement);
    void draw_shape(QPainter* painter, const RenderPresentationPtr& document, const PresentationShape& shape,
        const QVariantMap& theme, bool editing_text, const QImage& video_frame = {},
        const std::vector<PresentationAnimationState>& paragraph_states = {},
        const std::vector<std::vector<PresentationAnimationState>>& character_states = {});
}
