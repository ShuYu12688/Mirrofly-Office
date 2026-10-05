#include "presentation_chart.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

namespace
{
    using Node = pugi::xml_node;
    using Shape = mirrorfly::PresentationShape;
    using Point = std::array<double, 2>;
    constexpr double pi = 3.14159265358979323846;
    constexpr double missing = std::numeric_limits<double>::quiet_NaN();

    std::string local(const char* name)
    {
        const auto colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    Node child(Node node, const std::string& name)
    {
        for (auto item : node.children())
            if (local(item.name()) == name)
                return item;
        return {};
    }

    double number(const char* text, double fallback = 0)
    {
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        return end != text && *end == 0 && std::isfinite(value) && std::abs(value) < 1e100 ? value : fallback;
    }

    double value(Node node, const std::string& name, double fallback = 0)
    {
        return number(child(node, name).attribute("val").value(), fallback);
    }

    std::string word(Node node, const std::string& name, const std::string& fallback = {})
    {
        const auto attribute = child(node, name).attribute("val");
        return attribute ? attribute.value() : fallback;
    }

    void warning(std::vector<std::string>& warnings, const std::string& text)
    {
        if (warnings.size() < 80 && std::find(warnings.begin(), warnings.end(), text) == warnings.end())
            warnings.push_back(text);
    }

    Node cache(Node node)
    {
        for (auto entry : node.children())
        {
            const auto name = local(entry.name());
            if (name == "numLit" || name == "strLit")
                return entry;
            if (name == "numRef")
                return child(entry, "numCache");
            if (name == "strRef")
                return child(entry, "strCache");
            if (name == "multiLvlStrRef")
                return child(child(entry, "multiLvlStrCache"), "lvl");
        }
        return {};
    }

    std::vector<std::string> strings(Node node)
    {
        std::vector<std::string> result;
        for (auto point : cache(node).children())
        {
            if (local(point.name()) != "pt")
                continue;
            const int index = point.attribute("idx").as_int(-1);
            if (index < 0 || index >= 512)
                continue;
            if (result.size() <= static_cast<std::size_t>(index))
                result.resize(static_cast<std::size_t>(index) + 1);
            result[index] = child(point, "v").text().as_string();
        }
        return result;
    }

    std::vector<double> numbers(Node node)
    {
        const auto source = strings(node);
        std::vector<double> result;
        result.reserve(source.size());
        for (const auto& text : source)
            result.push_back(number(text.c_str(), missing));
        return result;
    }

    std::vector<std::string> extended_strings(Node level)
    {
        std::vector<std::string> result;
        for (const auto point : level.children())
        {
            if (local(point.name()) != "pt")
            {
                continue;
            }
            const int index = point.attribute("idx").as_int(-1);
            if (index < 0 || index >= 4096)
            {
                continue;
            }
            if (result.size() <= static_cast<std::size_t>(index))
            {
                result.resize(static_cast<std::size_t>(index) + 1);
            }
            result[index] = point.text().as_string();
        }
        return result;
    }

    std::vector<double> extended_numbers(Node level)
    {
        const auto source = extended_strings(level);
        std::vector<double> result;
        result.reserve(source.size());
        for (const auto& text : source)
        {
            result.push_back(number(text.c_str(), missing));
        }
        return result;
    }

    Node fill_node(Node properties)
    {
        for (auto entry : properties.children())
        {
            const auto name = local(entry.name());
            if (name == "solidFill" || name == "gradFill" || name == "noFill" || name == "pattFill")
                return entry;
        }
        return {};
    }

    struct Series
    {
        Node source;
        Node chart;
        std::string kind;
        std::string name;
        std::vector<std::string> categories;
        std::vector<double> values;
        std::vector<double> x;
        std::vector<double> sizes;
        mirrorfly::PresentationFill fill;
    };

    struct ExtendedData
    {
        std::vector<std::vector<std::string>> category_levels;
        std::map<std::string, std::vector<std::vector<double>>> number_levels;
        std::map<std::string, std::vector<std::string>> number_level_names;
    };

    struct ExtendedSeries
    {
        Node source;
        std::string layout;
        std::string name;
        ExtendedData data;
        mirrorfly::PresentationFill fill;
        std::set<std::size_t> subtotals;
    };

    struct Canvas
    {
        const Shape& frame;
        const mirrorfly::PresentationChartReader& reader;
        std::vector<Shape> shapes;
        mirrorfly::PresentationText style;

        Shape base(double x = 0, double y = 0, double width = 0, double height = 0)
        {
            Shape shape;
            shape.editable = false;
            shape.source_part = frame.source_part;
            shape.source_id = frame.source_id + ":chart:" + std::to_string(shapes.size());
            shape.source_groups = {frame.source_id};
            shape.transform = frame.transform;
            shape.transform[4] += frame.transform[0] * x + frame.transform[2] * y;
            shape.transform[5] += frame.transform[1] * x + frame.transform[3] * y;
            shape.width = width;
            shape.height = height;
            return shape;
        }

        void rectangle(
            double x, double y, double width, double height, const mirrorfly::PresentationFill& fill)
        {
            if (width <= 0 || height <= 0)
                return;
            auto shape = base(x, y, width, height);
            shape.fill = fill;
            shapes.push_back(std::move(shape));
        }

        void path(const std::vector<Point>& points, const mirrorfly::PresentationFill& fill,
            const std::string& outline = {}, double stroke = 1, bool close = false)
        {
            if (points.empty())
                return;
            auto shape = base(0, 0, frame.width, frame.height);
            shape.fill = fill;
            shape.outline_color = outline;
            shape.outline_width = stroke;
            mirrorfly::PresentationGeometry geometry;
            geometry.width = frame.width;
            geometry.height = frame.height;
            mirrorfly::PresentationPath path;
            path.width = frame.width;
            path.height = frame.height;
            for (std::size_t index = 0; index < points.size(); ++index)
                path.commands.push_back({index == 0 ? mirrorfly::PresentationPathAction::Move
                                                    : mirrorfly::PresentationPathAction::Line,
                    {points[index][0], points[index][1]}});
            if (close)
                path.commands.push_back({mirrorfly::PresentationPathAction::Close, {}});
            geometry.paths.push_back(std::move(path));
            shape.path_geometry =
                std::make_shared<const mirrorfly::PresentationGeometry>(std::move(geometry));
            shapes.push_back(std::move(shape));
        }

        void label(double x, double y, double width, double height, const std::string& text,
            const std::string& align = "center", double size = 10, bool bold = false, Node source = {})
        {
            if (text.empty() && !source)
                return;
            auto shape = base(x, y, width, height);
            shape.text = source ? reader.text(source) : style;
            if (shape.text.paragraphs.empty())
                shape.text.paragraphs.emplace_back();
            auto& paragraph = shape.text.paragraphs.front();
            if (paragraph.runs.empty())
                paragraph.runs.emplace_back();
            if (!source)
            {
                auto run = paragraph.runs.front();
                run.text = text;
                run.font_size = size;
                run.bold = bold;
                paragraph.runs = {run};
                shape.text.paragraphs.resize(1);
            }
            for (auto& item : shape.text.paragraphs)
                item.alignment = align;
            shape.text.inset_left = shape.text.inset_right = 0;
            shape.text.inset_top = shape.text.inset_bottom = 0;
            shape.text.vertical_alignment = "center";
            shape.text.wrap = true;
            shapes.push_back(std::move(shape));
        }
    };

    std::string display_number(double value, bool percent = false)
    {
        std::ostringstream text;
        const double shown = percent ? value * 100 : value;
        if (shown != 0 && (std::abs(shown) >= 1e7 || std::abs(shown) < 0.001))
            text << std::scientific << std::setprecision(2) << shown;
        else
            text << std::setprecision(5) << shown;
        if (percent)
            text << '%';
        return text.str();
    }

    mirrorfly::PresentationFill point_fill(
        const Series& series, std::size_t index, const Canvas& canvas, bool varying)
    {
        auto fill = series.fill;
        if (varying && !canvas.reader.colors.empty())
            fill.color = canvas.reader.colors[index % canvas.reader.colors.size()];
        for (auto point : series.source.children())
            if (local(point.name()) == "dPt" && value(point, "idx", -1) == static_cast<double>(index))
                if (const auto specified = fill_node(child(point, "spPr")))
                    fill = canvas.reader.fill(specified);
        return fill;
    }

    const std::vector<double>& extended_values(const ExtendedData& data, const std::string& type = "val")
    {
        static const std::vector<double> empty;
        const auto found = data.number_levels.find(type);
        if (found == data.number_levels.end() || found->second.empty())
        {
            return empty;
        }
        return *std::max_element(found->second.begin(), found->second.end(),
            [](const auto& left, const auto& right)
        {
            return left.size() < right.size();
        });
    }

    const std::vector<std::string>& extended_categories(const ExtendedData& data)
    {
        static const std::vector<std::string> empty;
        if (data.category_levels.empty())
        {
            return empty;
        }
        return *std::max_element(data.category_levels.begin(), data.category_levels.end(),
            [](const auto& left, const auto& right)
        {
            return left.size() < right.size();
        });
    }

    std::string descendant_text(Node root)
    {
        std::string result;
        std::vector<Node> nodes{root};
        for (std::size_t index = 0; index < nodes.size() && nodes.size() < 512; ++index)
        {
            const auto node = nodes[index];
            const auto name = local(node.name());
            if (name == "t" || name == "v")
            {
                result += node.text().as_string();
            }
            for (const auto item : node.children())
            {
                if (item.type() == pugi::node_element)
                {
                    nodes.push_back(item);
                }
            }
        }
        return result;
    }

    mirrorfly::PresentationFill extended_point_fill(
        const ExtendedSeries& series, std::size_t index, const Canvas& canvas, bool varying = true)
    {
        auto fill = series.fill;
        if (varying && !canvas.reader.colors.empty())
        {
            fill.color = canvas.reader.colors[index % canvas.reader.colors.size()];
        }
        for (const auto point : series.source.children())
        {
            if (local(point.name()) != "dataPt" || point.attribute("idx").as_ullong() != index)
            {
                continue;
            }
            if (const auto specified = fill_node(child(point, "spPr")))
            {
                fill = canvas.reader.fill(specified);
            }
        }
        return fill;
    }

    void pie(Canvas& canvas, const Series& series, double left, double top, double width, double height)
    {
        double total = 0;
        for (const auto item : series.values)
            if (std::isfinite(item))
                total += std::abs(item);
        if (total <= 0 || !std::isfinite(total))
            return;
        const bool doughnut = series.kind == "doughnutChart";
        const double hole = doughnut ? std::clamp(value(series.chart, "holeSize", 50), 10.0, 90.0) / 100 : 0;
        const double radius = std::max(1.0, std::min(width, height) / 2 - 10);
        const double cx = left + width / 2, cy = top + height / 2;
        double angle = (value(series.chart, "firstSliceAng") - 90) * pi / 180;
        const auto labels =
            child(series.source, "dLbls") ? child(series.source, "dLbls") : child(series.chart, "dLbls");
        for (std::size_t index = 0; index < series.values.size(); ++index)
        {
            const double item = series.values[index];
            if (!std::isfinite(item) || item == 0)
                continue;
            const double sweep = std::abs(item) / total * 2 * pi;
            const int segments = std::max(2, static_cast<int>(std::ceil(sweep * 24)));
            std::vector<Point> points;
            if (!doughnut)
                points.push_back({cx, cy});
            for (int step = 0; step <= segments; ++step)
            {
                const double current = angle + sweep * step / segments;
                points.push_back({cx + radius * std::cos(current), cy + radius * std::sin(current)});
            }
            if (doughnut)
                for (int step = segments; step >= 0; --step)
                {
                    const double current = angle + sweep * step / segments;
                    points.push_back(
                        {cx + radius * hole * std::cos(current), cy + radius * hole * std::sin(current)});
                }
            canvas.path(points, point_fill(series, index, canvas, true), "#FFFFFF", 0.5, true);
            if (value(labels, "showPercent") || value(labels, "showVal"))
            {
                const double middle = angle + sweep / 2, distance = radius * (hole + 1) / 2;
                const auto label = value(labels, "showPercent") ? display_number(std::abs(item) / total, true)
                                                                : display_number(item);
                canvas.label(cx + distance * std::cos(middle) - 25, cy + distance * std::sin(middle) - 7, 50,
                    14, label);
            }
            angle += sweep;
        }
    }

    struct Axis
    {
        double minimum = 0;
        double maximum = 1;
        bool reverse = false;
        double logarithm = 0;

        double fraction(double input) const
        {
            if (logarithm > 1)
                input = input > 0 ? std::log(input) / std::log(logarithm) : minimum;
            const double result = std::clamp((input - minimum) / (maximum - minimum), 0.0, 1.0);
            return reverse ? 1 - result : result;
        }
    };

    Axis axis(Node source, double minimum, double maximum)
    {
        const auto scaling = child(source, "scaling");
        Axis result;
        result.minimum = value(scaling, "min", minimum);
        result.maximum = value(scaling, "max", maximum);
        result.reverse = word(scaling, "orientation") == "maxMin";
        result.logarithm = value(scaling, "logBase");
        if (result.logarithm >= 2 && result.logarithm <= 1000)
        {
            result.minimum = std::log(std::max(result.minimum, 1.0)) / std::log(result.logarithm);
            result.maximum = std::log(std::max(result.maximum, 10.0)) / std::log(result.logarithm);
        }
        else
            result.logarithm = 0;
        if (result.maximum <= result.minimum)
            result.maximum = result.minimum + std::max(1.0, std::abs(result.minimum) * 0.1);
        return result;
    }

    void cartesian(Canvas& canvas, const std::vector<Series>& series, Node plot, double left, double top,
        double width, double height, std::vector<std::string>& warnings);

    void radar(Canvas& canvas, const std::vector<Series>& series, Node plot, double left, double top,
        double width, double height, std::vector<std::string>& warnings)
    {
        std::size_t count = 0;
        double minimum = 0;
        double maximum = 0;
        for (const auto& item : series)
        {
            count = std::max(count, item.values.size());
            for (const double input : item.values)
            {
                if (!std::isfinite(input))
                {
                    continue;
                }
                minimum = std::min(minimum, input);
                maximum = std::max(maximum, input);
            }
        }
        if (count < 3)
        {
            warning(warnings, "雷达图少于三个分类，使用折线近似显示；原始定义保留。");
            cartesian(canvas, series, plot, left, top, width, height, warnings);
            return;
        }
        Node numeric_axis;
        for (const auto entry : plot.children())
        {
            if (local(entry.name()) == "valAx")
            {
                numeric_axis = entry;
                break;
            }
        }
        if (minimum == maximum)
        {
            maximum = minimum + 1;
        }
        const auto scale = axis(numeric_axis, minimum, maximum);
        const double radius = std::max(1.0, std::min(width, height) / 2 - 24);
        const double cx = left + width / 2;
        const double cy = top + height / 2;
        const auto point = [&](std::size_t index, double fraction)
        {
            const double angle = -pi / 2 + 2 * pi * static_cast<double>(index) / count;
            return Point{cx + radius * fraction * std::cos(angle), cy + radius * fraction * std::sin(angle)};
        };
        for (int ring = 1; ring <= 5; ++ring)
        {
            std::vector<Point> grid;
            for (std::size_t index = 0; index < count; ++index)
            {
                grid.push_back(point(index, ring / 5.0));
            }
            canvas.path(grid, {}, "#D9D9D9", 0.5, true);
        }
        const auto& categories = series.front().categories;
        for (std::size_t index = 0; index < count; ++index)
        {
            canvas.path({Point{cx, cy}, point(index, 1)}, {}, "#D9D9D9", 0.5);
            const auto label = index < categories.size() ? categories[index] : std::to_string(index + 1);
            const auto anchor = point(index, 1.14);
            canvas.label(anchor[0] - 34, anchor[1] - 9, 68, 18, label);
        }
        if (!value(numeric_axis, "delete"))
        {
            for (int ring = 1; ring <= 5; ++ring)
            {
                double input = scale.minimum + ring / 5.0 * (scale.maximum - scale.minimum);
                if (scale.logarithm > 1)
                {
                    input = std::pow(scale.logarithm, input);
                }
                const auto anchor = point(0, ring / 5.0);
                canvas.label(anchor[0] + 2, anchor[1] - 8, 42, 16, display_number(input), "left", 8);
            }
        }
        for (const auto& item : series)
        {
            std::vector<Point> points;
            for (std::size_t index = 0; index < count; ++index)
            {
                const double input = index < item.values.size() ? item.values[index] : missing;
                if (!std::isfinite(input))
                {
                    continue;
                }
                points.push_back(point(index, scale.fraction(input)));
            }
            if (points.size() < 2)
            {
                continue;
            }
            const std::string style = word(item.chart, "radarStyle", "standard");
            auto fill = item.fill;
            if (style == "filled")
            {
                fill.opacity = std::min(fill.opacity, 0.35);
            }
            else
            {
                fill = {};
            }
            canvas.path(points, fill, item.fill.color, 1.5, true);
            const auto marker = child(item.source, "marker");
            const bool show_markers =
                style == "marker" || (marker && word(marker, "symbol", "circle") != "none");
            const double marker_size = std::clamp(value(marker, "size", 5), 2.0, 72.0);
            const auto labels =
                child(item.source, "dLbls") ? child(item.source, "dLbls") : child(item.chart, "dLbls");
            for (std::size_t index = 0; index < item.values.size() && index < count; ++index)
            {
                if (!std::isfinite(item.values[index]))
                {
                    continue;
                }
                const auto location = point(index, scale.fraction(item.values[index]));
                if (show_markers)
                {
                    auto dot = canvas.base(location[0] - marker_size / 2, location[1] - marker_size / 2,
                        marker_size, marker_size);
                    dot.geometry = word(marker, "symbol") == "diamond" ? "diamond" : "ellipse";
                    dot.fill = point_fill(item, index, canvas, false);
                    canvas.shapes.push_back(std::move(dot));
                }
                if (value(labels, "showVal"))
                {
                    canvas.label(
                        location[0] - 25, location[1] - 18, 50, 16, display_number(item.values[index]));
                }
            }
        }
        if (std::any_of(series.begin(), series.end(), [](const auto& item)
        {
            return item.kind != "radarChart";
        }))
        {
            warning(warnings, "雷达组合图表使用共同极坐标近似显示；原始定义保留。");
        }
    }

    void surface(Canvas& canvas, const std::vector<Series>& series, double left, double top, double width,
        double height, std::vector<std::string>& warnings)
    {
        std::size_t columns = 0;
        double minimum = missing;
        double maximum = missing;
        for (const auto& item : series)
        {
            columns = std::max(columns, item.values.size());
            for (const double input : item.values)
            {
                if (!std::isfinite(input))
                {
                    continue;
                }
                minimum = std::isfinite(minimum) ? std::min(minimum, input) : input;
                maximum = std::isfinite(maximum) ? std::max(maximum, input) : input;
            }
        }
        if (!columns || !std::isfinite(minimum) || !std::isfinite(maximum))
        {
            return;
        }
        if (minimum == maximum)
        {
            maximum = minimum + 1;
        }
        std::vector<mirrorfly::PresentationFill> bands;
        for (const auto band : child(series.front().chart, "bandFmts").children())
        {
            if (local(band.name()) != "bandFmt")
            {
                continue;
            }
            const auto specified = fill_node(child(band, "spPr"));
            if (specified)
            {
                bands.push_back(canvas.reader.fill(specified));
            }
        }
        const bool wireframe = value(series.front().chart, "wireframe") != 0;
        std::size_t band_count = 2;
        if (!bands.empty())
        {
            band_count = std::max<std::size_t>(2, bands.size());
        }
        else if (!canvas.reader.colors.empty())
        {
            band_count = std::min<std::size_t>(6, std::max<std::size_t>(2, canvas.reader.colors.size()));
        }
        const double cell_width = width / columns;
        const double cell_height = height / series.size();
        for (std::size_t row = 0; row < series.size(); ++row)
        {
            const auto& item = series[row];
            for (std::size_t column = 0; column < item.values.size(); ++column)
            {
                const double input = item.values[column];
                if (!std::isfinite(input))
                {
                    continue;
                }
                const double fraction = std::clamp((input - minimum) / (maximum - minimum), 0.0, 1.0);
                const auto band = std::min<std::size_t>(band_count - 1,
                    static_cast<std::size_t>(std::floor(fraction * static_cast<double>(band_count))));
                mirrorfly::PresentationFill fill;
                if (!wireframe)
                {
                    if (!bands.empty())
                    {
                        fill = bands[std::min(band, bands.size() - 1)];
                    }
                    else if (!canvas.reader.colors.empty())
                    {
                        fill.color = canvas.reader.colors[band % canvas.reader.colors.size()];
                    }
                    else
                    {
                        fill.color = item.fill.color;
                    }
                    fill.opacity = std::min(fill.opacity, 0.86);
                }
                const double x = left + column * cell_width;
                const double y = top + row * cell_height;
                canvas.path({Point{x, y}, Point{x + cell_width, y}, Point{x + cell_width, y + cell_height},
                                Point{x, y + cell_height}},
                    fill, wireframe ? item.fill.color : "#FFFFFF", wireframe ? 1.0 : 0.5, true);
            }
        }
        const auto& categories = series.front().categories;
        const std::size_t column_step = std::max<std::size_t>(1, (columns + 11) / 12);
        for (std::size_t column = 0; column < columns; column += column_step)
        {
            const auto label = column < categories.size() ? categories[column] : std::to_string(column + 1);
            canvas.label(left + column * cell_width, top + height + 2, cell_width * column_step, 22, label);
        }
        const std::size_t row_step = std::max<std::size_t>(1, (series.size() + 11) / 12);
        for (std::size_t row = 0; row < series.size(); row += row_step)
        {
            canvas.label(
                left - 50, top + row * cell_height, 44, cell_height * row_step, series[row].name, "right", 8);
        }
        if (wireframe)
        {
            warning(warnings, "曲面图按二维网格线近似显示；原始曲面定义保留。");
        }
        else
        {
            warning(warnings, "曲面图按二维分带热力网格近似显示；原始曲面定义保留。");
        }
    }

    void stock(Canvas& canvas, const std::vector<Series>& series, Node plot, double left, double top,
        double width, double height, std::vector<std::string>& warnings)
    {
        std::vector<const Series*> prices;
        std::vector<const Series*> volumes;
        std::size_t other_series = 0;
        for (const auto& item : series)
        {
            if (item.kind == "stockChart")
            {
                prices.push_back(&item);
            }
            else if (item.kind == "barChart" || item.kind == "bar3DChart")
            {
                volumes.push_back(&item);
            }
            else
            {
                ++other_series;
            }
        }
        if (prices.size() < 3)
        {
            warning(warnings, "股票图少于三个数据系列，使用折线近似显示；原始定义保留。");
            cartesian(canvas, series, plot, left, top, width, height, warnings);
            return;
        }
        const bool has_open = prices.size() >= 4;
        const std::size_t open_index = has_open ? prices.size() - 4 : 0;
        const std::size_t high_index = prices.size() - 3;
        const std::size_t low_index = prices.size() - 2;
        const std::size_t close_index = prices.size() - 1;
        const auto& high = *prices[high_index];
        const auto& low = *prices[low_index];
        const auto& close = *prices[close_index];
        const auto& open = *prices[open_index];
        const std::size_t count = std::max({high.values.size(), low.values.size(), close.values.size(),
            has_open ? open.values.size() : std::size_t{0}});
        double minimum = missing;
        double maximum = missing;
        for (std::size_t index = has_open ? open_index : high_index; index <= close_index; ++index)
        {
            for (const double input : prices[index]->values)
            {
                if (!std::isfinite(input))
                {
                    continue;
                }
                minimum = std::isfinite(minimum) ? std::min(minimum, input) : input;
                maximum = std::isfinite(maximum) ? std::max(maximum, input) : input;
            }
        }
        if (!count || !std::isfinite(minimum) || !std::isfinite(maximum))
        {
            return;
        }
        if (minimum == maximum)
        {
            maximum = minimum + 1;
        }
        Node numeric_axis;
        Node category_axis;
        for (const auto entry : plot.children())
        {
            const auto name = local(entry.name());
            if (name == "valAx" && !numeric_axis)
            {
                numeric_axis = entry;
            }
            else if ((name == "catAx" || name == "dateAx") && !category_axis)
            {
                category_axis = entry;
            }
        }
        const auto scale = axis(numeric_axis, minimum, maximum);
        const bool reversed = word(child(category_axis, "scaling"), "orientation") == "maxMin";
        const auto category = [&](std::size_t index)
        {
            const double fraction = (static_cast<double>(index) + 0.5) / count;
            return reversed ? 1 - fraction : fraction;
        };
        const auto position = [&](std::size_t index, double input)
        {
            return Point{left + width * category(index), top + height * (1 - scale.fraction(input))};
        };
        for (int tick = 0; tick <= 5; ++tick)
        {
            const double fraction = tick / 5.0;
            double input = scale.minimum + fraction * (scale.maximum - scale.minimum);
            if (scale.logarithm > 1)
            {
                input = std::pow(scale.logarithm, input);
            }
            const double y = top + height * (1 - fraction);
            if (child(numeric_axis, "majorGridlines"))
            {
                canvas.path({Point{left, y}, Point{left + width, y}}, {}, "#D9D9D9", 0.5);
            }
            if (!value(numeric_axis, "delete"))
            {
                canvas.label(left - 46, y - 8, 40, 16, display_number(input), "right");
            }
        }
        canvas.path({Point{left, top}, Point{left, top + height}}, {}, "#888888", 0.6);
        canvas.path({Point{left, top + height}, Point{left + width, top + height}}, {}, "#888888", 0.6);
        const auto& categories = !close.categories.empty() ? close.categories : high.categories;
        const std::size_t step = std::max<std::size_t>(1, (count + 19) / 20);
        for (std::size_t index = 0; index < count; index += step)
        {
            const auto label = index < categories.size() ? categories[index] : std::to_string(index + 1);
            canvas.label(left + width * category(index) - width / count * step / 2, top + height + 3,
                width / count * step, 24, label);
        }
        std::string line_color = close.fill.color.empty() ? "#666666" : close.fill.color;
        const auto line_fill = fill_node(child(child(child(close.chart, "hiLowLines"), "spPr"), "ln"));
        if (line_fill)
        {
            const auto parsed = canvas.reader.fill(line_fill);
            if (!parsed.color.empty())
            {
                line_color = parsed.color;
            }
        }
        const auto up_down = child(close.chart, "upDownBars");
        mirrorfly::PresentationFill up_fill{"#FFFFFF"};
        mirrorfly::PresentationFill down_fill{"#666666"};
        if (const auto specified = fill_node(child(child(up_down, "upBars"), "spPr")))
        {
            up_fill = canvas.reader.fill(specified);
        }
        if (const auto specified = fill_node(child(child(up_down, "downBars"), "spPr")))
        {
            down_fill = canvas.reader.fill(specified);
        }
        const double cluster = width / count;
        const double gap = std::clamp(value(up_down, "gapWidth", 150), 0.0, 500.0) / 100;
        const double bar_width = cluster / (1 + gap);
        if (!volumes.empty())
        {
            const auto& volume = *volumes.front();
            double volume_maximum = 0;
            for (const double input : volume.values)
            {
                if (std::isfinite(input))
                {
                    volume_maximum = std::max(volume_maximum, std::abs(input));
                }
            }
            if (volume_maximum > 0)
            {
                auto volume_fill = volume.fill;
                volume_fill.opacity = std::min(volume_fill.opacity, 0.38);
                for (std::size_t index = 0; index < std::min(count, volume.values.size()); ++index)
                {
                    if (!std::isfinite(volume.values[index]))
                    {
                        continue;
                    }
                    const double bar_height = height * 0.3 * std::abs(volume.values[index]) / volume_maximum;
                    canvas.rectangle(left + cluster * index + cluster * 0.18, top + height - bar_height,
                        cluster * 0.64, std::max(0.8, bar_height), volume_fill);
                }
            }
            warning(warnings, "成交量股票组合使用独立比例的底部柱形近似显示；原始双轴定义保留。");
            if (volumes.size() > 1)
            {
                warning(warnings, "股票组合显示首个成交量系列；其他非价格系列按原始定义保留。");
            }
        }
        if (other_series)
        {
            warning(warnings, "股票组合中的其他图表系列暂未叠加显示；原始组合定义保留。");
        }
        const auto labels =
            child(close.source, "dLbls") ? child(close.source, "dLbls") : child(close.chart, "dLbls");
        for (std::size_t index = 0; index < count; ++index)
        {
            if (index >= high.values.size() || index >= low.values.size() || index >= close.values.size() ||
                !std::isfinite(high.values[index]) || !std::isfinite(low.values[index]) ||
                !std::isfinite(close.values[index]))
            {
                continue;
            }
            const auto high_point = position(index, high.values[index]);
            const auto low_point = position(index, low.values[index]);
            const auto close_point = position(index, close.values[index]);
            canvas.path({high_point, low_point}, {}, line_color, 1.2);
            canvas.path({Point{close_point[0], close_point[1]},
                            Point{close_point[0] + bar_width / 2, close_point[1]}},
                {}, line_color, 1.2);
            if (has_open && index < open.values.size() && std::isfinite(open.values[index]))
            {
                const auto open_point = position(index, open.values[index]);
                canvas.path(
                    {Point{open_point[0] - bar_width / 2, open_point[1]}, open_point}, {}, line_color, 1.2);
                if (up_down)
                {
                    canvas.rectangle(close_point[0] - bar_width / 4, std::min(open_point[1], close_point[1]),
                        bar_width / 2, std::max(0.8, std::abs(open_point[1] - close_point[1])),
                        close.values[index] >= open.values[index] ? up_fill : down_fill);
                }
            }
            if (value(labels, "showVal"))
            {
                canvas.label(
                    close_point[0] - 25, close_point[1] - 18, 50, 16, display_number(close.values[index]));
            }
        }
        if (prices.size() > 4)
        {
            warning(warnings, "股票图显示最后四个开高低收系列；其他系列按原始定义保留。");
        }
        warning(warnings, "股票图按缓存数据二维近似显示；复杂线条定义保留。");
    }

    struct ValueScale
    {
        double minimum = 0;
        double maximum = 1;

        double y(double input, double top, double height) const
        {
            return top + height * (1 - std::clamp((input - minimum) / (maximum - minimum), 0.0, 1.0));
        }
    };

    ValueScale value_scale(double minimum, double maximum)
    {
        minimum = std::min(0.0, minimum);
        maximum = std::max(0.0, maximum);
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum == maximum)
        {
            maximum = minimum + 1;
        }
        return {minimum, maximum};
    }

