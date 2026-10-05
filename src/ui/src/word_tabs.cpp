#include "word_tabs.hpp"
#include "word_format_properties.hpp"
#include "word_units.hpp"
#include <QJSValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <cmath>

namespace mirrorfly
{
    QVariantList inspect_word_tabs(const QTextBlockFormat& format)
    {
        if (format.hasProperty(word_tabs_property))
            return format.property(word_tabs_property).toList();
        QVariantList result;
        for (const auto& tab : format.tabPositions())
        {
            const char* alignment = "left";
            if (tab.type == QTextOption::RightTab)
                alignment = "right";
            else if (tab.type == QTextOption::CenterTab)
                alignment = "center";
            else if (tab.type == QTextOption::DelimiterTab)
                alignment = "decimal";
            result.push_back(
                QVariantMap{{"position", word_pixels_to_points(tab.position + format.leftMargin())},
                    {"alignment", alignment}, {"leader", "none"}});
        }
        return result;
    }

    std::vector<WordTabStop> word_tabs_from_format(const QTextBlockFormat& format)
    {
        std::vector<WordTabStop> result;
        for (const auto& value : inspect_word_tabs(format))
        {
            const auto stop = value.toMap();
            result.push_back(
                {stop.value("position").toDouble(), stop.value("alignment").toString().toStdString(),
                    stop.value("leader").toString().toStdString()});
        }
        return result;
    }

    void apply_word_tabs(QTextBlockFormat& format, const std::vector<WordTabStop>& tabs)
    {
        QVariantList stored;
        QList<QTextOption::Tab> native;
        for (const auto& tab : tabs)
        {
            stored.push_back(
                QVariantMap{{"position", tab.position}, {"alignment", QString::fromStdString(tab.alignment)},
                    {"leader", QString::fromStdString(tab.leader)}});
            if (tab.position < 0 || tab.alignment == "bar")
                continue;
            QTextOption::Tab value;
            // Qt measures native tabs from the paragraph's left margin; OOXML uses the text area.
            value.position = word_points_to_pixels(tab.position) - format.leftMargin();
            value.type = QTextOption::LeftTab;
            if (tab.alignment == "right")
                value.type = QTextOption::RightTab;
            else if (tab.alignment == "center")
                value.type = QTextOption::CenterTab;
            else if (tab.alignment == "decimal")
                value.type = QTextOption::DelimiterTab;
            value.delimiter = QLocale().decimalPoint().front();
            native.push_back(value);
        }
        format.setTabPositions(native);
        format.setProperty(word_tabs_property, stored);
    }

    bool format_word_tabs(QTextDocument& document, QTextCursor& cursor, const QVariant& value)
    {
        const auto input = QJsonValue::fromVariant(value.metaType() == QMetaType::fromType<QJSValue>()
                ? value.value<QJSValue>().toVariant()
                : value);
        if (!input.isArray() || input.toArray().size() > 64)
            return false;
        std::vector<WordTabStop> stops;
        double previous = -1;
        for (const auto& entry : input.toArray())
        {
            const auto stop = entry.toObject();
            if (!entry.isObject() || stop.size() != 3 || !stop.value("position").isDouble() ||
                !stop.value("alignment").isString() || !stop.value("leader").isString())
                return false;
            const auto position = stop.value("position").toDouble();
            const auto alignment = stop.value("alignment").toString();
            const auto leader = stop.value("leader").toString();
            if (!std::isfinite(position) || position < 0 || position > maximum_word_indent_points ||
                position <= previous || std::abs(position * 20 - std::round(position * 20)) > 0.0001 ||
                !QStringList{"left", "center", "right", "decimal", "bar"}.contains(alignment) ||
                !QStringList{"none", "dot", "hyphen", "underscore", "heavy", "middleDot"}.contains(leader) ||
                (alignment == "bar" && leader != "none"))
                return false;
            stops.push_back({position, alignment.toStdString(), leader.toStdString()});
            previous = position;
        }
        cursor.beginEditBlock();
        const int last = std::max(cursor.selectionStart(), cursor.selectionEnd() - 1);
        for (auto block = document.findBlock(cursor.selectionStart());
            block.isValid() && block.position() <= last; block = block.next())
        {
            auto format = block.blockFormat();
            apply_word_tabs(format, stops);
            QTextCursor(block).setBlockFormat(format);
        }
        cursor.endEditBlock();
        return true;
    }

    bool word_has_tab_decoration(const QTextBlock& block)
    {
        for (const auto& tab : word_tabs_from_format(block.blockFormat()))
            if (tab.alignment == "bar" || (tab.leader != "none" && block.text().contains(QChar::Tabulation)))
                return true;
        return false;
    }
}
