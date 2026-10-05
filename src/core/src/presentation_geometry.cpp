#include <mirrorfly/presentation_geometry.hpp>

#include "presentation_geometry_data.hpp"
#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>

namespace
{
    using Node = pugi::xml_node;
    constexpr double pi = 3.14159265358979323846;
    constexpr double angle_unit = pi / 10800000;
    constexpr double emu_per_point = 12700;

    std::string local(const char* name)
    {
        const std::string value(name);
        const auto colon = value.find(':');
        return colon == std::string::npos ? value : value.substr(colon + 1);
    }

    Node child(Node node, const char* name)
    {
        for (auto item : node.children())
            if (local(item.name()) == name)
                return item;
        return {};
    }

    const pugi::xml_document& presets()
    {
        static const auto document = []
        {
            pugi::xml_document result;
            std::string xml;
            for (const auto* part : mirrorfly::presentation_geometry_xml_parts)
                xml += part;
            if (!result.load_string(xml.c_str()))
                throw std::runtime_error("预设图形数据无效。");
            return result;
        }();
        return document;
    }

    double bounded(double value, double maximum = 1e100)
    {
        if (!std::isfinite(value) || std::abs(value) > maximum)
            throw std::runtime_error("图形路径或控制点超出范围。");
        return value;
    }

    struct Guides
    {
        std::map<std::string, double> values;
        double get(const std::string& name) const
        {
            const auto found = values.find(name);
            if (found != values.end())
                return found->second;
            char* end = nullptr;
            const double value = std::strtod(name.c_str(), &end);
            if (name.empty() || end != name.c_str() + name.size())
                throw std::runtime_error("图形公式引用了未知控制点：" + name);
            return bounded(value);
        }

        double formula(const std::string& text, bool preset) const
        {
            std::istringstream input(text);
            std::string op, token;
            input >> op;
            std::vector<double> args;
            bool ignored_zero = false;
            while (input >> token)
            {
                if (args.size() == 3)
                {
                    // Eight formulas in the pinned POI presets have one redundant trailing zero.
                    if (preset && !ignored_zero && op == "+-" && token == "0")
                    {
                        ignored_zero = true;
                        continue;
                    }
                    throw std::runtime_error("图形公式参数过多。");
                }
                args.push_back(get(token));
            }
            const auto arity = [&](std::size_t count)
            {
                if (args.size() != count)
                    throw std::runtime_error("图形公式参数数量无效。");
            };
            const double x = args.empty() ? 0 : args[0];
            const double y = args.size() < 2 ? 0 : args[1];
            const double z = args.size() < 3 ? 0 : args[2];
            if (op == "val" || op == "abs" || op == "sqrt")
            {
                arity(1);
                return op == "val" ? x : op == "abs" ? std::abs(x) : std::sqrt(std::max(0.0, x));
            }
            if (op == "min" || op == "max" || op == "at2" || op == "sin" || op == "cos" || op == "tan")
            {
                arity(2);
                if (op == "min")
                    return std::min(x, y);
                if (op == "max")
                    return std::max(x, y);
                if (op == "at2")
                    return std::atan2(y, x) / angle_unit;
                if (op == "sin")
                    return x * std::sin(y * angle_unit);
                if (op == "cos")
                    return x * std::cos(y * angle_unit);
                return x * std::tan(y * angle_unit);
            }
            arity(3);
            if (op == "*/")
                return z == 0 ? 0 : x * y / z;
            if (op == "+-")
                return x + y - z;
            if (op == "+/")
                return z == 0 ? 0 : (x + y) / z;
            if (op == "?:")
                return x > 0 ? y : z;
            if (op == "pin")
                return std::max(x, std::min(y, z));
            if (op == "mod")
                return std::hypot(x, y, z);
            if (op == "cat2")
                return x * std::cos(std::atan2(z, y));
            if (op == "sat2")
                return x * std::sin(std::atan2(z, y));
            throw std::runtime_error("不支持的图形公式操作：" + op);
        }

        void read(Node list, std::size_t& count, bool preset = false)
        {
            for (auto node : list.children())
            {
                if (++count > 4096 || local(node.name()) != "gd")
                    throw std::runtime_error("图形控制点过多或类型无效。");
                const std::string name = node.attribute("name").value();
                if (name.empty() || name.size() > 128)
                    throw std::runtime_error("图形控制点名称无效。");
                values[name] = bounded(formula(node.attribute("fmla").value(), preset));
            }
        }
    };