    void extended_axes(Canvas& canvas, const ValueScale& scale, const std::vector<std::string>& categories,
        std::size_t count, double left, double top, double width, double height)
    {
        if (count == 0)
        {
            return;
        }
        for (int tick = 0; tick <= 5; ++tick)
        {
            const double input = scale.minimum + tick / 5.0 * (scale.maximum - scale.minimum);
            const double y = scale.y(input, top, height);
            canvas.path({Point{left, y}, Point{left + width, y}}, {}, "#D9D9D9", 0.5);
            canvas.label(left - 46, y - 8, 40, 16, display_number(input), "right", 8);
        }
        canvas.path({Point{left, top}, Point{left, top + height}}, {}, "#888888", 0.6);
        canvas.path({Point{left, top + height}, Point{left + width, top + height}}, {}, "#888888", 0.6);
        const std::size_t step = std::max<std::size_t>(1, (count + 19) / 20);
        for (std::size_t index = 0; index < count; index += step)
        {
            const auto label = index < categories.size() && !categories[index].empty()
                ? categories[index]
                : std::to_string(index + 1);
            canvas.label(left + width * index / count, top + height + 3, width * step / count, 24, label);
        }
    }

    void extended_columns(Canvas& canvas, const ExtendedSeries& series, bool pareto, double left, double top,
        double width, double height, std::vector<std::string>& warnings)
    {
        std::vector<double> values = extended_values(series.data);
        std::vector<std::string> categories = extended_categories(series.data);
        const auto binning = child(child(series.source, "layoutPr"), "binning");
        if (binning)
        {
            std::vector<double> finite;
            for (const double input : values)
            {
                if (std::isfinite(input))
                {
                    finite.push_back(input);
                }
            }
            if (finite.empty())
            {
                return;
            }
            const auto bounds = std::minmax_element(finite.begin(), finite.end());
            const auto bin_size_node = child(binning, "binSize");
            const auto bin_count_node = child(binning, "binCount");
            double bin_size =
                number(bin_size_node.text().as_string(), number(bin_size_node.attribute("val").value(), 0));
            std::size_t bin_count =
                bin_count_node.text().as_ullong(bin_count_node.attribute("val").as_ullong());
            if (bin_size > 0)
            {
                bin_count = static_cast<std::size_t>(std::ceil((*bounds.second - *bounds.first) / bin_size));
            }
            if (!bin_count)
            {
                bin_count = static_cast<std::size_t>(std::ceil(std::sqrt(finite.size())));
            }
            bin_count = std::clamp<std::size_t>(bin_count, 1, 128);
            if (bin_size <= 0)
            {
                bin_size = (*bounds.second - *bounds.first) / bin_count;
            }
            if (bin_size <= 0)
            {
                bin_size = 1;
            }
            values.assign(bin_count, 0);
            categories.clear();
            for (const double input : finite)
            {
                const auto index = std::min<std::size_t>(bin_count - 1,
                    static_cast<std::size_t>(std::max(0.0, std::floor((input - *bounds.first) / bin_size))));
                values[index] += 1;
            }
            for (std::size_t index = 0; index < bin_count; ++index)
            {
                categories.push_back(display_number(*bounds.first + index * bin_size) + "–" +
                    display_number(*bounds.first + (index + 1) * bin_size));
            }
        }
        if (values.empty())
        {
            return;
        }
        double minimum = 0;
        double maximum = 0;
        for (const double input : values)
        {
            if (std::isfinite(input))
            {
                minimum = std::min(minimum, input);
                maximum = std::max(maximum, input);
            }
        }
        const auto scale = value_scale(minimum, maximum);
        extended_axes(canvas, scale, categories, values.size(), left, top, width, height);
        const double cell = width / values.size();
        const double zero = scale.y(0, top, height);
        std::vector<Point> cumulative_line;
        double total = 0;
        for (const double input : values)
        {
            if (std::isfinite(input) && input > 0)
            {
                total += input;
            }
        }
        double cumulative = 0;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            if (!std::isfinite(values[index]))
            {
                continue;
            }
            const double y = scale.y(values[index], top, height);
            canvas.rectangle(left + index * cell + cell * 0.12, std::min(y, zero), cell * 0.76,
                std::max(0.8, std::abs(zero - y)), extended_point_fill(series, index, canvas));
            if (pareto && total > 0)
            {
                cumulative += std::max(0.0, values[index]);
                cumulative_line.push_back(
                    {left + cell * (index + 0.5), top + height * (1 - cumulative / total)});
            }
        }
        if (cumulative_line.size() > 1)
        {
            canvas.path(cumulative_line, {}, "#D94F4F", 1.8);
        }
        if (binning)
        {
            warning(warnings, "直方图按缓存原始值和分箱设置近似显示；原始统计定义保留。");
        }
        else if (pareto)
        {
            warning(warnings, "帕累托图按柱形与累计折线近似显示；原始轴定义保留。");
        }
    }

    void extended_waterfall(Canvas& canvas, const ExtendedSeries& series, double left, double top,
        double width, double height, std::vector<std::string>& warnings)
    {
        const auto& values = extended_values(series.data);
        const auto& categories = extended_categories(series.data);
        if (values.empty())
        {
            return;
        }
        struct Bar
        {
            double start = 0;
            double end = 0;
            bool subtotal = false;
        };
        std::vector<Bar> bars;
        bars.reserve(values.size());
        double cumulative = 0;
        double minimum = 0;
        double maximum = 0;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            if (!std::isfinite(values[index]))
            {
                bars.push_back({cumulative, cumulative, false});
                continue;
            }
            const bool subtotal = series.subtotals.count(index) != 0;
            const double start = subtotal ? 0 : cumulative;
            const double end = subtotal ? values[index] : cumulative + values[index];
            cumulative = end;
            minimum = std::min({minimum, start, end});
            maximum = std::max({maximum, start, end});
            bars.push_back({start, end, subtotal});
        }
        const auto scale = value_scale(minimum, maximum);
        extended_axes(canvas, scale, categories, bars.size(), left, top, width, height);
        const double cell = width / bars.size();
        const auto visibility = child(child(series.source, "layoutPr"), "visibility");
        const bool connectors =
            !visibility.attribute("connectorLines") || visibility.attribute("connectorLines").as_bool(true);
        for (std::size_t index = 0; index < bars.size(); ++index)
        {
            const auto& bar = bars[index];
            const double y1 = scale.y(bar.start, top, height);
            const double y2 = scale.y(bar.end, top, height);
            auto fill = extended_point_fill(series, index, canvas, false);
            if (!canvas.reader.colors.empty())
            {
                const std::size_t color = bar.subtotal ? 2 : (bar.end >= bar.start ? 0 : 1);
                fill.color = canvas.reader.colors[color % canvas.reader.colors.size()];
            }
            canvas.rectangle(left + index * cell + cell * 0.14, std::min(y1, y2), cell * 0.72,
                std::max(0.8, std::abs(y1 - y2)), fill);
            if (connectors && index + 1 < bars.size())
            {
                const double connector_y = scale.y(bar.end, top, height);
                canvas.path({Point{left + index * cell + cell * 0.86, connector_y},
                                Point{left + (index + 1) * cell + cell * 0.14, connector_y}},
                    {}, "#888888", 0.7);
            }
        }
        warning(warnings, "瀑布图按缓存增量、汇总点和连接线近似显示；原始定义保留。");
    }

    void extended_funnel(Canvas& canvas, const ExtendedSeries& series, double left, double top, double width,
        double height, std::vector<std::string>& warnings)
    {
        const auto& values = extended_values(series.data);
        const auto& categories = extended_categories(series.data);
        double maximum = 0;
        for (const double input : values)
        {
            if (std::isfinite(input))
            {
                maximum = std::max(maximum, std::abs(input));
            }
        }
        if (values.empty() || maximum <= 0)
        {
            return;
        }
        const double row = height / values.size();
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            if (!std::isfinite(values[index]))
            {
                continue;
            }
            const double top_width = width * std::abs(values[index]) / maximum;
            double bottom_width = top_width;
            if (index + 1 < values.size() && std::isfinite(values[index + 1]))
            {
                bottom_width = width * std::abs(values[index + 1]) / maximum;
            }
            const double y = top + index * row;
            canvas.path({Point{left + (width - top_width) / 2, y}, Point{left + (width + top_width) / 2, y},
                            Point{left + (width + bottom_width) / 2, y + row},
                            Point{left + (width - bottom_width) / 2, y + row}},
                extended_point_fill(series, index, canvas), "#FFFFFF", 0.6, true);
            const auto label = index < categories.size() && !categories[index].empty()
                ? categories[index] + "  " + display_number(values[index])
                : display_number(values[index]);
            canvas.label(left + width * 0.2, y, width * 0.6, row, label, "center", 9);
        }
        warning(warnings, "漏斗图按缓存值和阶段标签近似显示；原始排序与格式保留。");
    }

    void extended_treemap(Canvas& canvas, const ExtendedSeries& series, double left, double top, double width,
        double height, std::vector<std::string>& warnings, bool geographic = false)
    {
        const auto& values = extended_values(series.data);
        const auto& categories = extended_categories(series.data);
        double remaining = 0;
        for (const double input : values)
        {
            if (std::isfinite(input))
            {
                remaining += std::abs(input);
            }
        }
        if (values.empty() || remaining <= 0)
        {
            return;
        }
        double x = left;
        double y = top;
        double available_width = width;
        double available_height = height;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            if (!std::isfinite(values[index]))
            {
                continue;
            }
            const double weight = std::abs(values[index]);
            const bool last = index + 1 == values.size() || remaining <= weight;
            double cell_width = available_width;
            double cell_height = available_height;
            if (!last && available_width >= available_height)
            {
                cell_width = available_width * weight / remaining;
            }
            else if (!last)
            {
                cell_height = available_height * weight / remaining;
            }
            auto fill = extended_point_fill(series, index, canvas);
            fill.opacity = std::min(fill.opacity, 0.88);
            canvas.path({Point{x, y}, Point{x + cell_width, y}, Point{x + cell_width, y + cell_height},
                            Point{x, y + cell_height}},
                fill, "#FFFFFF", 0.8, true);
            if (cell_width >= 28 && cell_height >= 14)
            {
                const auto label = index < categories.size() && !categories[index].empty()
                    ? categories[index]
                    : display_number(values[index]);
                canvas.label(x + 3, y + 2, cell_width - 6, cell_height - 4, label, "left", 8);
            }
            if (!last && available_width >= available_height)
            {
                x += cell_width;
                available_width -= cell_width;
            }
            else if (!last)
            {
                y += cell_height;
                available_height -= cell_height;
            }
            remaining -= weight;
        }
        if (geographic)
        {
            warning(warnings, "区域地图缺少可复用地图几何，按带标签面积块近似显示；地理定义保留。");
        }
        else
        {
            warning(warnings, "树状图按缓存层级和值进行面积块近似；原始层级与格式保留。");
        }
    }

    void ring_segment(Canvas& canvas, double cx, double cy, double inner, double outer, double start,
        double sweep, const mirrorfly::PresentationFill& fill)
    {
        const int segments = std::max(3, static_cast<int>(std::ceil(std::abs(sweep) * 18)));
        std::vector<Point> points;
        for (int step = 0; step <= segments; ++step)
        {
            const double angle = start + sweep * step / segments;
            points.push_back({cx + outer * std::cos(angle), cy + outer * std::sin(angle)});
        }
        for (int step = segments; step >= 0; --step)
        {
            const double angle = start + sweep * step / segments;
            points.push_back({cx + inner * std::cos(angle), cy + inner * std::sin(angle)});
        }
        canvas.path(points, fill, "#FFFFFF", 0.6, true);
    }

    void extended_sunburst(Canvas& canvas, const ExtendedSeries& series, double left, double top,
        double width, double height, std::vector<std::string>& warnings)
    {
        const auto& values = extended_values(series.data);
        const auto& categories = extended_categories(series.data);
        double total = 0;
        for (const double input : values)
        {
            if (std::isfinite(input))
            {
                total += std::abs(input);
            }
        }
        if (values.empty() || total <= 0)
        {
            return;
        }
        const double radius = std::max(1.0, std::min(width, height) / 2 - 4);
        const double cx = left + width / 2;
        const double cy = top + height / 2;
        double angle = -pi / 2;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            if (!std::isfinite(values[index]))
            {
                continue;
            }
            const double sweep = std::abs(values[index]) / total * 2 * pi;
            ring_segment(canvas, cx, cy, radius * 0.35, radius, angle, sweep,
                extended_point_fill(series, index, canvas));
            if (sweep > 0.2)
            {
                const double middle = angle + sweep / 2;
                const auto label = index < categories.size() ? categories[index] : std::string{};
                canvas.label(cx + radius * 0.7 * std::cos(middle) - 28,
                    cy + radius * 0.7 * std::sin(middle) - 8, 56, 16, label, "center", 8);
            }
            angle += sweep;
        }
        warning(warnings, "旭日图按缓存层级和值进行环形近似；原始层级与标签定义保留。");
    }

    double quantile(const std::vector<double>& sorted, double position)
    {
        if (sorted.empty())
        {
            return 0;
        }
        const double scaled = position * (sorted.size() - 1);
        const auto lower = static_cast<std::size_t>(std::floor(scaled));
        const auto upper = std::min(sorted.size() - 1, lower + 1);
        return sorted[lower] + (sorted[upper] - sorted[lower]) * (scaled - lower);
    }

    void extended_box_whisker(Canvas& canvas, const ExtendedSeries& series, double left, double top,
        double width, double height, std::vector<std::string>& warnings)
    {
        std::vector<std::vector<double>> groups;
        std::vector<std::string> labels;
        const auto found = series.data.number_levels.find("val");
        if (found != series.data.number_levels.end() && found->second.size() > 1)
        {
            groups = found->second;
            const auto names = series.data.number_level_names.find("val");
            if (names != series.data.number_level_names.end())
            {
                labels = names->second;
            }
        }
        else
        {
            const auto& values = extended_values(series.data);
            const auto& categories = extended_categories(series.data);
            std::map<std::string, std::size_t> indexes;
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                if (!std::isfinite(values[index]))
                {
                    continue;
                }
                const std::string label =
                    index < categories.size() && !categories[index].empty() ? categories[index] : series.name;
                const auto inserted = indexes.emplace(label, groups.size());
                if (inserted.second)
                {
                    groups.emplace_back();
                    labels.push_back(label);
                }
                groups[inserted.first->second].push_back(values[index]);
            }
        }
        std::vector<std::vector<double>> filtered_groups;
        std::vector<std::string> filtered_labels;
        for (std::size_t index = 0; index < groups.size(); ++index)
        {
            auto group = std::move(groups[index]);
            group.erase(std::remove_if(group.begin(), group.end(),
                            [](double input)
            {
                return !std::isfinite(input);
            }),
                group.end());
            if (group.empty())
            {
                continue;
            }
            filtered_groups.push_back(std::move(group));
            filtered_labels.push_back(index < labels.size() ? labels[index] : std::string{});
        }
        groups = std::move(filtered_groups);
        labels = std::move(filtered_labels);
        if (groups.empty())
        {
            return;
        }
        double minimum = missing;
        double maximum = missing;
        for (auto& group : groups)
        {
            std::sort(group.begin(), group.end());
            if (!group.empty())
            {
                minimum = std::isfinite(minimum) ? std::min(minimum, group.front()) : group.front();
                maximum = std::isfinite(maximum) ? std::max(maximum, group.back()) : group.back();
            }
        }
        const auto scale = value_scale(minimum, maximum);
        extended_axes(canvas, scale, labels, groups.size(), left, top, width, height);
        const double cell = width / groups.size();
        for (std::size_t index = 0; index < groups.size(); ++index)
        {
            const auto& group = groups[index];
            if (group.empty())
            {
                continue;
            }
            const double low = scale.y(group.front(), top, height);
            const double q1 = scale.y(quantile(group, 0.25), top, height);
            const double median = scale.y(quantile(group, 0.5), top, height);
            const double q3 = scale.y(quantile(group, 0.75), top, height);
            const double high = scale.y(group.back(), top, height);
            const double center = left + cell * (index + 0.5);
            const double box_width = cell * 0.55;
            canvas.path({Point{center, high}, Point{center, low}}, {}, "#555555", 1.0);
            canvas.path({Point{center - box_width / 4, high}, Point{center + box_width / 4, high}}, {},
                "#555555", 1.0);
            canvas.path(
                {Point{center - box_width / 4, low}, Point{center + box_width / 4, low}}, {}, "#555555", 1.0);
            canvas.path({Point{center - box_width / 2, q3}, Point{center + box_width / 2, q3},
                            Point{center + box_width / 2, q1}, Point{center - box_width / 2, q1}},
                extended_point_fill(series, index, canvas), "#555555", 1.0, true);
            canvas.path({Point{center - box_width / 2, median}, Point{center + box_width / 2, median}}, {},
                "#333333", 1.4);
        }
        warning(warnings, "箱线图按缓存样本计算四分位与须线；离群点和复杂统计格式近似。");
    }

    void cartesian(Canvas& canvas, const std::vector<Series>& series, Node plot, double left, double top,
        double width, double height, std::vector<std::string>& warnings)
    {
        const bool horizontal = word(series.front().chart, "barDir") == "bar";
        const bool scatter = series.front().kind == "scatterChart" || series.front().kind == "bubbleChart";
        const std::string grouping = word(series.front().chart, "grouping");
        const bool stacked = grouping == "stacked" || grouping == "percentStacked";
        const bool percent = grouping == "percentStacked";
        std::size_t count = 0;
        for (const auto& item : series)
            count = std::max(count, item.values.size());
        if (count == 0)
            return;
        std::vector<double> positive(count), negative(count), totals(count);
        double minimum = 0, maximum = 0, x_minimum = missing, x_maximum = missing;
        for (const auto& item : series)
            for (std::size_t index = 0; index < item.values.size(); ++index)
            {
                const double input = item.values[index];
                if (!std::isfinite(input))
                    continue;
                totals[index] += std::abs(input);
                (input >= 0 ? positive[index] : negative[index]) += input;
                minimum = std::min(minimum, input);
                maximum = std::max(maximum, input);
                if (index < item.x.size() && std::isfinite(item.x[index]))
                {
                    x_minimum = std::isfinite(x_minimum) ? std::min(x_minimum, item.x[index]) : item.x[index];
                    x_maximum = std::isfinite(x_maximum) ? std::max(x_maximum, item.x[index]) : item.x[index];
                }
            }
        if (stacked)
        {
            minimum = *std::min_element(negative.begin(), negative.end());
            maximum = *std::max_element(positive.begin(), positive.end());
        }
        if (percent)
        {
            minimum = minimum < 0 ? -1 : 0;
            maximum = maximum > 0 ? 1 : 0;
        }
        if (minimum == maximum)
            maximum = minimum + 1;
        Node numeric_axis, category_axis;
        for (auto entry : plot.children())
        {
            const auto name = local(entry.name());
            if (name == "valAx")
            {
                const auto position = word(entry, "axPos");
                if ((horizontal && (position == "b" || position == "t")) ||
                    (!horizontal && (position == "l" || position == "r")))
                    numeric_axis = entry;
                else if (scatter)
                    category_axis = entry;
                if (!numeric_axis)
                    numeric_axis = entry;
            }
            else if (name == "catAx" || name == "dateAx")
                category_axis = entry;
        }
        const auto scale = axis(numeric_axis, minimum, maximum);
        const auto x_scale = axis(category_axis, std::isfinite(x_minimum) ? x_minimum : 0,
            std::isfinite(x_maximum) ? x_maximum : static_cast<double>(count));
        const bool reversed_categories = word(child(category_axis, "scaling"), "orientation") == "maxMin";
        const auto category = [&](std::size_t index)
        {
            double position = (static_cast<double>(index) + 0.5) / count;
            return reversed_categories ? 1 - position : position;
        };
        const auto position = [&](double category_position, double input) -> Point
        {
            return horizontal
                ? Point{left + width * scale.fraction(input), top + height * (1 - category_position)}
                : Point{left + width * category_position, top + height * (1 - scale.fraction(input))};
        };
        for (int tick = 0; tick <= 5; ++tick)
        {
            const double fraction = tick / 5.0;
            double input = scale.minimum + fraction * (scale.maximum - scale.minimum);
            if (scale.logarithm > 1)
                input = std::pow(scale.logarithm, input);
            const auto start = position(0, input), end = position(1, input);
            if (child(numeric_axis, "majorGridlines"))
                canvas.path({start, end}, {}, "#D9D9D9", 0.5);
            if (!value(numeric_axis, "delete"))
            {
                if (horizontal)
                    canvas.label(start[0] - 30, top + height + 2, 60, 16, display_number(input, percent));
                else
                    canvas.label(left - 46, start[1] - 8, 40, 16, display_number(input, percent), "right");
            }
        }
        if (!value(numeric_axis, "delete"))
        {
            const Point start = horizontal ? Point{left, top + height} : Point{left, top};
            const Point end = horizontal ? Point{left + width, top + height} : Point{left, top + height};
            canvas.path({start, end}, {}, "#888888", 0.6);
        }
        if (!value(category_axis, "delete"))
        {
            canvas.path({position(0, 0), position(1, 0)}, {}, "#888888", 0.6);
            const std::size_t step = std::max<std::size_t>(1, (count + 19) / 20);
            for (std::size_t index = 0; index < count && !scatter; index += step)
            {
                const auto& labels = series.front().categories;
                const auto label = index < labels.size() ? labels[index] : std::to_string(index + 1);
                if (horizontal)
                    canvas.label(left - 64, top + height * (1 - category(index)) - 9, 58, 18, label, "right");
                else
                    canvas.label(left + width * category(index) - width / count * step / 2, top + height + 3,
                        width / count * step, 26, label);
            }
            if (scatter)
                for (int tick = 0; tick <= 5; ++tick)
                {
                    const double input = x_scale.minimum + (x_scale.maximum - x_scale.minimum) * tick / 5;
                    canvas.label(left + width * x_scale.fraction(input) - 25, top + height + 3, 50, 20,
                        display_number(input));
                }
        }
        std::fill(positive.begin(), positive.end(), 0);
        std::fill(negative.begin(), negative.end(), 0);
        for (std::size_t series_index = 0; series_index < series.size(); ++series_index)
        {
            const auto& item = series[series_index];
            const bool bar = item.kind == "barChart" || item.kind == "bar3DChart";
            const bool area = item.kind == "areaChart" || item.kind == "area3DChart";
            const auto labels =
                child(item.source, "dLbls") ? child(item.source, "dLbls") : child(item.chart, "dLbls");
            const auto marker = child(item.source, "marker");
            const std::string symbol = word(marker, "symbol", scatter ? "circle" : "none");
            const double marker_size = std::clamp(value(marker, "size", 5), 2.0, 72.0);
            const double cluster = (horizontal ? height : width) / count;
            const double gap = std::clamp(value(item.chart, "gapWidth", 150), 0.0, 500.0) / 100;
            const double bar_width = cluster / (1 + gap) / (stacked ? 1 : series.size());
            std::vector<Point> points, bases;
            const auto flush = [&]()
            {
                if (area && !points.empty())
                {
                    points.insert(points.end(), bases.rbegin(), bases.rend());
                    canvas.path(points, item.fill, {}, 0, true);
                }
                else if (!scatter || word(item.chart, "scatterStyle").find("line") != std::string::npos ||
                    word(item.chart, "scatterStyle").find("smooth") != std::string::npos)
                    canvas.path(points, {}, item.fill.color, 1.8);
                points.clear();
                bases.clear();
            };
            for (std::size_t index = 0; index < item.values.size(); ++index)
            {
                double input = item.values[index];
                if (!std::isfinite(input) ||
                    (scatter && (index >= item.x.size() || !std::isfinite(item.x[index]))))
                {
                    flush();
                    continue;
                }
                if (percent)
                    input = totals[index] > 0 ? input / totals[index] : 0;
                auto& accumulated = input >= 0 ? positive[index] : negative[index];
                const double base = stacked ? accumulated : 0;
                if (stacked)
                    accumulated += input;
                const double center = scatter ? x_scale.fraction(item.x[index]) : category(index);
                const auto point = position(center, base + input), bottom = position(center, base);
                if (bar)
                {
                    const double offset =
                        stacked ? -bar_width / 2 : bar_width * (series_index - series.size() / 2.0);
                    const auto fill = point_fill(item, index, canvas, value(item.chart, "varyColors") != 0);
                    if (horizontal)
                        canvas.rectangle(std::min(point[0], bottom[0]), point[1] + offset,
                            std::abs(point[0] - bottom[0]), bar_width, fill);
                    else
                        canvas.rectangle(point[0] + offset, std::min(point[1], bottom[1]), bar_width,
                            std::abs(point[1] - bottom[1]), fill);
                }
                else
                {
                    points.push_back(point);
                    bases.push_back(bottom);
                    if (symbol != "none" || item.kind == "bubbleChart")
                    {
                        double size = marker_size;
                        if (item.kind == "bubbleChart" && index < item.sizes.size() &&
                            std::isfinite(item.sizes[index]))
                        {
                            const auto biggest =
                                *std::max_element(item.sizes.begin(), item.sizes.end(), [](double a, double b)
                            {
                                return (std::isfinite(a) ? a : 0) < (std::isfinite(b) ? b : 0);
                            });
                            size =
                                biggest > 0 ? 30 * std::sqrt(std::max(0.0, item.sizes[index]) / biggest) : 5;
                        }
                        auto dot = canvas.base(point[0] - size / 2, point[1] - size / 2, size, size);
                        dot.geometry =
                            symbol == "square" ? "rect" : (symbol == "diamond" ? "diamond" : "ellipse");
                        dot.fill = point_fill(item, index, canvas, false);
                        canvas.shapes.push_back(std::move(dot));
                    }
                }
                if (value(labels, "showVal"))
                    canvas.label(point[0] - 25, point[1] - 18, 50, 16, display_number(item.values[index]));
            }
            if (!bar)
                flush();
        }
        if (series.size() > 1)
            for (const auto& item : series)
                if (item.kind != series.front().kind || item.chart != series.front().chart)
                {
                    warning(warnings, "组合图表使用共同坐标轴近似显示；原始定义保留。");
                    break;
                }
    }
}

