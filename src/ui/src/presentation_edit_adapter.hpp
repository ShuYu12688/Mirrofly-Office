#pragma once

#include <mirrorfly/presentation.hpp>

#include <QString>
#include <QVariantMap>

#include <optional>

namespace mirrorfly
{
    std::optional<PresentationSlideLayout> presentation_slide_layout(const QString& name);
    PresentationTemplatePalette presentation_template_palette(const QVariantMap& options);
    QString presentation_option_error(const QString& action, const QVariantMap& options);
    PresentationEditCommand presentation_edit_command(
        const QString& action, const QVariantMap& options, int current_slide, int selected_shape);
}
