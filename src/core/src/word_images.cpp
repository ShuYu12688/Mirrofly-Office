#include "word_images.hpp"
#include "word_xml.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <string_view>

namespace
{
    constexpr auto drawing_ns = "http://schemas.openxmlformats.org/drawingml/2006/main";
    constexpr auto picture_ns = "http://schemas.openxmlformats.org/drawingml/2006/picture";
    constexpr auto word_drawing_ns = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";
    constexpr auto relationship_ns = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    constexpr auto vml_ns = "urn:schemas-microsoft-com:vml";

    pugi::xml_node descendant(pugi::xml_node node, const char* name, const char* space)
    {
        if (mirrorfly::word_xml::named(node, name, space))
            return node;
        for (auto item : node.children())
            if (auto found = descendant(item, name, space))
                return found;
        return {};
    }

    std::string relationship(pugi::xml_node node, const char* name)
    {
        for (auto item : node.attributes())
            if (std::string_view(item.name()).find(':') != std::string_view::npos &&
                mirrorfly::word_xml::local(item.name()) == name &&
                mirrorfly::word_xml::namespace_is(node, item.name(), relationship_ns))
                return item.value();
        return {};
    }

    std::string_view trim(std::string_view value)
    {
        const auto start = value.find_first_not_of(" \t\r\n");
        if (start == value.npos)
            return {};
        return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
    }

    std::string lower(std::string_view value)
    {
        std::string result(value);
        for (auto& character : result)
            if (character >= 'A' && character <= 'Z')
                character += 'a' - 'A';
        return result;
    }

    struct Number
    {
        double value = 0;
        std::string unit;
    };

    std::optional<Number> number(std::string_view text)
    {
        text = trim(text);
        if (text.empty() || text.size() > 128)
            return {};
        if (text.front() == '+')
            text.remove_prefix(1);
        double value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr == text.data() || !std::isfinite(value))
            return {};
        return Number{value, lower(trim(text.substr(static_cast<std::size_t>(parsed.ptr - text.data()))))};
    }

    double bounded(double value, double minimum, double maximum, bool& approximate)
    {
        const auto result = std::clamp(value, minimum, maximum);
        approximate = approximate || result != value;
        return result;
    }

    double dimension(std::string_view text, bool grouped, bool& approximate)
    {
        const auto parsed = number(text);
        static const std::map<std::string, double> units{{"", 0.75}, {"px", 0.75}, {"pt", 1}, {"in", 72},
            {"cm", 72 / 2.54}, {"mm", 72 / 25.4}, {"pc", 12}};
        if (parsed && parsed->value > 0 && !(grouped && parsed->unit.empty()))
        {
            const auto unit = units.find(parsed->unit);
            if (unit != units.end())
                return bounded(parsed->value * unit->second, 1, 3168, approximate);
        }
        approximate = true;
        return 100;
    }

    double scalar(std::string_view text, double divisor, bool& approximate)
    {
        if (text.empty())
            return 0;
        const auto parsed = number(text);
        if (parsed && parsed->unit.empty())
            return parsed->value / divisor;
        approximate = true;
        return 0;
    }

    double fraction(std::string_view text, bool& approximate)
    {
        if (text.empty())
            return 0;
        const auto parsed = number(text);
        if (parsed && (parsed->unit.empty() || parsed->unit == "f" || parsed->unit == "%"))
        {
            const auto divisor = parsed->unit == "f" ? 65536.0 : parsed->unit == "%" ? 100.0 : 1.0;
            return bounded(parsed->value / divisor, -1, 1, approximate);
        }
        approximate = true;
        return 0;
    }

    void read_vml(pugi::xml_node data, mirrorfly::WordImageReference& result)
    {
        const auto shape = data.parent();
        bool grouped = false;
        for (auto parent = shape.parent(); parent; parent = parent.parent())
            grouped = grouped || mirrorfly::word_xml::named(parent, "group", vml_ns);
        std::map<std::string, std::string> style;
        std::string_view text = shape.attribute("style").value();
        if (text.size() > 8192)
        {
            text = {};
            result.approximate = true;
        }
        for (int count = 0; !text.empty() && count < 128; ++count)
        {
            const auto end = text.find(';');
            const auto item = text.substr(0, end);
            const auto colon = item.find(':');
            if (colon != item.npos)
                style[lower(trim(item.substr(0, colon)))] = lower(trim(item.substr(colon + 1)));
            text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        }
        result.approximate = result.approximate || !text.empty();
        auto& image = result.image;
        image.width = dimension(style["width"], grouped, result.approximate);
        image.height = dimension(style["height"], grouped, result.approximate);
        const char* edges[] = {"cropleft", "croptop", "cropright", "cropbottom"};
        for (std::size_t edge = 0; edge < image.crop.size(); ++edge)
            image.crop[edge] = fraction(data.attribute(edges[edge]).value(), result.approximate);
        const auto flip = style["flip"];
        image.flip_horizontal = flip == "x" || flip == "xy" || flip == "yx";
        image.flip_vertical = flip == "y" || flip == "xy" || flip == "yx";
        if (const auto rotation = number(style["rotation"]);
            rotation && (rotation->unit.empty() || rotation->unit == "deg"))
        {
            image.rotation = std::remainder(rotation->value, 360.0);
            // VML flips after rotating; the shared image model flips before rotating.
            if (image.flip_horizontal != image.flip_vertical)
                image.rotation = -image.rotation;
        }
        else if (!style["rotation"].empty())
            result.approximate = true;
        image.anchored = style["position"] == "absolute";
        image.behind_text = image.anchored && scalar(style["z-index"], 1, result.approximate) < 0;
        image.description = shape.attribute("alt").value();
    }

    void read_drawing(pugi::xml_node object, pugi::xml_node blip, mirrorfly::WordImageReference& result)
    {
        auto picture = blip.parent();
        while (picture.parent() && !mirrorfly::word_xml::named(picture, "pic", picture_ns))
            picture = picture.parent();
        if (!mirrorfly::word_xml::named(picture, "pic", picture_ns))
            picture = object;
        auto& image = result.image;
        const auto extent = descendant(object, "extent", word_drawing_ns);
        const auto size = [&](const char* name)
        {
            const auto parsed = number(extent.attribute(name).value());
            if (!parsed || !parsed->unit.empty() || parsed->value <= 0)
            {
                result.approximate = true;
                return 100.0;
            }
            return bounded(parsed->value / 12700, 1, 3168, result.approximate);
        };
        image.width = size("cx");
        image.height = size("cy");
        const auto crop = descendant(picture, "srcRect", drawing_ns);
        const char* edges[] = {"l", "t", "r", "b"};
        for (std::size_t edge = 0; edge < image.crop.size(); ++edge)
            image.crop[edge] =
                bounded(scalar(crop.attribute(edges[edge]).value(), 100000, result.approximate), -1, 1,
                    result.approximate);
        const auto transform = descendant(picture, "xfrm", drawing_ns);
        image.rotation =
            std::remainder(scalar(transform.attribute("rot").value(), 60000, result.approximate), 360.0);
        image.flip_horizontal = transform.attribute("flipH").as_bool();
        image.flip_vertical = transform.attribute("flipV").as_bool();
        const auto anchor = descendant(object, "anchor", word_drawing_ns);
        image.anchored = bool(anchor);
        image.behind_text = anchor.attribute("behindDoc").as_bool();
        image.description = descendant(object, "docPr", word_drawing_ns).attribute("descr").value();
    }
}