    Guides initial_guides(double width, double height)
    {
        const double w = width * emu_per_point, h = height * emu_per_point;
        Guides result{{{"w", w}, {"h", h}, {"l", 0}, {"t", 0}, {"r", w}, {"b", h}, {"hc", w / 2},
            {"vc", h / 2}, {"ss", std::min(w, h)}, {"ls", std::max(w, h)}, {"cd2", 10800000},
            {"cd4", 5400000}, {"cd8", 2700000}, {"3cd4", 16200000}, {"3cd8", 8100000}, {"5cd8", 13500000},
            {"7cd8", 18900000}}};
        for (int divisor : {2, 3, 4, 5, 6, 8, 10, 12, 16, 32})
        {
            const auto suffix = std::to_string(divisor);
            result.values["wd" + suffix] = w / divisor;
            result.values["hd" + suffix] = h / divisor;
            result.values["ssd" + suffix] = std::min(w, h) / divisor;
        }
        return result;
    }

    void arc(mirrorfly::PresentationPath& path, double& x, double& y, double wr, double hr, double start,
        double sweep)
    {
        if (wr < 0 || hr < 0 || std::abs(sweep) > 21600000.01)
            throw std::runtime_error("图形圆弧参数无效。");
        if (wr == 0 || hr == 0 || sweep == 0)
            return;
        const auto parameter = [&](double angle)
        {
            return std::atan2(wr * std::sin(angle * angle_unit), hr * std::cos(angle * angle_unit));
        };
        double a = parameter(start);
        double delta = parameter(start + sweep) - a;
        if (std::abs(sweep) >= 21600000 - 0.01)
            delta = std::copysign(2 * pi, sweep);
        else if (sweep > 0 && delta < 0)
            delta += 2 * pi;
        else if (sweep < 0 && delta > 0)
            delta -= 2 * pi;
        const double cx = x - wr * std::cos(a), cy = y - hr * std::sin(a);
        const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (pi / 2))));
        const double step = delta / segments;
        for (int index = 0; index < segments; ++index)
        {
            const double b = a + step, factor = 4.0 / 3 * std::tan(step / 4);
            const double end_x = cx + wr * std::cos(b), end_y = cy + hr * std::sin(b);
            path.commands.push_back({mirrorfly::PresentationPathAction::Cubic,
                {x - factor * wr * std::sin(a), y + factor * hr * std::cos(a),
                    end_x + factor * wr * std::sin(b), end_y - factor * hr * std::cos(b), end_x, end_y}});
            x = end_x;
            y = end_y;
            a = b;
        }
    }

    mirrorfly::PresentationPath read_path(Node node, const Guides& guides, std::size_t& count)
    {
        using Action = mirrorfly::PresentationPathAction;
        mirrorfly::PresentationPath path;
        path.width = node.attribute("w") ? guides.get(node.attribute("w").value()) : guides.get("w");
        path.height = node.attribute("h") ? guides.get(node.attribute("h").value()) : guides.get("h");
        path.fill = node.attribute("fill").as_string("norm");
        path.stroke = node.attribute("stroke").as_bool(true);
        if (path.width <= 0 || path.height <= 0 || path.width > 1e14 || path.height > 1e14)
            throw std::runtime_error("图形路径尺寸无效。");
        if (path.fill != "norm" && path.fill != "none" && path.fill != "lighten" &&
            path.fill != "lightenLess" && path.fill != "darken" && path.fill != "darkenLess")
            throw std::runtime_error("图形路径填充模式无效。");
        double x = 0, y = 0, start_x = 0, start_y = 0;
        for (auto command : node.children())
        {
            if (++count > 20000)
                throw std::runtime_error("图形路径点超过限制。");
            const auto type = local(command.name());
            if (type == "arcTo")
            {
                arc(path, x, y, guides.get(command.attribute("wR").value()),
                    guides.get(command.attribute("hR").value()),
                    guides.get(command.attribute("stAng").value()),
                    guides.get(command.attribute("swAng").value()));
                continue;
            }
            mirrorfly::PresentationPathCommand result;
            std::size_t points = 0;
            if (type == "moveTo")
            {
                result.action = Action::Move;
                points = 1;
            }
            else if (type == "lnTo")
            {
                result.action = Action::Line;
                points = 1;
            }
            else if (type == "quadBezTo")
            {
                result.action = Action::Quadratic;
                points = 2;
            }
            else if (type == "cubicBezTo")
            {
                result.action = Action::Cubic;
                points = 3;
            }
            else if (type == "close")
                result.action = Action::Close;
            else
                throw std::runtime_error("不支持的图形路径指令。");
            std::size_t index = 0;
            for (auto point : command.children())
            {
                if (local(point.name()) != "pt" || index >= points)
                    throw std::runtime_error("图形路径点数量无效。");
                result.values[index * 2] = guides.get(point.attribute("x").value());
                result.values[index * 2 + 1] = guides.get(point.attribute("y").value());
                ++index;
            }
            if (index != points)
                throw std::runtime_error("图形路径缺少坐标。");
            if (points > 0)
            {
                x = result.values[points * 2 - 2];
                y = result.values[points * 2 - 1];
                if (result.action == Action::Move)
                {
                    start_x = x;
                    start_y = y;
                }
            }
            else
            {
                x = start_x;
                y = start_y;
            }
            path.commands.push_back(result);
        }
        for (const auto& command : path.commands)
            for (double value : command.values)
                bounded(value, 1e14);
        return path;
    }
}

