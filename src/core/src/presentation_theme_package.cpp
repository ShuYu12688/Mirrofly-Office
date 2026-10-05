#include "presentation_theme_package.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

namespace mirrorfly
{
    std::string fixed_group_tree()
    {
        std::string xml = "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>";
        xml += "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>";
        xml += "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>";
        return xml;
    }

    PresentationThemeDefinition authored_theme_definition(const std::array<std::string, 6>& palette)
    {
        PresentationThemeDefinition definition;
        definition.name = "Mirrorfly Office";
        definition.colors = {{"dk1", "#000000"}, {"lt1", palette[1]}, {"dk2", palette[0]},
            {"lt2", palette[2]}, {"accent1", palette[4]}, {"accent2", palette[3]}, {"accent3", palette[5]},
            {"accent4", "#C8A86B"}, {"accent5", "#6C7A89"}, {"accent6", "#A46B66"}, {"hlink", "#76543C"},
            {"folHlink", "#795C70"}};
        definition.fonts = {{"majorLatin", "Arial"}, {"minorLatin", "Arial"},
            {"majorEastAsian", "Microsoft YaHei"}, {"minorEastAsian", "Microsoft YaHei"}};
        for (const auto& entry : definition.colors)
            definition.editable_color_slots.insert(entry.first);
        for (const auto& entry : definition.fonts)
            definition.editable_font_slots.insert(entry.first);
        return definition;
    }

