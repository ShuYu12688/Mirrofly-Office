#pragma once
#include <QString>
namespace mirrorfly::detail
{
    inline QString quote_prefix(int level)
    {
        return QStringLiteral("> ").repeated(level);
    }
    inline QString delimiter_text(const QString& alignment, QString hyphens = QStringLiteral("---"))
    {
        return (alignment == "left" || alignment == "center" ? ":" : "") + hyphens +
            (alignment == "right" || alignment == "center" ? ":" : "");
    }
}
