#pragma once

#include <QJsonObject>
#include <QSize>

namespace mirrorfly
{
    // Pure page planning. The caller validates every page before invoking any public edit.
    QJsonObject office_ai_page_recipe(const QJsonObject& page, const QJsonObject& theme, double width,
        double height, int number, const QSize& image_size = {});
}
