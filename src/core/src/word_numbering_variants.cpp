#include "word_numbering_variants.hpp"
#include "word_package.hpp"
#include "word_xml.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>

namespace mirrorfly::word_detail
{
    namespace
    {
        using namespace word_xml;
        pugi::xml_node identified(pugi::xml_node root, const char* name, const char* key, int id)
        {
            for (auto node : root.children())
                if (named(node, name) && attribute(node, key).as_int(-1) == id)
                    return node;
            return {};
        }
        void set_value(pugi::xml_node node, const char* name, const std::string& value)
        {
            auto field = attribute(node, name);
            if (!field)
                field = node.append_attribute(("w:" + std::string(name)).c_str());
            field = value.c_str();
        }
        pugi::xml_node level_property(pugi::xml_node level, const char* name)
        {
            if (auto node = child(level, name))
                return node;
            const std::vector<std::string> order{"start", "numFmt", "lvlRestart", "pStyle", "isLgl", "suff",
                "lvlText", "lvlPicBulletId", "legacy", "lvlJc", "pPr", "rPr"};
            const auto rank = std::find(order.begin(), order.end(), name);
            for (auto node : level.children())
                if (std::find(order.begin(), order.end(), std::string(local(node.name()))) > rank)
                    return level.insert_child_before(("w:" + std::string(name)).c_str(), node);
            return level.append_child(("w:" + std::string(name)).c_str());
        }
        std::string append_variant(
            std::vector<OfficePart>& parts, const WordParagraph& paragraph, int source_id, int& output)
        {
            bool allocated = false;
            if (source_id <= 0)
            {
                std::map<WordListKind, int> fresh;
                const auto error = append_numbering(parts, {paragraph.list}, fresh);
                if (!error.empty())
                    return error;
                source_id = fresh.at(paragraph.list);
                allocated = true;
            }
            std::string error;
            const auto path = numbering_path(parts, error);
            const auto source = part(parts, path);
            pugi::xml_document xml;
            if (!error.empty() || !source || !read(source->bytes, xml))
                return "无法读取列表编号定义。";
            auto root = xml.document_element();
            auto number = identified(root, "num", "numId", source_id);
            if (!number)
                return "无法定位原列表实例，未改写编号。";
            std::set<int> used;
            for (auto item : root.children())
                if (named(item, "num"))
                {
                    const auto id = attribute(item, "numId").as_int(-1);
                    if (id < 0 || !used.insert(id).second)
                        return "原编号实例标识无效或重复。";
                }
            if (!allocated)
            {
                int id = 1;
                while (used.count(id) && id < std::numeric_limits<int>::max())
                    ++id;
                if (used.count(id))
                    return "编号实例已达上限。";
                const auto anchor = child(root, "numIdMacAtCleanup");
                number = anchor ? root.insert_copy_before(number, anchor) : root.append_copy(number);
                set_value(number, "numId", std::to_string(id));
            }
            if (!number.attribute("xmlns:w"))
                number.append_attribute("xmlns:w") = main_ns;
            output = attribute(number, "numId").as_int();
            const int abstract_id = attribute(child(number, "abstractNumId"), "val").as_int(-1);
            const auto definition = identified(root, "abstractNum", "abstractNumId", abstract_id);
            auto override = identified(number, "lvlOverride", "ilvl", paragraph.list_level);
            if (!override)
            {
                override = number.append_child("w:lvlOverride");
                override.append_attribute("w:ilvl") = paragraph.list_level;
            }
            auto level = child(override, "lvl");
            if (!level)
            {
                const auto base = identified(definition, "lvl", "ilvl", paragraph.list_level);
                if (!base)
                    return "原编号缺少目标层级，未替换列表定义。";
                level = override.append_copy(base);
            }
            auto start = child(override, "startOverride");
            if (!start)
                start = override.prepend_child("w:startOverride");
            set_value(start, "val", std::to_string(paragraph.list_start));
            set_value(level_property(level, "start"), "val", std::to_string(paragraph.list_start));
            const bool bullet = paragraph.list == WordListKind::Bullet;
            const auto marker =
                paragraph.list_marker.empty() ? (bullet ? "disc" : "decimal") : paragraph.list_marker;
            set_value(level_property(level, "numFmt"), "val", bullet ? "bullet" : marker);
            auto text = paragraph.list_text;
            if (text.empty())
            {
                const auto glyph = marker == "circle" ? "◦" : (marker == "square" ? "▪" : "•");
                text = bullet ? glyph : "%" + std::to_string(paragraph.list_level + 1) + ".";
                if (bullet)
                {
                    auto properties = level_property(level, "rPr");
                    auto fonts = child(properties, "rFonts");
                    if (!fonts)
                        fonts = properties.prepend_child("w:rFonts");
                    for (const auto* key : {"asciiTheme", "hAnsiTheme", "eastAsiaTheme", "cstheme"})
                        fonts.remove_attribute(attribute(fonts, key));
                    for (const auto* key : {"ascii", "hAnsi", "eastAsia", "cs"})
                        set_value(fonts, key, "Arial");
                }
            }
            set_value(level_property(level, "lvlText"), "val", text);
            replace_part(parts, path, xml);
            return {};
        }
    }

