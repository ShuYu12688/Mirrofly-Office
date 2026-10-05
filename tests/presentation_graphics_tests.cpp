#include "presentation_graphics_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;
    void check(bool condition, const char* label)
    {
        if (!condition)
        {
            std::cerr << label << '\n';
            ++failures;
        }
    }

    void preserved(
        const std::vector<mirrorfly::PresentationPart>& original, mirrorfly::PresentationScene scene)
    {
        scene.native_editable = true;
        mirrorfly::PresentationEditCommand command;
        command.action = mirrorfly::PresentationEditAction::UpdateText;
        command.text = "不可修改";
        check(mirrorfly::apply_presentation_edit(scene, command).error !=
                mirrorfly::PresentationEditError::None,
            "cached content rejects shape editing");
        const auto saved = mirrorfly::serialize_presentation(scene);
        check(saved.error == mirrorfly::PresentationError::None, "save cached graphics");
        for (const auto& part : original)
        {
            const auto found = std::find_if(saved.parts.begin(), saved.parts.end(), [&](const auto& output)
            {
                return output.path == part.path;
            });
            check(found != saved.parts.end() && found->bytes == part.bytes,
                "cached graphics source bytes preserved");
        }
    }

    int run_graphics_tests()
    {
        using namespace mirrorfly;
        const auto diagram = test_fixture::diagram_package();
        auto parsed = parse_presentation(diagram);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() == 1,
            "SmartArt cached drawing parsed");
        if (parsed.error == PresentationError::None && !parsed.scene.slides[0].shapes.empty())
        {
            const auto& group = parsed.scene.slides[0].groups.front();
            check(group.source_id == "22" && group.width == 400 && group.height == 240 &&
                    group.transform[4] == 10 && group.transform[5] == 10,
                "SmartArt keeps original frame for group animations");
            const auto& shape = parsed.scene.slides[0].shapes[0];
            check(shape.width == 100 && shape.height == 40 && shape.transform[0] == 1 &&
                    shape.transform[3] == 1 && shape.transform[4] == 30 && shape.transform[5] == 30,
                "SmartArt cache coordinates scale into graphic frame");
            check(shape.fill.color == "#2244AA" && shape.geometry == "roundRect" &&
                    shape.text.paragraphs[0].runs[0].text == "认识图形",
                "SmartArt uses shared shape geometry and text parser");
            check(!shape.editable && shape.source_id == "22:diagram:2" && shape.source_groups[0] == "22",
                "SmartArt read only and slide animation identifiers isolated");
            preserved(diagram, parsed.scene);
        }
        for (const auto* kind : {"barChart", "lineChart", "areaChart", "pieChart", "doughnutChart",
                 "scatterChart", "bubbleChart", "radarChart", "bar3DChart", "line3DChart", "area3DChart",
                 "pie3DChart", "surfaceChart", "surface3DChart", "ofPieChart"})
        {
            const auto original = test_fixture::chart_package(kind);
            parsed = parse_presentation(original);
            check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() > 3,
                "chart family produces rendered geometry");
            if (parsed.error != PresentationError::None)
                continue;
            const auto& shapes = parsed.scene.slides[0].shapes;
            check(std::all_of(shapes.begin(), shapes.end(),
                      [](const auto& shape)
            {
                return !shape.editable && !shape.source_groups.empty() && shape.source_groups[0] == "22" &&
                    std::isfinite(shape.width) && std::isfinite(shape.height);
            }),
                "chart geometry bounded and locked to original frame");
            preserved(original, parsed.scene);
        }
        const auto stock = test_fixture::stock_chart_package();
        parsed = parse_presentation(stock);
        std::size_t stock_lines = 0;
        bool stock_down_bar = false;
        if (parsed.error == PresentationError::None && !parsed.scene.slides.empty())
        {
            for (const auto& shape : parsed.scene.slides[0].shapes)
            {
                if (shape.path_geometry && shape.outline_color == "#2244AA")
                {
                    ++stock_lines;
                }
                stock_down_bar = stock_down_bar || shape.fill.color == "#CC2200";
            }
        }
        check(parsed.error == PresentationError::None && stock_lines >= 9 && stock_down_bar,
            "stock chart produces high-low lines, open-close ticks and down bars");
        if (parsed.error == PresentationError::None)
        {
            preserved(stock, parsed.scene);
        }
        for (const bool include_open : {false, true})
        {
            const auto volume_stock = test_fixture::volume_stock_chart_package(include_open);
            parsed = parse_presentation(volume_stock);
            bool volume_bars = false;
            std::size_t price_lines = 0;
            if (parsed.error == PresentationError::None && !parsed.scene.slides.empty())
            {
                for (const auto& shape : parsed.scene.slides[0].shapes)
                {
                    volume_bars = volume_bars ||
                        (shape.fill.color == "#70AD47" && std::abs(shape.fill.opacity - 0.38) < 0.001);
                    if (shape.path_geometry && shape.outline_color == "#2244AA")
                    {
                        ++price_lines;
                    }
                }
            }
            check(parsed.error == PresentationError::None && volume_bars &&
                    price_lines >= (include_open ? 9 : 6),
                "volume stock combination keeps volume columns separate from price series");
            if (parsed.error == PresentationError::None)
            {
                check(std::any_of(parsed.scene.slides[0].warnings.begin(),
                          parsed.scene.slides[0].warnings.end(),
                          [](const auto& warning)
                {
                    return warning.find("成交量") != std::string::npos;
                }),
                    "volume stock combination reports the independent scale approximation");
                preserved(volume_stock, parsed.scene);
            }
        }
        for (const auto* layout : {"waterfall", "funnel", "treemap", "sunburst", "boxWhisker", "histogram",
                 "paretoLine", "regionMap"})
        {
            const auto original = test_fixture::chart_ex_package(layout);
            parsed = parse_presentation(original);
            check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() > 3,
                "ChartEx family produces rendered geometry");
            if (parsed.error != PresentationError::None)
            {
                continue;
            }
            const auto& shapes = parsed.scene.slides[0].shapes;
            check(std::all_of(shapes.begin(), shapes.end(),
                      [](const auto& shape)
            {
                return !shape.editable && !shape.source_groups.empty() && shape.source_groups[0] == "22" &&
                    std::isfinite(shape.width) && std::isfinite(shape.height);
            }),
                "ChartEx geometry remains bounded and locked to the source frame");
            check(std::any_of(parsed.scene.slides[0].warnings.begin(), parsed.scene.slides[0].warnings.end(),
                      [](const auto& warning)
            {
                return warning.find("扩展图表") != std::string::npos;
            }),
                "ChartEx reports its read-only approximation");
            preserved(original, parsed.scene);
        }
        const auto chart_ex_fallback = test_fixture::chart_ex_package("waterfall", true);
        parsed = parse_presentation(chart_ex_fallback);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() == 1 &&
                parsed.scene.slides[0].shapes[0].image_path == "ppt/media/chart-fallback.png" &&
                parsed.scene.images.size() == 1,
            "ChartEx package fallback image takes priority over generated geometry");
        if (parsed.error == PresentationError::None)
        {
            check(!parsed.scene.slides[0].shapes[0].editable &&
                    parsed.scene.slides[0].shapes[0].source_groups[0] == "22" &&
                    std::any_of(parsed.scene.slides[0].warnings.begin(),
                        parsed.scene.slides[0].warnings.end(),
                        [](const auto& warning)
            {
                return warning.find("回退图") != std::string::npos;
            }),
                "ChartEx fallback image remains locked and reports the selected path");
            preserved(chart_ex_fallback, parsed.scene);
        }
        auto missing_chart_ex_data = test_fixture::chart_ex_package("waterfall");
        for (auto& part : missing_chart_ex_data)
        {
            if (part.path == "ppt/charts/chartEx1.xml")
            {
                const auto position = part.bytes.find("<cx:dataId val='1'/>");
                part.bytes.replace(position, 20, "<cx:dataId val='99'/>");
            }
        }
        parsed = parse_presentation(missing_chart_ex_data);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.empty() &&
                !parsed.scene.slides[0].warnings.empty(),
            "ChartEx missing data reference remains nonfatal and preserves the source package");
        auto radar = test_fixture::chart_package("radarChart");
        for (auto& part : radar)
        {
            if (part.path == "ppt/charts/chart1.xml")
            {
                const auto position = part.bytes.find("<c:barDir");
                part.bytes.insert(position, "<c:radarStyle val='filled'/>");
            }
        }
        parsed = parse_presentation(radar);
        bool filled_radar = false;
        if (parsed.error == PresentationError::None && !parsed.scene.slides.empty())
        {
            filled_radar = std::any_of(parsed.scene.slides[0].shapes.begin(),
                parsed.scene.slides[0].shapes.end(), [](const auto& shape)
            {
                return shape.path_geometry && shape.fill.color == "#2244AA" && shape.fill.opacity == 0.35;
            });
        }
        check(parsed.error == PresentationError::None && filled_radar,
            "filled radar chart produces a bounded translucent data polygon");
        if (parsed.error == PresentationError::None)
            preserved(radar, parsed.scene);
        for (const auto* grouping : {"clustered", "stacked", "percentStacked"})
        {
            parsed = parse_presentation(test_fixture::chart_package("barChart", grouping));
            const auto& shapes = parsed.scene.slides[0].shapes;
            const auto blue = std::find_if(shapes.begin(), shapes.end(), [](const auto& shape)
            {
                return shape.fill.color == "#2244AA";
            });
            check(blue != shapes.end() && blue->height > 0, "negative bar has positive rectangle height");
            if (blue != shapes.end())
            {
                const double baseline = std::string(grouping) == "clustered"
                    ? 169
                    : (std::string(grouping) == "stacked" ? 178 : 124);
                check(std::abs(blue->transform[5] - baseline) < 0.001,
                    "negative bar starts at correct zero position for grouping");
            }
        }
        auto missing_cache = test_fixture::diagram_package();
        missing_cache.erase(std::remove_if(missing_cache.begin(), missing_cache.end(),
                                [](const auto& part)
        {
            return part.path == "ppt/diagrams/drawing1.xml";
        }),
            missing_cache.end());
        parsed = parse_presentation(missing_cache);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.empty() &&
                !parsed.scene.slides[0].warnings.empty(),
            "missing SmartArt cache retains package and reports limitation");

        const auto data_only = test_fixture::diagram_data_package();
        parsed = parse_presentation(data_only);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() == 5 &&
                parsed.scene.slides[0].groups.size() == 1,
            "SmartArt data model produces nodes and connectors without a drawing cache");
        if (parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() == 5)
        {
            const auto& shapes = parsed.scene.slides[0].shapes;
            check(shapes[2].text.paragraphs[0].runs[0].text == "课程主题" &&
                    shapes[3].text.paragraphs[0].runs[0].text == "概念" &&
                    shapes[4].text.paragraphs[0].runs[0].text == "练习",
                "SmartArt fallback keeps data-node text and hierarchy order");
            check(std::all_of(shapes.begin(), shapes.end(),
                      [](const auto& shape)
            {
                return !shape.editable && !shape.source_groups.empty() && shape.source_groups[0] == "22";
            }),
                "SmartArt fallback shapes remain locked to the source frame");
            preserved(data_only, parsed.scene);
        }

        const auto cached_data = test_fixture::diagram_data_package(true);
        parsed = parse_presentation(cached_data);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() == 1 &&
                parsed.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text == "认识图形",
            "SmartArt drawing cache takes priority over the data-model fallback");

        auto limited_data = test_fixture::diagram_data_package();
        for (auto& part : limited_data)
        {
            if (part.path != "ppt/diagrams/data1.xml")
            {
                continue;
            }
            std::string points;
            for (int index = 0; index < 66; ++index)
            {
                points += "<dgm:pt modelId='node" + std::to_string(index) +
                    "'><dgm:t><a:bodyPr/>"
                    "<a:lstStyle/><a:p><a:r><a:t>节点</a:t></a:r></a:p></dgm:t></dgm:pt>";
            }
            part.bytes =
                "<dgm:dataModel xmlns:dgm='http://schemas.openxmlformats.org/drawingml/2006/diagram' "
                "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main'><dgm:ptLst>" +
                points + "</dgm:ptLst><dgm:cxnLst/><dgm:bg/><dgm:whole/></dgm:dataModel>";
        }
        parsed = parse_presentation(limited_data);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() == 64 &&
                std::any_of(parsed.scene.slides[0].warnings.begin(), parsed.scene.slides[0].warnings.end(),
                    [](const auto& warning)
        {
            return warning.find("64") != std::string::npos;
        }),
            "SmartArt fallback caps excessive data nodes and reports the limit");

        auto cyclic_data = test_fixture::diagram_data_package();
        for (auto& part : cyclic_data)
        {
            if (part.path == "ppt/diagrams/data1.xml")
            {
                const auto position = part.bytes.find("</dgm:cxnLst>");
                part.bytes.insert(position,
                    "<dgm:cxn modelId='cycle' type='parOf' srcId='left' destId='root' "
                    "srcOrd='0' destOrd='0'/>");
            }
        }
        parsed = parse_presentation(cyclic_data);
        check(parsed.error == PresentationError::None && parsed.scene.slides[0].shapes.size() == 3 &&
                std::any_of(parsed.scene.slides[0].warnings.begin(), parsed.scene.slides[0].warnings.end(),
                    [](const auto& warning)
        {
            return warning.find("循环") != std::string::npos;
        }),
            "cyclic SmartArt relationships flatten safely and report the approximation");
        return failures ? 1 : 0;
    }
}

int main()
{
    return run_graphics_tests();
}
