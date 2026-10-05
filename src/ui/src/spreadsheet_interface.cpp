#include "spreadsheet_bridge.hpp"

namespace mirrorfly
{
    QVariantMap SpreadsheetBridge::editSchema() const
    {
        QVariantMap fields{{"font", "installed font family"}, {"size", "pt [6,96]"},
            {"fill", "#RRGGBB or none"}, {"text", "#RRGGBB"},
            {"align", "general|left|center|right|justify|distributed"}, {"valign", "top|center|bottom"},
            {"indent", "integer [0,15]"}, {"decimals", "integer [0,10]"},
            {"textRotation",
                "integer 0..180 or 255; 0=horizontal, 1..90=counterclockwise degrees, "
                "91..180=clockwise (value-90) degrees, 255=stacked upright characters"},
            {"number",
                "0:general,1:integer,2:decimal,3:grouped integer,4:grouped decimal,"
                "9:percent,10:decimal percent,14:date,49:text,currency"}};
        for (const auto* key :
            {"bold", "italic", "underline", "strike", "wrap", "shrinkToFit", "border", "reset"})
            fields.insert(key, "boolean");
        for (const auto* edge : {"Left", "Right", "Top", "Bottom"})
        {
            const auto key = QStringLiteral("border") + edge;
            fields.insert(key,
                "none|thin|medium|thick|double|dotted|dashed|hair|dashDot|dashDotDot|"
                "mediumDashed|mediumDashDot|mediumDashDotDot|slantDashDot");
            fields.insert(key + "Color", "#RRGGBB");
        }
        return {{"schemaVersion", 1}, {"indices", "zero-based rows and columns; A1 addresses also supported"},
            {"selection", "selectCell(row,column,extend); selectAddress(A1:B5); maximum 4096 cells"},
            {"formatSelection", QVariantMap{{"patch", fields}, {"scope", "current selected rectangle"}}},
            {"styleSelection",
                QVariantMap{
                    {"preset", "header|banded|plain|normal|good|bad|neutral|input|output|title|total"},
                    {"palette",
                        "header/banded: header,body,alternate,headerText,bodyText colors; "
                        "plain/normal: {}; other presets: fill,text,line colors; all #RRGGBB"}}},
            {"setCellValue",
                QVariantMap{{"kind", "auto|text|number|boolean"},
                    {"text", "use kind=auto and text starting with = for formulas; formula reads omit ="}}},
            {"pasteText", QVariantMap{{"text", "TSV rows separated by newline, columns by tab; =formula"}}},
            {"resizeSelection",
                QVariantMap{{"columns", "true: character widths [3,80]; false: row heights pt [12,300]"},
                    {"size", "finite number in the corresponding unit"}}},
            {"startTool",
                QVariantMap{{"insertRows|insertColumns",
                                "selected range; count integer [1,4096], optional after boolean"},
                    {"deleteRows|deleteColumns", "selected range; count integer [1,4096]"},
                    {"copySheet|renameSheet|addSheet", "name string; copySheet can choose a default"},
                    {"moveSheet|showSheet", "index zero-based integer"},
                    {"deleteSheet|hideSheet", "no arguments; current sheet, original preserved in undo"},
                    {"merge|unmerge|freeze|unfreeze|filter|clearFilter|tableStyle",
                        "operate on current selection; inspect the selected range and current sheet first"},
                    {"otherActions",
                        "Use the public startTool action catalog; unknown action/argument is rejected"}}},
            {"insertTemplate", QVariantMap{{"name", "references|tasks|budget; destination must be empty"}}}};
    }
}
