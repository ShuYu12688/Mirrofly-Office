#pragma once

#include <mirrorfly/presentation.hpp>

#include <QVariantMap>

namespace mirrorfly
{
    QVariantMap presentation_text_style_state(const PresentationShape& shape);
    std::optional<PresentationTextStylePatch> presentation_text_style_command(const QVariantMap& options);
}