namespace mirrorfly
{
    std::string validate_word_image_placement(const WordDocument& original, const WordDocument& current)
    {
        constexpr auto error = "暂不支持删除、复制或跨段移动原文档图片。原始对象已保留。";
        const auto identities = [](const WordParagraph& paragraph)
        {
            std::vector<std::uint64_t> result;
            for (const auto& run : paragraph.runs)
                if (run.image_id)
                    result.push_back(run.image_id);
            return result;
        };
        std::vector<bool> retained(original.paragraphs.size());
        for (const auto& paragraph : current.paragraphs)
        {
            if (paragraph.source_id && paragraph.source_id <= retained.size())
                retained[paragraph.source_id - 1] = true;
            const auto before = paragraph.source_id && paragraph.source_id <= original.paragraphs.size()
                ? identities(original.paragraphs[paragraph.source_id - 1])
                : std::vector<std::uint64_t>{};
            if (before != identities(paragraph))
                return error;
        }
        for (std::size_t index = 0; index < original.paragraphs.size(); ++index)
            if (!retained[index] && !identities(original.paragraphs[index]).empty())
                return error;
        return {};
    }

    WordImageReference read_word_image(pugi::xml_node object)
    {
        WordImageReference result;
        const auto blip = descendant(object, "blip", drawing_ns);
        result.relationship = relationship(blip, "embed");
        if (!result.relationship.empty())
            read_drawing(object, blip, result);
        else
        {
            const auto data = descendant(object, "imagedata", vml_ns);
            result.relationship = relationship(data, "id");
            if (!result.relationship.empty())
                read_vml(data, result);
        }
        for (std::size_t edge = 0; edge < 2; ++edge)
            if (result.image.crop[edge] + result.image.crop[edge + 2] >= 1)
            {
                result.image.crop[edge] = result.image.crop[edge + 2] = 0;
                result.approximate = true;
            }
        return result;
    }
}
