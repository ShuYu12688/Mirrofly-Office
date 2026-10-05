#pragma once

#include <mirrorfly/presentation.hpp>

#include <QPainter>
#include <QTextDocument>
#include <QTextFormat>

namespace mirrorfly
{
    constexpr int presentation_text_effect_property = QTextFormat::UserProperty + 77;
    constexpr int presentation_relative_before_property = QTextFormat::UserProperty + 78;
    constexpr int presentation_relative_after_property = QTextFormat::UserProperty + 79;

    void fit_presentation_text(QTextDocument& document, const QSizeF& bounds, bool shrink_to_cell = false);

    void paint_presentation_text(QPainter& painter, QTextDocument& document, const PresentationText& text,
        const QRectF& area, double offset, const std::vector<PresentationTextEffects>& effects);
}
