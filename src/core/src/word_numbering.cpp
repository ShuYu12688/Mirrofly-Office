#include "word_package.hpp"
#include "word_xml.hpp"

#include <charconv>
#include <set>

namespace
{
    using namespace mirrorfly;
    using namespace mirrorfly::word_xml;
    constexpr auto numbering_rel =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering";
    constexpr auto numbering_type =
        "application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml";
    constexpr auto rels_path = "word/_rels/document.xml.rels";

    int identifier(pugi::xml_attribute value)
    {
        const std::string_view text(value.value());
        int result = -1;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result < 0)
            return -1;
        return result;
    }

    int allocate(std::set<int>& used)
    {
        int id = 1;
        while (used.count(id))
            ++id;
        used.insert(id);
        return id;
    }

    bool read_rels(const std::vector<OfficePart>& parts, pugi::xml_document& xml)
    {
        if (const auto source = part(parts, rels_path))
            return read(source->bytes, xml) && named(xml.document_element(), "Relationships", rel_ns);
        xml.append_child("Relationships").append_attribute("xmlns") = rel_ns;
        return true;
    }

    std::string add_relationship(std::vector<OfficePart>& parts, const std::string& path)
    {
        pugi::xml_document xml;
        if (!read_rels(parts, xml))
            return "DOCX 编号关系 XML 无效。";
        auto root = xml.document_element();
        std::set<std::string> ids;
        for (auto node : root.children())
            if (named(node, "Relationship", rel_ns))
            {
                const std::string id = node.attribute("Id").value();
                if (id.empty() || !ids.insert(id).second)
                    return "DOCX 包含重复或空的关系标识，未改写列表。";
            }
        std::string id;
        for (std::size_t index = 1; id.empty() || ids.count(id); ++index)
            id = "mfNumbering" + std::to_string(index);
        auto relation = root.append_child("Relationship");
        relation.append_attribute("xmlns") = rel_ns;
        relation.append_attribute("Id") = id.c_str();
        relation.append_attribute("Type") = numbering_rel;
        relation.append_attribute("Target") = ("/" + path).c_str();
        replace_part(parts, rels_path, xml);
        return {};
    }

    std::string ensure_type(std::vector<OfficePart>& parts, const std::string& path)
    {
        const auto source = part(parts, "[Content_Types].xml");
        pugi::xml_document xml;
        if (!source || !read(source->bytes, xml) || !named(xml.document_element(), "Types", content_ns))
            return "DOCX 内容类型声明无效，未改写列表。";
        bool found = false;
        for (auto node : xml.document_element().children())
            if (named(node, "Override", content_ns) &&
                std::string(node.attribute("PartName").value()) == "/" + path)
            {
                if (found || std::string_view(node.attribute("ContentType").value()) != numbering_type)
                    return "DOCX 编号内容类型冲突，未改写列表。";
                found = true;
            }
        if (found)
            return {};
        auto node = xml.document_element().append_child("Override");
        node.append_attribute("xmlns") = content_ns;
        node.append_attribute("PartName") = ("/" + path).c_str();
        node.append_attribute("ContentType") = numbering_type;
        replace_part(parts, "[Content_Types].xml", xml);
        return {};
    }
}

namespace mirrorfly::word_detail
{
    std::string numbering_path(const std::vector<OfficePart>& parts, std::string& error)
    {
        return document_relationship(parts, "numbering", error);
    }

    std::string append_numbering(
        std::vector<OfficePart>& parts, const std::set<WordListKind>& kinds, std::map<WordListKind, int>& ids)
    {
        ids.clear();
        if (kinds.empty())
            return {};
        std::string error;
        auto path = numbering_path(parts, error);
        if (!error.empty())
            return error;
        const bool create = path.empty();
        pugi::xml_document xml;
        if (create)
        {
            path = "word/numbering.xml";
            for (std::size_t index = 1; part(parts, path); ++index)
                path = "word/mirrorfly-numbering-" + std::to_string(index) + ".xml";
            xml.append_child("w:numbering").append_attribute("xmlns:w") = main_ns;
        }
        else
        {
            const auto source = part(parts, path);
            if (!source || !read(source->bytes, xml) || !named(xml.document_element(), "numbering"))
                return "原 DOCX 编号定义缺失或损坏，未替换原定义。";
        }
        auto root = xml.document_element();
        std::set<int> abstract_ids, concrete_ids;
        for (auto node : root.children())
        {
            const bool abstract = named(node, "abstractNum");
            if (!abstract && !named(node, "num"))
                continue;
            const int id = identifier(attribute(node, abstract ? "abstractNumId" : "numId"));
            auto& used = abstract ? abstract_ids : concrete_ids;
            if (id < 0 || !used.insert(id).second)
                return "原 DOCX 编号定义含无效或重复标识，未改写列表。";
        }
        const auto generated = serialize_text(WordDocument{});
        const auto source = part(generated.parts, "word/numbering.xml");
        pugi::xml_document defaults;
        if (!source || !read(source->bytes, defaults))
            return "无法创建列表定义。";
        for (const auto kind : kinds)
        {
            if (kind != WordListKind::Bullet && kind != WordListKind::Numbered)
                return "不支持的列表类型。";
            const int abstract_id = allocate(abstract_ids);
            const int concrete_id = allocate(concrete_ids);
            const int template_id = kind == WordListKind::Bullet ? 0 : 1;
            pugi::xml_node definition;
            for (auto node : defaults.document_element().children())
                if (named(node, "abstractNum") && identifier(attribute(node, "abstractNumId")) == template_id)
                    definition = node;
            auto anchor = child(root, "num");
            if (!anchor)
                anchor = child(root, "numIdMacAtCleanup");
            auto added = anchor ? root.insert_copy_before(definition, anchor) : root.append_copy(definition);
            added.append_attribute("xmlns:w") = main_ns;
            added.attribute("w:abstractNumId") = abstract_id;
            anchor = child(root, "numIdMacAtCleanup");
            auto number = anchor ? root.insert_child_before("w:num", anchor) : root.append_child("w:num");
            number.append_attribute("xmlns:w") = main_ns;
            number.append_attribute("w:numId") = concrete_id;
            number.append_child("w:abstractNumId").append_attribute("w:val") = abstract_id;
            ids[kind] = concrete_id;
        }
        if (create)
            error = add_relationship(parts, path);
        if (error.empty())
            error = ensure_type(parts, path);
        if (!error.empty())
            return error;
        replace_part(parts, path, xml);
        return {};
    }
}