namespace mirrorfly
{
    std::vector<PresentationShape> presentation_chart_shapes(Node space, const PresentationShape& frame,
        const PresentationChartReader& reader, std::vector<std::string>& warnings)
    {
        Canvas canvas{frame, reader, {}, reader.text(child(space, "txPr"))};
        const auto chart = child(space, "chart");
        const auto plot = child(chart, "plotArea");
        std::vector<Series> series;
        std::size_t total_points = 0;
        for (auto group : plot.children())
        {
            const auto kind = local(group.name());
            if (kind.size() < 5 || kind.substr(kind.size() - 5) != "Chart")
                continue;
            const bool supported = kind == "barChart" || kind == "lineChart" || kind == "areaChart" ||
                kind == "pieChart" || kind == "doughnutChart" || kind == "scatterChart" ||
                kind == "bubbleChart" || kind == "radarChart" || kind == "bar3DChart" ||
                kind == "line3DChart" || kind == "area3DChart" || kind == "pie3DChart" ||
                kind == "surfaceChart" || kind == "surface3DChart" || kind == "stockChart" ||
                kind == "ofPieChart";
            if (!supported)
            {
                warning(warnings, "此图表类型暂未绘制；原始图表及工作簿保留。");
                continue;
            }
            if (kind.find("3D") != std::string::npos)
                warning(warnings, "三维图表按二维缓存数据近似显示；原始三维定义保留。");
            for (auto entry : group.children())
            {
                if (local(entry.name()) != "ser")
                    continue;
                if (series.size() >= 32)
                {
                    warning(warnings, "图表超过 32 个系列显示上限；原始数据保留。");
                    break;
                }
                Series item;
                item.source = entry;
                item.chart = group;
                item.kind = kind;
                item.name = child(child(entry, "tx"), "v").text().as_string();
                const auto names = strings(child(entry, "tx"));
                if (item.name.empty() && !names.empty())
                    item.name = names.front();
                if (item.name.empty())
                    item.name = "系列 " + std::to_string(series.size() + 1);
                item.categories = strings(child(entry, "cat"));
                item.values = numbers(child(entry, "val"));
                if (item.values.empty())
                    item.values = numbers(child(entry, "yVal"));
                item.x = numbers(child(entry, "xVal"));
                item.sizes = numbers(child(entry, "bubbleSize"));
                total_points += item.values.size();
                if (total_points > 4096)
                {
                    warning(warnings, "图表超过 4096 个数据点显示上限；原始数据保留。");
                    break;
                }
                item.fill.color =
                    reader.colors.empty() ? "#4472C4" : reader.colors[series.size() % reader.colors.size()];
                const auto properties = child(entry, "spPr");
                if (const auto specified = fill_node(properties))
                    item.fill = reader.fill(specified);
                else if (const auto line_fill = fill_node(child(properties, "ln")))
                    item.fill = reader.fill(line_fill);
                if (!item.values.empty())
                    series.push_back(std::move(item));
            }
        }
        if (series.empty())
        {
            warning(warnings, "图表没有可用的缓存数据，暂未显示；原始图表及工作簿保留。");
            return {};
        }
        PresentationFill background{"#FFFFFF"};
        if (const auto specified = fill_node(child(space, "spPr")))
            background = reader.fill(specified);
        canvas.rectangle(0, 0, frame.width, frame.height, background);
        double left = 52, top = 18, right = 18, bottom = 38;
        const auto title = child(chart, "title");
        if (title)
        {
            const auto rich = child(child(title, "tx"), "rich");
            const auto text = strings(child(title, "tx"));
            canvas.label(
                10, 5, frame.width - 20, 28, text.empty() ? "" : text.front(), "center", 16, true, rich);
            top += 28;
        }
        const bool pie_chart = series.front().kind == "pieChart" || series.front().kind == "pie3DChart" ||
            series.front().kind == "doughnutChart" || series.front().kind == "ofPieChart";
        const auto legend = child(chart, "legend");
        const auto legend_position = word(legend, "legendPos", "r");
        std::vector<std::string> legend_names;
        if (pie_chart)
            legend_names = series.front().categories;
        else
            for (const auto& item : series)
                legend_names.push_back(item.name);
        if (legend)
        {
            const bool side = legend_position == "r" || legend_position == "l" || legend_position == "tr";
            const double extent = std::min(100.0, frame.width * 0.24);
            double x = legend_position == "l" ? 8 : frame.width - extent;
            double y = top + 6;
            if (!side)
            {
                x = 12;
                y = legend_position == "t" ? top : frame.height - 20;
                if (legend_position == "t")
                    top += 24;
                else
                    bottom += 22;
            }
            else if (legend_position == "l")
                left += extent;
            else
                right += extent;
            const auto count = std::min<std::size_t>(legend_names.size(), side ? 20 : 6);
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto fill =
                    pie_chart ? point_fill(series.front(), index, canvas, true) : series[index].fill;
                canvas.rectangle(x, y + 4, 8, 8, fill);
                canvas.label(x + 12, y, side ? extent - 16 : (frame.width - 24) / count - 16, 18,
                    legend_names[index], "left");
                if (side)
                    y += 18;
                else
                    x += (frame.width - 24) / count;
            }
        }
        if (pie_chart)
        {
            if (legend_position != "l")
                left = 8;
            bottom = legend && legend_position == "b" ? 28 : 8;
        }
        const auto manual = child(child(plot, "layout"), "manualLayout");
        if (manual && value(manual, "w") > 0 && value(manual, "h") > 0)
        {
            left = std::clamp(value(manual, "x"), 0.0, 0.95) * frame.width;
            top = std::clamp(value(manual, "y"), 0.0, 0.95) * frame.height;
            right = frame.width - left - std::clamp(value(manual, "w"), 0.05, 1.0) * frame.width;
            bottom = frame.height - top - std::clamp(value(manual, "h"), 0.05, 1.0) * frame.height;
        }
        const double width = std::max(1.0, frame.width - left - right);
        const double height = std::max(1.0, frame.height - top - bottom);
        if (const auto specified = fill_node(child(plot, "spPr")))
            canvas.rectangle(left, top, width, height, reader.fill(specified));
        if (pie_chart)
        {
            pie(canvas, series.front(), left, top, width, height);
            if (series.front().kind == "ofPieChart")
            {
                warning(warnings, "分离饼图按单饼图近似显示；第二绘图区与分割规则保留。");
            }
        }
        else if (series.front().kind == "radarChart")
            radar(canvas, series, plot, left, top, width, height, warnings);
        else if (series.front().kind == "surfaceChart" || series.front().kind == "surface3DChart")
            surface(canvas, series, left, top, width, height, warnings);
        else if (std::any_of(series.begin(), series.end(), [](const auto& item)
        {
            return item.kind == "stockChart";
        }))
            stock(canvas, series, plot, left, top, width, height, warnings);
        else
            cartesian(canvas, series, plot, left, top, width, height, warnings);
        warning(warnings, "图表按文档缓存数据显示；自动布局和复杂格式有近似，数据编辑暂不支持。");
        return std::move(canvas.shapes);
    }

    std::vector<PresentationShape> presentation_chart_ex_shapes(Node space, const PresentationShape& frame,
        const PresentationChartReader& reader, std::vector<std::string>& warnings)
    {
        std::map<std::string, ExtendedData> data_sets;
        const auto chart_data = child(space, "chartData");
        for (const auto data : chart_data.children())
        {
            if (local(data.name()) != "data")
            {
                continue;
            }
            ExtendedData parsed;
            for (const auto dimension : data.children())
            {
                const auto dimension_kind = local(dimension.name());
                if (dimension_kind == "strDim")
                {
                    for (const auto level : dimension.children())
                    {
                        if (local(level.name()) == "lvl")
                        {
                            parsed.category_levels.push_back(extended_strings(level));
                        }
                    }
                }
                else if (dimension_kind == "numDim")
                {
                    const std::string type = dimension.attribute("type").as_string("val");
                    for (const auto level : dimension.children())
                    {
                        if (local(level.name()) != "lvl")
                        {
                            continue;
                        }
                        parsed.number_levels[type].push_back(extended_numbers(level));
                        parsed.number_level_names[type].push_back(level.attribute("name").value());
                    }
                }
            }
            auto id = data.attribute("id").value();
            if (!*id)
            {
                id = child(data, "id").attribute("val").value();
            }
            if (*id)
            {
                data_sets[id] = std::move(parsed);
            }
        }

        const auto chart = child(space, "chart");
        const auto plot = child(chart, "plotArea");
        auto region = child(plot, "plotAreaRegion");
        if (!region)
        {
            region = plot;
        }
        std::vector<ExtendedSeries> series;
        std::size_t total_points = 0;
        for (const auto entry : region.children())
        {
            if (local(entry.name()) != "series")
            {
                continue;
            }
            if (series.size() >= 32)
            {
                warning(warnings, "扩展图表超过 32 个系列显示上限；原始数据保留。");
                break;
            }
            if (entry.attribute("hidden").as_bool(false))
            {
                continue;
            }
            auto data_id = child(entry, "dataId").attribute("val").value();
            if (!*data_id)
            {
                data_id = entry.attribute("dataId").value();
            }
            const auto found = data_sets.find(data_id);
            if (found == data_sets.end())
            {
                warning(warnings, "扩展图表系列引用的数据缺失；该系列未显示，原始定义保留。");
                continue;
            }
            ExtendedSeries item;
            item.source = entry;
            item.layout = entry.attribute("layoutId").value();
            if (item.layout.empty())
            {
                item.layout = child(entry, "layoutId").attribute("val").value();
            }
            item.name = descendant_text(child(entry, "tx"));
            item.data = found->second;
            const auto values = extended_values(item.data);
            if (item.name.empty())
            {
                const auto names = item.data.number_level_names.find("val");
                if (names != item.data.number_level_names.end() && !names->second.empty())
                {
                    item.name = names->second.front();
                }
            }
            if (item.name.empty())
            {
                item.name = "系列 " + std::to_string(series.size() + 1);
            }
            if (values.empty())
            {
                warning(warnings, "扩展图表系列没有可用的缓存数值；该系列未显示，原始定义保留。");
                continue;
            }
            if (total_points + values.size() > 4096)
            {
                warning(warnings, "扩展图表超过 4096 个数据点显示上限；超出部分未显示，原始数据保留。");
                const std::size_t remaining = 4096 - total_points;
                for (auto& [type, levels] : item.data.number_levels)
                {
                    for (auto& level : levels)
                    {
                        level.resize(std::min(level.size(), remaining));
                    }
                }
                for (auto& level : item.data.category_levels)
                {
                    level.resize(std::min(level.size(), remaining));
                }
            }
            total_points += extended_values(item.data).size();
            item.fill.color =
                reader.colors.empty() ? "#4472C4" : reader.colors[series.size() % reader.colors.size()];
            if (const auto specified = fill_node(child(entry, "spPr")))
            {
                item.fill = reader.fill(specified);
            }
            const auto subtotals = child(child(entry, "layoutPr"), "subtotals");
            for (const auto index : subtotals.children())
            {
                if (local(index.name()) == "idx")
                {
                    item.subtotals.insert(index.attribute("val").as_ullong());
                }
            }
            series.push_back(std::move(item));
            if (total_points >= 4096)
            {
                break;
            }
        }
        if (series.empty())
        {
            warning(warnings, "扩展图表没有可用的缓存数据，暂未显示；原始图表及工作簿保留。");
            return {};
        }

        Canvas canvas{frame, reader, {}, reader.text(child(space, "txPr"))};
        PresentationFill background{"#FFFFFF"};
        if (const auto specified = fill_node(child(space, "spPr")))
        {
            background = reader.fill(specified);
        }
        canvas.rectangle(0, 0, frame.width, frame.height, background);
        double left = 52;
        double top = 18;
        double right = 18;
        double bottom = 38;
        const auto title = child(chart, "title");
        if (title)
        {
            const auto rich = child(child(title, "tx"), "rich");
            canvas.label(10, 5, frame.width - 20, 28, descendant_text(title), "center", 16, true, rich);
            top += 28;
        }
        if (const auto legend = child(chart, "legend"))
        {
            const double extent = std::min(100.0, frame.width * 0.24);
            double y = top + 6;
            const auto count = std::min<std::size_t>(series.size(), 20);
            for (std::size_t index = 0; index < count; ++index)
            {
                canvas.rectangle(frame.width - extent, y + 4, 8, 8, series[index].fill);
                canvas.label(frame.width - extent + 12, y, extent - 16, 18, series[index].name, "left");
                y += 18;
            }
            right += extent;
        }
        const double width = std::max(1.0, frame.width - left - right);
        const double height = std::max(1.0, frame.height - top - bottom);
        if (const auto specified = fill_node(child(region, "spPr")))
        {
            canvas.rectangle(left, top, width, height, reader.fill(specified));
        }

        const auto primary = std::find_if(series.begin(), series.end(), [](const auto& item)
        {
            return item.layout != "paretoLine";
        });
        const auto& item = primary == series.end() ? series.front() : *primary;
        const bool pareto = std::any_of(series.begin(), series.end(), [](const auto& candidate)
        {
            return candidate.layout == "paretoLine";
        });
        if (item.layout == "waterfall")
        {
            extended_waterfall(canvas, item, left, top, width, height, warnings);
        }
        else if (item.layout == "funnel")
        {
            extended_funnel(canvas, item, left, top, width, height, warnings);
        }
        else if (item.layout == "treemap")
        {
            extended_treemap(canvas, item, left, top, width, height, warnings);
        }
        else if (item.layout == "sunburst")
        {
            extended_sunburst(canvas, item, left, top, width, height, warnings);
        }
        else if (item.layout == "boxWhisker")
        {
            extended_box_whisker(canvas, item, left, top, width, height, warnings);
        }
        else if (item.layout == "regionMap")
        {
            extended_treemap(canvas, item, left, top, width, height, warnings, true);
        }
        else
        {
            extended_columns(canvas, item, pareto, left, top, width, height, warnings);
            if (item.layout != "clusteredColumn" && item.layout != "paretoLine")
            {
                warning(warnings, "未知扩展图表布局按柱形图近似显示；原始定义保留。");
            }
        }
        if (series.size() > 1 && !pareto)
        {
            warning(warnings, "扩展图表显示首个主要系列；其他系列及组合关系按原始定义保留。");
        }
        warning(warnings, "Office 扩展图表按缓存数据只读近似显示；复杂布局和数据编辑暂不支持。");
        return std::move(canvas.shapes);
    }
}
