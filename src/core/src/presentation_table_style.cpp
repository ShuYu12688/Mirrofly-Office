#include "presentation_table_style.hpp"

namespace mirrorfly
{
    const char* authored_table_style_id()
    {
        return "{C944F620-5537-4C75-882B-AD91E2E53E77}";
    }

    const std::string& authored_table_styles_xml()
    {
        static const std::string xml = []
        {
            const auto region =
                [](const char* name, const char* fill_slot, const char* text_slot, bool bold, int tint)
            {
                std::string result = "<a:" + std::string(name) + "><a:tcTxStyle";
                if (bold)
                    result += " b=\"1\"";
                result += "><a:fontRef idx=\"minor\"><a:schemeClr val=\"dk1\"/></a:fontRef>";
                result += "<a:schemeClr val=\"";
                result += text_slot;
                result += "\"/></a:tcTxStyle><a:tcStyle><a:fill><a:solidFill><a:schemeClr val=\"";
                result += fill_slot;
                result += "\"";
                if (tint > 0)
                    result += "><a:tint val=\"" + std::to_string(tint) + "\"/></a:schemeClr>";
                else
                    result += "/>";
                result += "</a:solidFill></a:fill></a:tcStyle></a:";
                result += name;
                result += ">";
                return result;
            };
            std::string result = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
            result +=
                "<a:tblStyleLst xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" def=\"";
            result += authored_table_style_id();
            result += "\"><a:tblStyle styleId=\"";
            result += authored_table_style_id();
            result += "\" styleName=\"Mirrorfly Theme\">";
            result += region("wholeTbl", "lt1", "dk1", false, 0);
            result += region("band1H", "accent1", "dk1", false, 85000);
            result += region("band2H", "lt1", "dk1", false, 0);
            result += region("band1V", "accent2", "dk1", false, 85000);
            result += region("band2V", "lt1", "dk1", false, 0);
            result += region("firstCol", "accent3", "dk1", true, 70000);
            result += region("lastCol", "accent4", "dk1", true, 70000);
            result += region("firstRow", "accent1", "lt1", true, 0);
            result += region("lastRow", "accent2", "lt1", true, 0);
            result += "</a:tblStyle></a:tblStyleLst>";
            return result;
        }();
        return xml;
    }
}
