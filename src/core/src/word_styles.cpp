#include "word_styles.hpp"
#include "word_style_merge.hpp"
#include "word_xml.hpp"

#include <set>

namespace
{
    using namespace mirrorfly;
    using namespace mirrorfly::word_xml;
    constexpr auto drawing_ns = "http://schemas.openxmlformats.org/drawingml/2006/main";

    pugi::xml_node drawing_child(pugi::xml_node node, const char* name)
    {
        for (auto item : node.children())
            if (named(item, name, drawing_ns))
                return item;
        return {};
    }

    std::size_t cache_cost(pugi::xml_node node)
    {
        std::size_t result = 128 + std::string_view(node.name()).size();
        for (auto value : node.attributes())
            result += 128 + std::string_view(value.name()).size() + std::string_view(value.value()).size();
        for (auto item : node.children())
            result += cache_cost(item);
        return result;
    }

    bool related_xml(const std::vector<OfficePart>& parts, const char* type, const char* expected,
        const char* space, pugi::xml_document& xml, std::string& error)
    {
        const auto target = document_relationship(parts, type, error);
        if (!error.empty())
            return false;
        if (target.empty())
            return true;
        const auto source = part(parts, target);
        if (!source || !read(source->bytes, xml) || !named(xml.document_element(), expected, space))
        {
            error = std::string("DOCX ") + type + " 定义缺失或无效。";
            return false;
        }
        return true;
    }
}

namespace mirrorfly::word_detail
{
    bool StyleResolver::load(const std::vector<OfficePart>& parts, std::string& error)
    {
        error.clear();
        definitions_.clear();
        cache_.clear();
        cache_cost_ = 0;
        default_paragraph_.clear();
        default_character_.clear();
        default_table_.clear();
        tables_.clear();
        cell_count_ = 0;
        run_base_ = {};
        styles_.reset();
        theme_.reset();
        if (!related_xml(parts, "styles", "styles", main_ns, styles_, error) ||
            !related_xml(parts, "theme", "theme", drawing_ns, theme_, error))
            return false;
        for (auto node : styles_.document_element().children())
        {
            if (!named(node, "style"))
                continue;
            const std::string id = attribute(node, "styleId").value();
            if (id.empty() || id.size() > 256 || definitions_.size() >= 4096 ||
                !definitions_.emplace(id, node).second)
            {
                error = "DOCX 样式标识重复、无效或数量超过上限。";
                return false;
            }
            const std::string_view type = attribute(node, "type").value();
            const std::string_view value = attribute(node, "default").value();
            if (value == "1" || value == "true" || value == "on")
            {
                if (type == "paragraph")
                    default_paragraph_ = id;
                else if (type == "character")
                    default_character_ = id;
                else if (type == "table")
                    default_table_ = id;
            }
        }
        for (const auto& entry : definitions_)
        {
            std::set<std::string> seen;
            auto node = entry.second;
            const std::string type = attribute(node, "type").value();
            while (node)
            {
                const std::string id = attribute(node, "styleId").value();
                if (seen.size() >= 64 || !seen.insert(id).second)
                {
                    error = "DOCX 样式继承循环或深度超过 64 层。";
                    return false;
                }
                const auto found = definitions_.find(attribute(child(node, "basedOn"), "val").value());
                node = found == definitions_.end() ? pugi::xml_node{} : found->second;
                if (node && attribute(node, "type").value() != type)
                    break;
            }
        }
        return true;
    }

    bool StyleResolver::has_styles() const
    {
        return bool(styles_.document_element());
    }

