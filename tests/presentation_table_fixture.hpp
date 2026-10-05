#pragma once

#include <mirrorfly/presentation.hpp>

namespace mirrorfly::test_fixture
{
    inline std::vector<PresentationPart> table_package()
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.width = 240;
        scene.height = 160;
        auto package = serialize_presentation(scene);
        const auto cell =
            [](const std::string& attributes, const std::string& text, const std::string& properties)
        {
            return "<a:tc " + attributes +
                "><a:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr sz='1200'/><a:t>" + text +
                "</a:t></a:r></a:p></a:txBody><a:tcPr marL='25400' marR='25400' marT='12700' marB='12700' "
                "anchor='ctr'>" +
                properties + "</a:tcPr></a:tc>";
        };
        const std::string frame =
            "<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id='22' "
            "name='课程表格'/><p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr>"
            "<p:xfrm><a:off x='127000' y='254000'/><a:ext cx='2540000' cy='1524000'/></p:xfrm>"
            "<a:graphic><a:graphicData uri='http://schemas.openxmlformats.org/drawingml/2006/table'><a:tbl>"
            "<a:tblPr firstRow='1' bandRow='1'><a:tableStyleId>{TEST}</a:tableStyleId></a:tblPr>"
            "<a:tblGrid><a:gridCol w='1270000'/><a:gridCol w='1270000'/></a:tblGrid>"
            "<a:tr h='508000'>" +
            cell("gridSpan='2'", "合并表头", "") + cell("hMerge='1'", "", "") +
            "</a:tr>"
            "<a:tr h='508000'>" +
            cell("rowSpan='2'", "跨行", "<a:solidFill><a:srgbClr val='FF0000'/></a:solidFill>") +
            cell("", "B2", "") + "</a:tr><a:tr h='508000'>" + cell("vMerge='1'", "", "") +
            cell("", "B3", "") + "</a:tr></a:tbl></a:graphicData></a:graphic></p:graphicFrame>";
        for (auto& part : package.parts)
        {
            if (part.path == "ppt/slides/slide1.xml")
                part.bytes.insert(part.bytes.find("</p:spTree>"), frame);
            else if (part.path == "ppt/tableStyles.xml")
                part.bytes =
                    "<a:tblStyleLst xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main' "
                    "def='{TEST}'>"
                    "<a:tblStyle styleId='{TEST}' styleName='课程'><a:wholeTbl><a:tcTxStyle><a:srgbClr "
                    "val='112233'/></a:tcTxStyle>"
                    "<a:tcStyle><a:tcBdr><a:insideH><a:ln w='12700'><a:solidFill><a:srgbClr "
                    "val='000000'/></a:solidFill></a:ln></a:insideH>"
                    "<a:insideV><a:ln w='12700'><a:solidFill><a:srgbClr "
                    "val='000000'/></a:solidFill></a:ln></a:insideV></a:tcBdr>"
                    "<a:fill><a:solidFill><a:srgbClr "
                    "val='EFEFEF'/></a:solidFill></a:fill></a:tcStyle></a:wholeTbl>"
                    "<a:firstRow><a:tcTxStyle b='1'><a:srgbClr val='FFFFFF'/></a:tcTxStyle>"
                    "<a:tcStyle><a:fill><a:solidFill><a:srgbClr "
                    "val='2244AA'/></a:solidFill></a:fill></a:tcStyle></a:firstRow>"
                    "</a:tblStyle></a:tblStyleLst>";
        }
        return package.parts;
    }
}