    std::string numbering_variants(std::vector<OfficePart>& parts, const WordDocument& document,
        const WordDocument* original, std::map<std::size_t, int>& ids)
    {
        using Key = std::tuple<int, WordListKind, int, std::string, int, std::string>;
        std::map<Key, int> groups;
        for (std::size_t index = 0; index < document.paragraphs.size(); ++index)
        {
            const auto& paragraph = document.paragraphs[index];
            if (paragraph.list == WordListKind::None)
                continue;
            const auto origin = paragraph.source_id ? paragraph.source_id : paragraph.origin_id;
            const auto* previous = original && origin && origin <= original->paragraphs.size()
                ? &original->paragraphs[origin - 1]
                : nullptr;
            if (previous && paragraph.list == previous->list &&
                paragraph.list_marker == previous->list_marker &&
                paragraph.list_start == previous->list_start && paragraph.list_text == previous->list_text &&
                paragraph.list_instance == previous->list_instance)
                continue;
            if ((!previous || previous->list != paragraph.list) && paragraph.list_marker.empty() &&
                paragraph.list_start == 1 && paragraph.list_text.empty() && paragraph.list_instance == 0)
                continue;
            const Key key{paragraph.list_instance, paragraph.list, paragraph.list_level,
                paragraph.list_marker, paragraph.list_start, paragraph.list_text};
            auto found = groups.find(key);
            if (found == groups.end())
            {
                int id = 0;
                const auto error = append_variant(parts, paragraph,
                    previous && previous->list == paragraph.list ? previous->list_instance : 0, id);
                if (!error.empty())
                    return error;
                found = groups.emplace(key, id).first;
            }
            ids[index] = found->second;
        }
        return {};
    }

    std::string materialize_numbering(std::vector<OfficePart>& parts, const WordDocument& document)
    {
        std::map<std::size_t, int> ids;
        const auto error = numbering_variants(parts, document, nullptr, ids);
        if (!error.empty() || ids.empty())
            return error;
        pugi::xml_document xml;
        if (!read(part(parts, "word/document.xml")->bytes, xml))
            return "无法写入列表实例。";
        std::size_t index = 0;
        for (auto paragraph : child(xml.document_element(), "body").children())
            if (named(paragraph, "p"))
            {
                if (const auto found = ids.find(index); found != ids.end())
                    set_value(child(child(child(paragraph, "pPr"), "numPr"), "numId"), "val",
                        std::to_string(found->second));
                ++index;
            }
        replace_part(parts, "word/document.xml", xml);
        return {};
    }
}
