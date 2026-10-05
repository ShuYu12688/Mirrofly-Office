#include "presentation_edit_adapter.hpp"
#include "presentation_semantics.hpp"

#include <QJsonObject>
#include <QSet>

#include <cmath>

namespace mirrorfly
{
    QString presentation_option_error(const QString& action, const QVariantMap& options)
    {
        static const auto schemas = presentation_edit_schema();
        const auto schema = schemas.value(action).toMap();
        static const QSet<QString> flags{"before", "bold", "italic", "underline", "strike", "bullet",
            "numbered", "wrap", "autoFit", "shadowEnabled", "glowEnabled", "flipHorizontal", "flipVertical",
            "preserveAspect", "hidden", "advanceOnClick", "caseSensitive", "clipHorizontal", "clipVertical"};
        static const QSet<QString> numbers{"x", "y", "width", "height", "fontSize", "characterSpacing",
            "baseline", "marginLeft", "firstLineIndent", "lineSpacing", "spaceBefore", "spaceAfter",
            "insetLeft", "insetTop", "insetRight", "insetBottom", "fillOpacity", "gradientAngle",
            "outlineOpacity", "outlineWidth", "shadowOpacity", "shadowBlur", "shadowX", "shadowY",
            "glowOpacity", "glowRadius", "cropLeft", "cropRight", "cropTop", "cropBottom", "opacity",
            "rotation", "durationSeconds", "advanceAfterSeconds", "warpAdjustment"};
        static const QSet<QString> integers{"offset", "targetIndex", "targetSlide", "rows", "columns",
            "numberStart", "listLevel", "paragraphIndex", "shapeIndex", "slideIndex", "startByte"};
        static const QSet<QString> objects{
            "palette", "colors", "fonts", "style", "fill", "outline", "shadow", "glow", "reflection"};
        static const QSet<QString> metadata{
            "effect", "availability", "progress", "target", "guiPath", "slots"};
        const auto json = QJsonObject::fromVariantMap(options);
        for (auto it = json.begin(); it != json.end(); ++it)
        {
            const auto key = it.key();
            const auto value = it.value();
            if (!schema.contains(key) || metadata.contains(key) ||
                (key == "scope" && action != "replaceTextMatches"))
                return key;
            bool valid = true;
            if (flags.contains(key))
                valid = value.isBool();
            else if (numbers.contains(key) || integers.contains(key))
            {
                valid = value.isDouble() && std::isfinite(value.toDouble());
                if (integers.contains(key))
                    valid = valid && value.toDouble() >= -2147483648.0 && value.toDouble() <= 2147483647.0 &&
                        std::floor(value.toDouble()) == value.toDouble();
            }
            else if (objects.contains(key))
                valid = value.isObject();
            else
                valid = value.isString();
            if (!valid)
                return key;
        }
        return {};
    }
}
