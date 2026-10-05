#include "office_ai_slide_style.hpp"

#include <QRegularExpression>
#include <QStringList>

#include <cmath>

namespace mirrorfly
{
    QJsonObject office_ai_slide_style_fields()
    {
        QJsonObject fields;
        const auto number = [&](const QString& name, double minimum, double maximum)
        {
            fields.insert(name, QJsonObject{{"type", "number"}, {"minimum", minimum}, {"maximum", maximum}});
        };
        number("fontSize", 1, 400);
        number("outlineWidth", 0, 72);
        number("shadowBlur", 0, 72);
        number("shadowOpacity", 0, 1);
        number("fillOpacity", 0, 1);
        number("outlineOpacity", 0, 1);
        number("shadowX", -200, 200);
        number("shadowY", -200, 200);
        for (const auto* name : {"textColor", "fillColor", "outlineColor", "shadowColor"})
            fields.insert(name, QJsonObject{{"type", "string"}, {"pattern", "^#[0-9a-fA-F]{6}$"}});
        for (const auto* name : {"bold", "italic", "underline", "strike", "shadowEnabled"})
            fields.insert(name, QJsonObject{{"type", "boolean"}});
        fields.insert("fontFamily", QJsonObject{{"type", "string"}, {"maxLength", 64}});
        return fields;
    }

    QJsonObject office_ai_validate_slide_style(const QJsonObject& style, bool text)
    {
        const auto fields = office_ai_slide_style_fields();
        QJsonObject shape_style;
        QJsonObject text_style;
        const QStringList text_fields{
            "fontSize", "fontFamily", "bold", "italic", "underline", "strike", "textColor"};
        for (auto it = style.begin(); it != style.end(); ++it)
        {
            const auto field = fields.value(it.key()).toObject();
            const auto value = it.value();
            bool valid = !field.isEmpty();
            const QString type = field.value("type").toString();
            if (type == "number")
                valid = value.isDouble() && std::isfinite(value.toDouble()) &&
                    value.toDouble() >= field.value("minimum").toDouble() &&
                    value.toDouble() <= field.value("maximum").toDouble();
            if (type == "boolean")
                valid = value.isBool();
            if (type == "string")
            {
                valid = value.isString();
                if (field.contains("pattern"))
                    valid = valid &&
                        QRegularExpression(field.value("pattern").toString())
                            .match(value.toString())
                            .hasMatch();
                if (field.contains("maxLength"))
                    valid = valid && value.toString().size() <= field.value("maxLength").toInt();
            }
            if (!valid || (!text && text_fields.contains(it.key())))
            {
                QString hint = "Use the declared style field and value range. No page actions have executed.";
                if (!text && text_fields.contains(it.key()))
                    hint = "Use text elements for font styles; shapes accept fill/outline/shadow only.";
                return {{"ok", false}, {"error", "invalid_style"}, {"field", it.key()}, {"expected", field},
                    {"elementType", text ? "text" : "shape"}, {"hint", hint}};
            }
            if (text_fields.contains(it.key()))
                text_style.insert(it.key(), value);
            else
                shape_style.insert(it.key(), value);
        }
        return {{"ok", true}, {"text", text_style}, {"shape", shape_style}};
    }
}