namespace mirrorfly
{
    std::vector<std::string> presentation_geometry_presets()
    {
        std::vector<std::string> result;
        for (auto node : presets().document_element().children())
            result.emplace_back(node.name());
        return result;
    }

    PresentationGeometry presentation_geometry(
        const std::string& preset, double width, double height, const std::string& definition)
    {
        PresentationGeometry result;
        try
        {
            if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 ||
                width > 100000 || height > 100000 || definition.size() > 1024 * 1024)
                throw std::runtime_error("图形尺寸或定义超过限制。");
            pugi::xml_document document;
            Node local_geometry;
            if (!definition.empty())
            {
                if (definition.find("<!") != std::string::npos ||
                    !document.load_buffer(definition.data(), definition.size()))
                    throw std::runtime_error("图形定义 XML 无效。");
                local_geometry = document.document_element();
                for (auto node = local_geometry.next_sibling(); node; node = node.next_sibling())
                    if (node.type() == pugi::node_element)
                        throw std::runtime_error("图形定义包含多个根节点。");
                if (local(local_geometry.name()) != "prstGeom" && local(local_geometry.name()) != "custGeom")
                    throw std::runtime_error("图形定义类型无效。");
            }
            const bool custom = local_geometry && local(local_geometry.name()) == "custGeom";
            const auto geometry =
                custom ? local_geometry : presets().document_element().child(preset.c_str());
            if (!geometry)
                throw std::runtime_error("未支持的预设图形：" + preset);
            auto guides = initial_guides(std::max(width, 0.0001), std::max(height, 0.0001));
            std::size_t count = 0;
            guides.read(child(geometry, "avLst"), count, !custom);
            if (!custom)
                guides.read(child(local_geometry, "avLst"), count);
            guides.read(child(geometry, "gdLst"), count, !custom);
            result.width = width;
            result.height = height;
            result.text_rect = {0, 0, width, height};
            if (const auto rect = child(geometry, "rect"))
            {
                const char* names[]{"l", "t", "r", "b"};
                for (std::size_t index = 0; index < 4; ++index)
                    result.text_rect[index] =
                        bounded(guides.get(rect.attribute(names[index]).value()), 1e14) / emu_per_point;
            }
            count = 0;
            for (auto node : child(geometry, "pathLst").children())
            {
                if (result.paths.size() == 128 || local(node.name()) != "path")
                    throw std::runtime_error("图形路径数量或类型无效。");
                result.paths.push_back(read_path(node, guides, count));
            }
            if (result.paths.empty())
                throw std::runtime_error("图形没有可显示的路径。");
        }
        catch (const std::runtime_error& error)
        {
            result = {};
            result.error = error.what();
        }
        return result;
    }
}
