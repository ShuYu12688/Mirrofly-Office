#pragma once

#include <mirrorfly/presentation.hpp>

#include <algorithm>

namespace mirrorfly::test_fixture
{
    inline void graphic_frame(std::vector<PresentationPart>& parts, const std::string& contents,
        const std::string& relation, const std::string& uri)
    {
        for (auto& part : parts)
        {
            if (part.path == "ppt/slides/slide1.xml")
                part.bytes.insert(part.bytes.find("</p:spTree>"),
                    "<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id='22' name='课堂图形'/>"
                    "<p:cNvGraphicFramePr/><p:nvPr/></p:nvGraphicFramePr>"
                    "<p:xfrm><a:off x='127000' y='127000'/><a:ext cx='5080000' cy='3048000'/></p:xfrm>"
                    "<a:graphic><a:graphicData uri='" +
                        uri + "'>" + contents + "</a:graphicData></a:graphic></p:graphicFrame>");
            else if (part.path == "ppt/slides/_rels/slide1.xml.rels")
                part.bytes.insert(part.bytes.find("</Relationships>"), relation);
        }
    }

    inline std::vector<PresentationPart> chart_package(const std::string& kind = "barChart",
        const std::string& grouping = "clustered", bool second_series = true)
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.width = 420;
        scene.height = 260;
        auto package = serialize_presentation(scene);
        graphic_frame(package.parts,
            "<c:chart xmlns:c='http://schemas.openxmlformats.org/drawingml/2006/chart' r:id='chart1'/>",
            "<Relationship Id='chart1' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart' "
            "Target='../charts/chart1.xml'/>",
            "http://schemas.openxmlformats.org/drawingml/2006/chart");
        const auto series = [](int index, const std::string& color, const std::string& values)
        {
            return "<c:ser><c:idx val='" + std::to_string(index) + "'/><c:order val='" +
                std::to_string(index) +
                "'/><c:tx><c:v>课程数据</c:v></c:tx><c:spPr><a:solidFill><a:srgbClr val='" + color +
                "'/></a:solidFill></c:spPr><c:cat><c:strLit><c:ptCount val='3'/>"
                "<c:pt idx='0'><c:v>甲</c:v></c:pt><c:pt idx='1'><c:v>乙</c:v></c:pt>"
                "<c:pt idx='2'><c:v>丙</c:v></c:pt></c:strLit></c:cat>"
                "<c:val><c:numRef><c:f>Sheet1!B2:B4</c:f><c:numCache><c:formatCode>General</c:formatCode>"
                "<c:ptCount val='3'/>" +
                values +
                "</c:numCache></c:numRef></c:val>"
                "<c:xVal><c:numLit><c:pt idx='0'><c:v>10</c:v></c:pt><c:pt idx='1'><c:v>20</c:v></c:pt>"
                "<c:pt idx='2'><c:v>40</c:v></c:pt></c:numLit></c:xVal>"
                "<c:bubbleSize><c:numLit><c:pt idx='0'><c:v>2</c:v></c:pt><c:pt idx='1'><c:v>8</c:v></c:pt>"
                "<c:pt idx='2'><c:v>4</c:v></c:pt></c:numLit></c:bubbleSize></c:ser>";
        };
        const auto first = series(0, "2244AA",
            "<c:pt idx='0'><c:v>-2</c:v></c:pt><c:pt idx='1'><c:v>6</c:v></c:pt><c:pt "
            "idx='2'><c:v>4</c:v></c:pt>");
        const auto second = second_series
            ? series(1, "CC2200",
                  "<c:pt idx='0'><c:v>3</c:v></c:pt><c:pt idx='1'><c:v>2</c:v></c:pt><c:pt "
                  "idx='2'><c:v>1</c:v></c:pt>")
            : "";
        package.parts.push_back({"ppt/charts/chart1.xml",
            "<c:chartSpace xmlns:c='http://schemas.openxmlformats.org/drawingml/2006/chart' "
            "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main'><c:chart><c:plotArea>"
            "<c:layout><c:manualLayout><c:x val='0.15'/><c:y val='0.1'/><c:w val='0.75'/><c:h val='0.75'/>"
            "</c:manualLayout></c:layout><c:" +
                kind + "><c:barDir val='col'/><c:grouping val='" + grouping +
                "'/><c:holeSize val='50'/><c:scatterStyle val='lineMarker'/>" + first + second +
                "</c:" + kind +
                ">"
                "<c:catAx><c:axId val='1'/><c:scaling><c:orientation val='minMax'/></c:scaling><c:axPos "
                "val='b'/></c:catAx>"
                "<c:valAx><c:axId val='2'/><c:scaling><c:orientation val='minMax'/></c:scaling>"
                "<c:axPos val='l'/><c:majorGridlines/></c:valAx></c:plotArea></c:chart></c:chartSpace>"});
        return package.parts;
    }

    inline std::vector<PresentationPart> stock_chart_package()
    {
        auto package = chart_package();
        const auto series = [](int index, const std::string& name, const std::string& values)
        {
            return "<c:ser><c:idx val='" + std::to_string(index) + "'/><c:order val='" +
                std::to_string(index) + "'/><c:tx><c:strLit><c:pt idx='0'><c:v>" + name +
                "</c:v></c:pt></c:strLit></c:tx><c:cat><c:strLit>"
                "<c:pt idx='0'><c:v>周一</c:v></c:pt><c:pt idx='1'><c:v>周二</c:v></c:pt>"
                "<c:pt idx='2'><c:v>周三</c:v></c:pt></c:strLit></c:cat>"
                "<c:val><c:numLit><c:pt idx='0'><c:v>" +
                values.substr(0, values.find(',')) + "</c:v></c:pt><c:pt idx='1'><c:v>" +
                values.substr(values.find(',') + 1, values.rfind(',') - values.find(',') - 1) +
                "</c:v></c:pt><c:pt idx='2'><c:v>" + values.substr(values.rfind(',') + 1) +
                "</c:v></c:pt></c:numLit></c:val></c:ser>";
        };
        for (auto& part : package)
        {
            if (part.path == "ppt/charts/chart1.xml")
            {
                part.bytes =
                    "<c:chartSpace xmlns:c='http://schemas.openxmlformats.org/drawingml/2006/chart' "
                    "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main'><c:chart><c:plotArea>"
                    "<c:stockChart>" +
                    series(0, "开盘", "10,12,11") + series(1, "最高", "15,17,16") +
                    series(2, "最低", "8,9,7") + series(3, "收盘", "14,10,13") +
                    "<c:hiLowLines><c:spPr><a:ln><a:solidFill><a:srgbClr val='2244AA'/>"
                    "</a:solidFill></a:ln></c:spPr></c:hiLowLines><c:upDownBars><c:gapWidth val='100'/>"
                    "<c:upBars><c:spPr><a:solidFill><a:srgbClr val='FFFFFF'/></a:solidFill></c:spPr>"
                    "</c:upBars><c:downBars><c:spPr><a:solidFill><a:srgbClr val='CC2200'/>"
                    "</a:solidFill></c:spPr></c:downBars></c:upDownBars>"
                    "<c:axId val='1'/><c:axId val='2'/></c:stockChart>"
                    "<c:catAx><c:axId val='1'/><c:scaling><c:orientation val='minMax'/></c:scaling>"
                    "<c:axPos val='b'/></c:catAx><c:valAx><c:axId val='2'/><c:scaling>"
                    "<c:orientation val='minMax'/></c:scaling><c:axPos val='l'/><c:majorGridlines/>"
                    "</c:valAx></c:plotArea></c:chart></c:chartSpace>";
            }
        }
        return package;
    }

    inline std::vector<PresentationPart> volume_stock_chart_package(bool include_open = true)
    {
        auto package = stock_chart_package();
        for (auto& part : package)
        {
            if (part.path != "ppt/charts/chart1.xml")
            {
                continue;
            }
            const std::string volume =
                "<c:barChart><c:barDir val='col'/><c:grouping val='clustered'/><c:ser>"
                "<c:idx val='10'/><c:order val='10'/><c:tx><c:v>成交量</c:v></c:tx>"
                "<c:spPr><a:solidFill><a:srgbClr val='70AD47'/></a:solidFill></c:spPr>"
                "<c:cat><c:strLit><c:pt idx='0'><c:v>周一</c:v></c:pt>"
                "<c:pt idx='1'><c:v>周二</c:v></c:pt><c:pt idx='2'><c:v>周三</c:v></c:pt>"
                "</c:strLit></c:cat><c:val><c:numLit><c:pt idx='0'><c:v>1200</c:v></c:pt>"
                "<c:pt idx='1'><c:v>1800</c:v></c:pt><c:pt idx='2'><c:v>900</c:v></c:pt>"
                "</c:numLit></c:val></c:ser><c:axId val='3'/><c:axId val='1'/></c:barChart>";
            const auto stock_start = part.bytes.find("<c:stockChart>");
            part.bytes.insert(stock_start, volume);
            if (!include_open)
            {
                const auto adjusted_stock_start = part.bytes.find("<c:stockChart>");
                const auto series_start = part.bytes.find("<c:ser>", adjusted_stock_start);
                const auto series_end = part.bytes.find("</c:ser>", series_start);
                part.bytes.erase(series_start, series_end + std::string("</c:ser>").size() - series_start);
            }
        }
        return package;
    }

    inline std::vector<PresentationPart> chart_ex_package(
        const std::string& requested_layout, bool fallback_image = false)
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.width = 420;
        scene.height = 260;
        auto package = serialize_presentation(scene);
        graphic_frame(package.parts,
            "<cx:chart xmlns:cx='http://schemas.microsoft.com/office/drawing/2014/chartex' "
            "r:id='chartEx1'/>",
            "<Relationship Id='chartEx1' "
            "Type='http://schemas.microsoft.com/office/2014/relationships/chartEx' "
            "Target='../charts/chartEx1.xml'/>",
            "http://schemas.microsoft.com/office/drawing/2014/chartex");
        for (auto& part : package.parts)
        {
            if (part.path == "[Content_Types].xml")
            {
                part.bytes.insert(part.bytes.find("</Types>"),
                    "<Override PartName='/ppt/charts/chartEx1.xml' "
                    "ContentType='application/vnd.ms-office.chartex+xml'/>");
            }
        }
        const bool histogram = requested_layout == "histogram";
        const bool pareto = requested_layout == "paretoLine";
        const std::string layout = histogram || pareto ? "clusteredColumn" : requested_layout;
        const std::string layout_properties = layout == "waterfall"
            ? "<cx:layoutPr><cx:subtotals><cx:idx val='2'/></cx:subtotals></cx:layoutPr>"
            : (histogram ? "<cx:layoutPr><cx:binning><cx:binCount>3</cx:binCount></cx:binning></cx:layoutPr>"
                         : "");
        const std::string box_levels = layout == "boxWhisker"
            ? "<cx:lvl ptCount='4' name='甲组'><cx:pt idx='0'>1</cx:pt><cx:pt idx='1'>2</cx:pt>"
              "<cx:pt idx='2'>8</cx:pt><cx:pt idx='3'>9</cx:pt></cx:lvl>"
              "<cx:lvl ptCount='4' name='乙组'><cx:pt idx='0'>3</cx:pt><cx:pt idx='1'>4</cx:pt>"
              "<cx:pt idx='2'>5</cx:pt><cx:pt idx='3'>7</cx:pt></cx:lvl>"
            : "<cx:lvl ptCount='4' name='课程数据'><cx:pt idx='0'>4</cx:pt><cx:pt idx='1'>8</cx:pt>"
              "<cx:pt idx='2'>3</cx:pt><cx:pt idx='3'>6</cx:pt></cx:lvl>";
        std::string series = "<cx:series layoutId='" + layout +
            "'><cx:tx><cx:v>课程数据</cx:v></cx:tx><cx:dataId val='1'/>" + layout_properties +
            "<cx:spPr><a:solidFill><a:srgbClr val='2244AA'/></a:solidFill></cx:spPr>"
            "<cx:dataPt idx='1'><cx:spPr><a:solidFill><a:srgbClr val='CC2200'/></a:solidFill>"
            "</cx:spPr></cx:dataPt></cx:series>";
        if (pareto)
        {
            series += "<cx:series layoutId='paretoLine'><cx:dataId val='1'/></cx:series>";
        }
        const std::string fallback_attribute = fallback_image ? " fallbackImg='fallback1'" : "";
        package.parts.push_back({"ppt/charts/chartEx1.xml",
            "<cx:chartSpace xmlns:cx='http://schemas.microsoft.com/office/drawing/2014/chartex' "
            "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main' "
            "xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'" +
                fallback_attribute +
                "><cx:chartData><cx:data id='1'><cx:strDim type='cat'><cx:lvl ptCount='4'>"
                "<cx:pt idx='0'>甲</cx:pt><cx:pt idx='1'>乙</cx:pt><cx:pt idx='2'>丙</cx:pt>"
                "<cx:pt idx='3'>丁</cx:pt></cx:lvl></cx:strDim><cx:numDim type='val'>" +
                box_levels +
                "</cx:numDim></cx:data></cx:chartData><cx:chart><cx:title><cx:tx><cx:rich>"
                "<a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr sz='1600'/><a:t>扩展图表</a:t></a:r>"
                "</a:p></cx:rich></cx:tx></cx:title><cx:plotArea><cx:plotAreaRegion>" +
                series + "</cx:plotAreaRegion></cx:plotArea></cx:chart></cx:chartSpace>"});
        if (fallback_image)
        {
            package.parts.push_back({"ppt/charts/_rels/chartEx1.xml.rels",
                "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
                "<Relationship Id='fallback1' "
                "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/image' "
                "Target='../media/chart-fallback.png'/></Relationships>"});
            package.parts.push_back({"ppt/media/chart-fallback.png", "chart-fallback-image"});
        }
        return package.parts;
    }

    inline std::vector<PresentationPart> diagram_package()
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.width = 420;
        scene.height = 260;
        auto package = serialize_presentation(scene);
        graphic_frame(package.parts,
            "<dgm:relIds xmlns:dgm='http://schemas.openxmlformats.org/drawingml/2006/diagram' "
            "r:dm='diagram1'/>",
            "<Relationship Id='diagram1' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/diagramData' "
            "Target='../diagrams/data1.xml'/>",
            "http://schemas.openxmlformats.org/drawingml/2006/diagram");
        package.parts.push_back({"ppt/diagrams/data1.xml",
            "<dgm:dataModel xmlns:dgm='http://schemas.openxmlformats.org/drawingml/2006/diagram'/>"});
        package.parts.push_back({"ppt/diagrams/_rels/data1.xml.rels",
            "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
            "<Relationship Id='cache1' "
            "Type='http://schemas.microsoft.com/office/2007/relationships/diagramDrawing' "
            "Target='drawing1.xml'/></Relationships>"});
        package.parts.push_back({"ppt/diagrams/drawing1.xml",
            "<dsp:drawing xmlns:dsp='http://schemas.microsoft.com/office/drawing/2008/diagram' "
            "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main'><dsp:spTree>"
            "<dsp:nvGrpSpPr/><dsp:grpSpPr><a:xfrm><a:chOff x='0' y='0'/><a:chExt cx='2540000' cy='1524000'/>"
            "</a:xfrm></dsp:grpSpPr><dsp:sp><dsp:nvSpPr><dsp:cNvPr id='2' name='节点'/></dsp:nvSpPr>"
            "<dsp:spPr><a:xfrm><a:off x='127000' y='127000'/><a:ext cx='635000' cy='254000'/></a:xfrm>"
            "<a:prstGeom prst='roundRect'><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val='2244AA'/>"
            "</a:solidFill></dsp:spPr><dsp:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr sz='1200'/>"
            "<a:t>认识图形</a:t></a:r></a:p></dsp:txBody></dsp:sp></dsp:spTree></dsp:drawing>"});
        return package.parts;
    }

    inline std::vector<PresentationPart> diagram_data_package(bool include_drawing_cache = false)
    {
        auto package = diagram_package();
        for (auto& part : package)
        {
            if (part.path == "ppt/diagrams/data1.xml")
            {
                part.bytes =
                    "<dgm:dataModel xmlns:dgm='http://schemas.openxmlformats.org/drawingml/2006/diagram' "
                    "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main'>"
                    "<dgm:ptLst><dgm:pt modelId='doc' type='doc'/><dgm:pt modelId='root'>"
                    "<dgm:t><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr sz='1600'/><a:t>课程主题</a:t>"
                    "</a:r></a:p></dgm:t></dgm:pt><dgm:pt modelId='left'><dgm:t><a:bodyPr/>"
                    "<a:lstStyle/><a:p><a:r><a:rPr sz='1400'/><a:t>概念</a:t></a:r></a:p></dgm:t>"
                    "</dgm:pt><dgm:pt modelId='right'><dgm:t><a:bodyPr/><a:lstStyle/><a:p><a:r>"
                    "<a:rPr sz='1400'/><a:t>练习</a:t></a:r></a:p></dgm:t></dgm:pt></dgm:ptLst>"
                    "<dgm:cxnLst><dgm:cxn modelId='c0' type='parOf' srcId='doc' destId='root' "
                    "srcOrd='0' destOrd='0'/><dgm:cxn modelId='c1' type='parOf' srcId='root' "
                    "destId='left' srcOrd='0' destOrd='0'/><dgm:cxn modelId='c2' type='parOf' "
                    "srcId='root' destId='right' srcOrd='1' destOrd='0'/></dgm:cxnLst>"
                    "<dgm:bg/><dgm:whole/></dgm:dataModel>";
            }
        }
        if (!include_drawing_cache)
        {
            package.erase(std::remove_if(package.begin(), package.end(),
                              [](const auto& part)
            {
                return part.path == "ppt/diagrams/drawing1.xml";
            }),
                package.end());
        }
        return package;
    }
}