    bool append_authored_theme_parts(
        std::vector<PresentationPart>& parts, const std::vector<std::array<std::string, 6>>& palettes)
    {
        const auto append = [](std::vector<PresentationPart>& output, std::string path, std::string bytes)
        {
            output.push_back({std::move(path), std::move(bytes)});
        };
        // clang-format off: fixed Open XML fragments use four-space indentation.
        append(parts, "ppt/slideMasters/slideMaster1.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<p:sldMaster xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
            "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"><p:cSld><p:spTree>" +
                fixed_group_tree() +
                "</p:spTree></p:cSld><p:clrMap accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" "
                "accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" bg1=\"lt1\" bg2=\"lt2\" "
                "folHlink=\"folHlink\" hlink=\"hlink\" tx1=\"dk1\" "
                "tx2=\"dk2\"/><p:sldLayoutIdLst><p:sldLayoutId id=\"2147483648\" "
                "r:id=\"rId1\"/></p:sldLayoutIdLst><p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></"
                "p:txStyles></p:sldMaster>");
        append(parts, "ppt/slideMasters/_rels/slideMaster1.xml.rels",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" "
            "Target=\"../slideLayouts/slideLayout1.xml\"/>"
            "<Relationship Id=\"rId2\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" "
            "Target=\"../theme/theme1.xml\"/>"
            "</Relationships>");
        append(parts, "ppt/slideLayouts/slideLayout1.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<p:sldLayout xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
            "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" type=\"blank\" "
            "preserve=\"1\"><p:cSld name=\"空白\"><p:spTree>" +
                fixed_group_tree() +
                "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>");
        append(parts, "ppt/slideLayouts/_rels/slideLayout1.xml.rels",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" "
            "Target=\"../slideMasters/slideMaster1.xml\"/>"
            "</Relationships>");
        append(parts, "ppt/theme/theme1.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Mirrorfly "
            "Office\"><a:themeElements><a:clrScheme name=\"Mirrorfly\"><a:dk1><a:sysClr val=\"windowText\" "
            "lastClr=\"000000\"/></a:dk1><a:lt1><a:sysClr val=\"window\" "
            "lastClr=\"FFFFFF\"/></a:lt1><a:dk2><a:srgbClr val=\"292724\"/></a:dk2><a:lt2><a:srgbClr "
            "val=\"F4F0E9\"/></a:lt2><a:accent1><a:srgbClr val=\"B77746\"/></a:accent1><a:accent2><a:srgbClr "
            "val=\"6F7D69\"/></a:accent2><a:accent3><a:srgbClr "
            "val=\"9A8978\"/></a:accent3><a:accent4><a:srgbClr "
            "val=\"C8A86B\"/></a:accent4><a:accent5><a:srgbClr "
            "val=\"6C7A89\"/></a:accent5><a:accent6><a:srgbClr "
            "val=\"A46B66\"/></a:accent6><a:hlink><a:srgbClr "
            "val=\"76543C\"/></a:hlink><a:folHlink><a:srgbClr "
            "val=\"795C70\"/></a:folHlink></a:clrScheme><a:fontScheme "
            "name=\"Mirrorfly\"><a:majorFont><a:latin typeface=\"Arial\"/><a:ea typeface=\"Microsoft "
            "YaHei\"/><a:cs typeface=\"Arial\"/></a:majorFont><a:minorFont><a:latin "
            "typeface=\"Arial\"/><a:ea typeface=\"Microsoft YaHei\"/><a:cs "
            "typeface=\"Arial\"/></a:minorFont></a:fontScheme><a:fmtScheme "
            "name=\"Mirrorfly\"><a:fillStyleLst><a:solidFill><a:schemeClr "
            "val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"><a:tint "
            "val=\"35000\"/></a:schemeClr></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"><a:shade "
            "val=\"85000\"/></a:schemeClr></a:solidFill></a:fillStyleLst><a:lnStyleLst><a:ln "
            "w=\"12700\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln><a:ln "
            "w=\"19050\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln><a:ln "
            "w=\"25400\"><a:solidFill><a:schemeClr "
            "val=\"phClr\"/></a:solidFill></a:ln></"
            "a:lnStyleLst><a:effectStyleLst><a:effectStyle><a:effectLst/></"
            "a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></"
            "a:effectStyle></a:effectStyleLst><a:bgFillStyleLst><a:solidFill><a:schemeClr "
            "val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"><a:tint "
            "val=\"95000\"/></a:schemeClr></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"><a:tint "
            "val=\"85000\"/></a:schemeClr></a:solidFill></a:bgFillStyleLst></a:fmtScheme></a:themeElements></"
            "a:theme>");
        const auto theme_template = parts.back().bytes;
        const auto part_bytes = [&parts](const std::string& path)
        {
            const auto found = std::find_if(parts.begin(), parts.end(), [&](const auto& part)
            {
                return part.path == path;
            });
            return found == parts.end() ? std::string{} : found->bytes;
        };
        const auto replace_target = [](std::string bytes, const std::string& old_name,
                                        const std::string& new_name)
        {
            const auto position = bytes.find(old_name);
            if (position != std::string::npos)
                bytes.replace(position, old_name.size(), new_name);
            return bytes;
        };
        for (std::size_t index = 0; index < palettes.size(); ++index)
        {
            const auto number = std::to_string(index + 1);
            const auto definition = authored_theme_definition(palettes[index]);
            pugi::xml_document theme_document;
            if (!theme_document.load_buffer(theme_template.data(), theme_template.size()))
            {
                return false;
            }
            const auto colors = theme_document.child("a:theme")
                                    .child("a:themeElements")
                                    .child("a:clrScheme");
            for (const char* slot : {"dk2", "lt1", "lt2", "accent2", "accent1", "accent3"})
            {
                auto entry = colors.child((std::string("a:") + slot).c_str());
                entry.remove_children();
                entry.append_child("a:srgbClr").append_attribute("val") =
                    definition.colors.at(slot).substr(1).c_str();
            }
            std::ostringstream theme_xml;
            theme_document.save(theme_xml, "", pugi::format_raw);
            if (index == 0)
            {
                parts.back().bytes = theme_xml.str();
                continue;
            }
            append(parts, "ppt/slideMasters/slideMaster" + number + ".xml",
                part_bytes("ppt/slideMasters/slideMaster1.xml"));
            auto master_rels = part_bytes("ppt/slideMasters/_rels/slideMaster1.xml.rels");
            master_rels = replace_target(master_rels, "slideLayout1.xml", "slideLayout" + number + ".xml");
            master_rels = replace_target(master_rels, "theme1.xml", "theme" + number + ".xml");
            append(parts, "ppt/slideMasters/_rels/slideMaster" + number + ".xml.rels",
                std::move(master_rels));
            append(parts, "ppt/slideLayouts/slideLayout" + number + ".xml",
                part_bytes("ppt/slideLayouts/slideLayout1.xml"));
            auto layout_rels = part_bytes("ppt/slideLayouts/_rels/slideLayout1.xml.rels");
            layout_rels = replace_target(layout_rels, "slideMaster1.xml", "slideMaster" + number + ".xml");
            append(parts, "ppt/slideLayouts/_rels/slideLayout" + number + ".xml.rels",
                std::move(layout_rels));
            append(parts, "ppt/theme/theme" + number + ".xml", theme_xml.str());
        }
        // clang-format on
        return true;
    }
}
