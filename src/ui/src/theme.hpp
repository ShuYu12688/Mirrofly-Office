#pragma once

#include <QString>
#include <QVariantMap>

namespace mirrorfly
{
    struct ThemeResult
    {
        QVariantMap values;
        QString warning;
    };

    ThemeResult load_theme(const QString& application_directory);
}
