#pragma once

#include <QJsonObject>

namespace mirrorfly
{
    QJsonObject office_ai_slide_style_fields();
    QJsonObject office_ai_validate_slide_style(const QJsonObject& style, bool text);
}
