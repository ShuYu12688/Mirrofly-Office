#include "word_bridge.hpp"

#include <QFontDatabase>

namespace mirrorfly
{
    QVariantMap WordBridge::editSchema() const
    {
        QVariantMap formats;
        const auto add = [&formats](QStringList keys, QString type, QString scope, QVariantMap details = {})
        {
            for (const auto& action : keys)
            {
                auto value = details;
                value.insert("type", type);
                value.insert("scope", scope);
                formats.insert(action, value);
            }
        };
        add({"bold", "italic", "underline", "strike", "outline"}, "boolean", "character");
        add({"underlineStyle", "strikeStyle"}, "string", "character",
            {{"values", QStringList{"none", "single", "double"}},
                {"legacy", "underline/strike booleans select single or clear all line variants"}});
        add({"rtl"}, "boolean", "paragraph");
        add({"font"}, "string", "character", {{"values", QFontDatabase::families()}});
        add({"size"}, "number", "character", {{"unit", "pt"}, {"minimum", 6}, {"maximum", 96}});
        add({"script"}, "integer", "character",
            {{"values", QVariantList{-1, 0, 1}}, {"meaning", "subscript|normal|superscript"}});
        add({"characterSpacing"}, "number", "character", {{"unit", "pt"}, {"minimum", -3}, {"maximum", 20}});
        const QVariantMap color{{"pattern", "empty string or #RRGGBB"}, {"empty", "clear property"}};
        add({"color", "highlight", "characterBorder"}, "string", "character", color);
        add({"paragraphFill", "paragraphBorder", "paragraphBottomBorder"}, "string", "paragraph", color);
        add({"grow", "shrink", "clear", "rubyAuto", "rubyClear"}, "ignored", "character",
            {{"argument", "supply false; the positional value argument is still required"}});
        add({"ruby"}, "string", "character",
            {{"selection", "one Unicode scalar for explicit ruby; empty value clears"}});
        add({"heading"}, "integer", "paragraph", {{"minimum", 0}, {"maximum", 3}});
        add({"align"}, "integer", "paragraph",
            {{"minimum", 0}, {"maximum", 4}, {"meaning", "0:left,1:center,2:right,3:justify,4:distributed"}});
        add({"list"}, "integer", "paragraph",
            {{"values", QVariantList{0, 1, 2}}, {"meaning", "none|bullet|numbered"}});
        add({"listLevel"}, "integer", "paragraph", {{"minimum", 0}, {"maximum", 2}});
        const QStringList markers{
            "disc", "circle", "square", "decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman"};
        add({"listMarker"}, "string", "paragraph",
            {{"values", markers}, {"inspectFields", "listStyle"},
                {"constraint",
                    "selected paragraphs become one independent list; empty selection means current "
                    "paragraph"}});
        add({"listStart"}, "integer", "paragraph",
            {{"minimum", 0}, {"maximum", 1000000}, {"inspectFields", "listStyle.start,number,label"},
                {"constraint",
                    "selected paragraphs must belong to one numbered list; creates an independent selected "
                    "list; unselected following paragraphs stay in their original list; letters start >=1; "
                    "Roman displayed numbers must stay within 1..4999"}});
        add({"tabStops"}, "array", "paragraph",
            {{"items", "objects with exactly position (pt), alignment and leader"}, {"maximumItems", 64},
                {"position", "0..504 pt, ascending unique values in 0.05pt increments"},
                {"alignment", "left|center|right|decimal|bar"},
                {"leader", "none|dot|hyphen|underscore|heavy|middleDot; bar requires none"},
                {"inspectFields", "tabStops,defaultTabStop,tabLayoutSupported,tabLayoutReason"},
                {"constraint",
                    "replace the whole custom-stop array in selected paragraphs; [] clears inherited and "
                    "direct stops; positions are text-area-relative and stay unchanged when leftIndent "
                    "changes; preserve text Tab characters; defaultTabStop is read-only; "
                    "tabLayoutSupported=false reports preview/PDF limitations for first-line/hanging "
                    "indentation or center/right tabs with a paragraph soft break, "
                    "not data loss or a prohibition on editing"}});
        add({"spacing"}, "number", "paragraph", {{"unit", "ratio"}, {"minimum", 1}, {"maximum", 2}});
        const QVariantMap line_fields{{"rule", "integer 0:multiple,1:exact,2:minimum"},
            {"value", "rule=0: ratio [1,2]; otherwise pt [1,144]"}};
        add({"lineSpacing"}, "object", "paragraph",
            {{"fields", line_fields}, {"additionalProperties", false},
                {"inspectFields", "lineSpacingRule,lineSpacingPoints,spacing"}});
        add({"leftIndent", "rightIndent"}, "number", "paragraph",
            {{"unit", "pt"}, {"minimum", 0}, {"maximum", maximum_word_indent_points}});
        add({"firstLineIndent"}, "number", "paragraph",
            {{"unit", "pt"}, {"minimum", -144}, {"maximum", maximum_word_spacing_points},
                {"constraint", "negative magnitude <= leftIndent"}});
        add({"spaceBefore", "spaceAfter"}, "number", "paragraph",
            {{"unit", "pt"}, {"minimum", 0}, {"maximum", maximum_word_spacing_points}});
        const QStringList styles{"normal", "heading1", "heading2", "heading3", "strong", "points", "emphasis",
            "quote", "header", "pageNumber", "web", "defaultFont"};
        add({"style"}, "string", "paragraph",
            {{"values", styles},
                {"constraint",
                    "header/pageNumber are text presets, not header regions or automatic page fields"}});
        const QStringList sorts{"textAscending", "textDescending", "numberAscending", "numberDescending"};
        add({"sort"}, "string", "paragraph",
            {{"values", sorts}, {"constraint", "new documents only; empty selection sorts all"}});
        const QVariantMap border_fields{{"edge", "left|top|right|bottom"},
            {"style", "nil|single|double|dotted|dashed"}, {"color", "#RRGGBB"},
            {"width", "pt, 0.25..12 in 0.125 increments; nil also allows 0"}};
        add({"cellBorder"}, "object", "cell",
            {{"fields", border_fields}, {"additionalProperties", false},
                {"inspectFields", "cellBorders.source,cellBorders.display; zero-based cellRow/cellColumn"},
                {"constraint",
                    "one whole cell edge; merged side affects all its segments; nil suppresses shared "
                    "border"}});
        add({"cellFill"}, "string", "cell", color);
        add({"cellAlign"}, "integer", "cell",
            {{"values", QVariantList{0, 1, 2}}, {"meaning", "top|middle|bottom"}});
        add({"cellLeft", "cellTop", "cellRight", "cellBottom"}, "number", "cell",
            {{"unit", "pt"}, {"minimum", 0}, {"maximum", 144}});
        return {{"schemaVersion", 1}, {"formats", formats}, {"execution", "format(start,end,action,value)"},
            {"positions", "zero-based UTF-16 editor positions; end-exclusive"},
            {"emptySelection", "formats current paragraph, not an independent future typing style"},
            {"cellSelection", "must stay within one cell; no table structural edits"},
            {"identity", "positions are invalidated by intervening edits; refresh the global revision"},
            {"requires", "active, attached editor, unlocked, not readOnly; imported files need a copy"},
            {"contentTrust", "untrustedDocumentData"}};
    }
}