    std::vector<pugi::xml_node> StyleResolver::chain(const std::string& id, const char* type) const
    {
        std::vector<pugi::xml_node> result;
        auto found = definitions_.find(id);
        while (found != definitions_.end() && result.size() < 64)
        {
            const auto node = found->second;
            if (std::string_view(attribute(node, "type").value()) != type)
                break;
            result.push_back(node);
            found = definitions_.find(attribute(child(node, "basedOn"), "val").value());
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    pugi::xml_node StyleResolver::paragraph_base(const std::string& requested, pugi::xml_node node)
    {
        const auto id = definitions_.count(requested) ? requested : default_paragraph_;
        const auto context = table_context(node);
        const auto key =
            std::to_string(id.size()) + ":" + id + context.first + ":" + std::to_string(context.second);
        const auto found = cache_.find(key);
        if (found != cache_.end())
            return found->second->document_element();
        auto xml = std::make_unique<pugi::xml_document>();
        auto base = property_root(*xml, "w:style");
        auto paragraph = base.append_child("w:pPr");
        auto run = base.append_child("w:rPr");
        const auto defaults = child(styles_.document_element(), "docDefaults");
        merge_properties(paragraph, child(child(defaults, "pPrDefault"), "pPr"), false);
        merge_properties(run, child(child(defaults, "rPrDefault"), "rPr"), false);
        merge_table_style(base, context.first, context.second);
        for (const auto style : chain(id, "paragraph"))
        {
            merge_properties(paragraph, child(style, "pPr"), false);
            merge_properties(run, child(style, "rPr"), true);
        }
        constexpr std::size_t budget = 8 * 1024 * 1024;
        const auto cost = cache_cost(base);
        if (cost > budget)
            throw std::bad_alloc{};
        if (cache_cost_ + cost > budget)
        {
            cache_.clear();
            cache_cost_ = 0;
        }
        cache_.emplace(key, std::move(xml));
        cache_cost_ += cost;
        return base;
    }

    pugi::xml_node StyleResolver::paragraph_properties(pugi::xml_node paragraph)
    {
        const auto direct = child(paragraph, "pPr");
        auto base = paragraph_base(attribute(child(direct, "pStyle"), "val").value(), paragraph);
        auto result = property_root(paragraph_, "w:pPr");
        merge_properties(result, child(base, "pPr"), false);
        merge_properties(result, direct, false);
        run_base_ = child(base, "rPr");
        resolve_colors(result);
        return result;
    }

    void StyleResolver::resolve_theme(pugi::xml_node properties) const
    {
        const auto elements = drawing_child(theme_.document_element(), "themeElements");
        const auto scheme = drawing_child(elements, "fontScheme");
        const auto fonts = child(properties, "rFonts");
        const auto language = std::string(attribute(child(properties, "lang"), "eastAsia").value());
        const std::pair<const char*, const char*> choices[] = {{"ascii", "asciiTheme"},
            {"hAnsi", "hAnsiTheme"}, {"eastAsia", "eastAsiaTheme"}, {"cs", "cstheme"}};
        for (const auto& choice : choices)
        {
            const std::string key = attribute(fonts, choice.second).value();
            if (key.substr(0, 5) != "major" && key.substr(0, 5) != "minor")
                continue;
            const auto family =
                drawing_child(scheme, key.substr(0, 5) == "major" ? "majorFont" : "minorFont");
            const char* part_name = "latin";
            if (key.substr(5) == "EastAsia")
                part_name = "ea";
            else if (key.substr(5) == "Bidi")
                part_name = "cs";
            std::string font = drawing_child(family, part_name).attribute("typeface").value();
            if (font.empty() && key.substr(5) == "EastAsia")
            {
                std::string script;
                if (language.substr(0, 2) == "zh")
                    script =
                        language == "zh-TW" || language == "zh-HK" || language == "zh-MO" ? "Hant" : "Hans";
                else if (language.substr(0, 2) == "ja")
                    script = "Jpan";
                else if (language.substr(0, 2) == "ko")
                    script = "Hang";
                for (auto item : family.children())
                    if (named(item, "font", drawing_ns) && !script.empty() &&
                        item.attribute("script").value() == script)
                        font = item.attribute("typeface").value();
            }
            if (!font.empty())
                set(fonts, choice.first, font);
        }
        resolve_colors(properties);
    }

    void StyleResolver::resolve_colors(pugi::xml_node properties) const
    {
        const auto resolve = [&](pugi::xml_node color, bool fill)
        {
            const auto rgb = theme_color(color, fill);
            if (!rgb.empty())
                set(color, fill ? "fill" : local(color.name()) == "color" ? "val" : "color", rgb.substr(1));
        };
        resolve(child(properties, "color"), false);
        resolve(child(properties, "shd"), true);
        resolve(child(properties, "shd"), false);
        resolve(child(properties, "bdr"), false);
        resolve(child(properties, "u"), false);
        for (auto edge : child(properties, "pBdr").children())
            resolve(edge, false);
    }

    pugi::xml_node StyleResolver::run_properties(pugi::xml_node run)
    {
        auto result = property_root(run_, "w:rPr");
        merge_properties(result, run_base_, false);
        const auto direct = child(run, "rPr");
        std::string id = attribute(child(direct, "rStyle"), "val").value();
        if (id.empty())
            id = default_character_;
        for (const auto style : chain(id, "character"))
            merge_properties(result, child(style, "rPr"), true);
        merge_properties(result, direct, false);
        resolve_theme(result);
        return result;
    }
}
