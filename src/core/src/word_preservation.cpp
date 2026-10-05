#include "word_cell_preservation.hpp"
#include "word_numbering_variants.hpp"
#include "word_package.hpp"
#include "word_property_patch.hpp"
#include "word_xml.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>

namespace
{
    constexpr auto word_ns = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

    std::string local(const char* name)
    {
        const std::string value(name);
        const auto colon = value.find(':');
        return colon == value.npos ? value : value.substr(colon + 1);
    }

    bool word_node(pugi::xml_node node)
    {
        const std::string name(node.name());
        const auto colon = name.find(':');
        const auto key = colon == name.npos ? std::string("xmlns") : "xmlns:" + name.substr(0, colon);
        for (; node; node = node.parent())
            if (const auto declared = node.attribute(key.c_str()))
                return std::string_view(declared.value()) == word_ns;
        return false;
    }

    pugi::xml_node child(pugi::xml_node node, const std::string& name)
    {
        for (auto item : node.children())
            if (local(item.name()) == name && word_node(item))
                return item;
        return {};
    }

    std::string bytes(pugi::xml_node node)
    {
        if (!node)
            return {};
        std::ostringstream stream;
        node.print(stream, "", pugi::format_raw);
        return stream.str();
    }

    pugi::xml_node locate(pugi::xml_document& xml, const std::vector<std::size_t>& path)
    {
        pugi::xml_node node = xml;
        for (const auto index : path)
        {
            node = node.first_child();
            for (std::size_t offset = 0; node && offset < index; ++offset)
                node = node.next_sibling();
            if (!node)
                break;
        }
        return node;
    }

    std::vector<pugi::xml_node> children(pugi::xml_node parent, const char* name)
    {
        std::vector<pugi::xml_node> result;
        for (auto node : parent.children())
            if (local(node.name()) == name && word_node(node))
                result.push_back(node);
        return result;
    }

    bool text_child(pugi::xml_node node)
    {
        const auto name = local(node.name());
        return word_node(node) && (name == "t" || name == "tab" || name == "br" || name == "cr");
    }

    std::string prefix(pugi::xml_node root)
    {
        std::set<std::string> shadowed;
        std::vector<pugi::xml_node> pending{root};
        while (!pending.empty())
        {
            const auto node = pending.back();
            pending.pop_back();
            for (auto attribute : node.attributes())
            {
                const std::string_view name(attribute.name());
                if (name.substr(0, 6) == "xmlns:" && std::string_view(attribute.value()) != word_ns)
                    shadowed.insert(std::string(name.substr(6)));
            }
            for (auto item : node.children())
                pending.push_back(item);
        }
        for (int attempt = 0;; ++attempt)
        {
            const auto value = attempt == 0 ? std::string("w") : "mfword" + std::to_string(attempt);
            if (shadowed.count(value))
                continue;
            const auto key = "xmlns:" + value;
            const auto declared = root.attribute(key.c_str());
            if (declared && std::string_view(declared.value()) != word_ns)
                continue;
            if (!declared)
                root.append_attribute(key.c_str()) = word_ns;
            return value;
        }
    }

    void inherit_namespaces(pugi::xml_node source, pugi::xml_node target)
    {
        std::set<std::string> seen;
        for (auto node = source.parent(); node; node = node.parent())
            for (auto attribute : node.attributes())
            {
                const std::string name(attribute.name());
                if ((name != "xmlns" && name.substr(0, 6) != "xmlns:") || !seen.insert(name).second ||
                    target.attribute(name.c_str()))
                    continue;
                pugi::xml_attribute inherited;
                for (auto ancestor = target.parent(); ancestor && !inherited; ancestor = ancestor.parent())
                    inherited = ancestor.attribute(name.c_str());
                if (!inherited || std::string_view(inherited.value()) != attribute.value())
                    target.append_attribute(name.c_str()) = attribute.value();
            }
    }

    void rename_prefix(pugi::xml_node node, const std::string& target)
    {
        if (std::string_view(node.name()).substr(0, 2) == "w:")
            node.set_name((target + ":" + local(node.name())).c_str());
        for (auto attribute : node.attributes())
            if (std::string_view(attribute.name()).substr(0, 2) == "w:")
                attribute.set_name((target + ":" + local(attribute.name())).c_str());
        for (auto item : node.children())
            rename_prefix(item, target);
    }

    pugi::xml_node ordered_copy(pugi::xml_node target, pugi::xml_node next, const char* kind)
    {
        return mirrorfly::word_detail::ordered_property(target, next, kind);
    }

    void properties(pugi::xml_node raw, pugi::xml_node before, pugi::xml_node after, const char* kind,
        const std::string& target_prefix, const std::string& skip = {})
    {
        mirrorfly::word_detail::patch_properties(raw, before, after, kind, target_prefix, skip);
    }

    bool patch_runs(pugi::xml_node raw, pugi::xml_node baseline,
        const std::vector<pugi::xml_node>& replacements, const std::string& target_prefix)
    {
        if (replacements.size() == 1 && bytes(baseline) == bytes(replacements.front()))
            return true;
        // Compound runs must retain their object and field ordering.
        for (auto node : raw.children())
            if (!(word_node(node) && local(node.name()) == "rPr") && !text_child(node))
                return false;
        auto parent = raw.parent();
        for (const auto replacement : replacements)
        {
            auto next = parent.insert_copy_before(raw, raw);
            for (auto node = next.first_child(); node;)
            {
                const auto following = node.next_sibling();
                if (text_child(node))
                    next.remove_child(node);
                node = following;
            }
            properties(next, child(baseline, "rPr"), child(replacement, "rPr"), "rPr", target_prefix);
            for (auto text : replacement.children())
                if (text_child(text))
                    rename_prefix(next.append_copy(text), target_prefix);
        }
        parent.remove_child(raw);
        return true;
    }

    mirrorfly::WordResult failure(const std::string& error)
    {
        mirrorfly::WordResult result;
        result.error = error;
        return result;
    }

    template <typename T, typename Predicate>
    bool same_items(const std::vector<T>& left, const std::vector<T>& right, Predicate equal)
    {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), equal);
    }

    bool same_blocks(
        const std::vector<mirrorfly::WordBlock>& left, const std::vector<mirrorfly::WordBlock>& right)
    {
        return same_items(left, right, [](const auto& a, const auto& b)
        {
            return a.kind == b.kind && a.index == b.index;
        });
    }

    bool same_structure(const mirrorfly::WordDocument& left, const mirrorfly::WordDocument& right)
    {
        const bool images_equal = same_items(left.images, right.images, [](const auto& a, const auto& b)
        {
            const auto first = std::tie(a.id, a.path, a.mime_type, a.description, a.bytes, a.width, a.height,
                a.crop, a.rotation, a.flip_horizontal, a.flip_vertical, a.anchored, a.behind_text);
            const auto second = std::tie(b.id, b.path, b.mime_type, b.description, b.bytes, b.width, b.height,
                b.crop, b.rotation, b.flip_horizontal, b.flip_vertical, b.anchored, b.behind_text);
            return first == second;
        });
        return images_equal &&
            same_items(left.sections, right.sections,
                [](const auto& a, const auto& b)
        {
            const auto first = std::tie(a.width, a.height, a.margins, a.continuous);
            const auto second = std::tie(b.width, b.height, b.margins, b.continuous);
            return first == second;
        }) &&
            same_items(left.tables, right.tables, [](const auto& a, const auto& b)
        {
            const auto first = std::tie(a.source_id, a.column_widths, a.rows, a.header_rows, a.left_indent,
                a.border_color, a.border_width, a.borders, a.right_to_left, a.border_layout_supported);
            const auto second = std::tie(b.source_id, b.column_widths, b.rows, b.header_rows, b.left_indent,
                b.border_color, b.border_width, b.borders, b.right_to_left, b.border_layout_supported);
            return first == second &&
                same_items(a.cells, b.cells, [](const auto& c, const auto& d)
            {
                const auto first_cell = std::tie(c.row, c.column, c.row_span, c.column_span);
                const auto second_cell = std::tie(d.row, d.column, d.row_span, d.column_span);
                return first_cell == second_cell && c.border_rows.size() == d.border_rows.size();
            });
        });
    }

    bool same_cell_styles(const mirrorfly::WordDocument& left, const mirrorfly::WordDocument& right)
    {
        return same_items(left.tables, right.tables, [](const auto& a, const auto& b)
        {
            return same_items(a.cells, b.cells, [](const auto& c, const auto& d)
            {
                return std::tie(c.background, c.vertical_alignment, c.margins, c.border_rows) ==
                    std::tie(d.background, d.vertical_alignment, d.margins, d.border_rows);
            });
        });
    }

    bool simple_run(pugi::xml_node raw)
    {
        return mirrorfly::word_xml::plain_run(raw);
    }

    bool simple_paragraph(pugi::xml_node raw, bool allow_section = false)
    {
        return mirrorfly::word_xml::plain_paragraph(raw, allow_section);
    }

    std::set<std::size_t> referenced_paragraphs(const mirrorfly::WordDocument& document, bool& unique)
    {
        std::set<std::size_t> result;
        const auto collect = [&](const auto& flow)
        {
            for (const auto& block : flow)
                if (block.kind == mirrorfly::WordBlock::Kind::Paragraph && !result.insert(block.index).second)
                    unique = false;
        };
        collect(document.blocks);
        for (const auto& table : document.tables)
            for (const auto& cell : table.cells)
                collect(cell.blocks);
        return result;
    }

    bool rebuild_runs(pugi::xml_node raw, const mirrorfly::WordParagraph& paragraph, pugi::xml_node canonical,
        const std::map<std::uint64_t, pugi::xml_node>& source_runs,
        const std::map<std::uint64_t, pugi::xml_node>& baseline_runs, const std::string& target_prefix)
    {
        for (auto node = raw.first_child(); node;)
        {
            const auto next = node.next_sibling();
            if (word_node(node) && local(node.name()) == "r")
                raw.remove_child(node);
            node = next;
        }
        const auto desired = children(canonical, "r");
        if (desired.size() != paragraph.runs.size())
            return false;
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            const auto& run = paragraph.runs[index];
            if (run.image_id)
                return false;
            if (!run.source_id)
            {
                rename_prefix(raw.append_copy(desired[index]), target_prefix);
                continue;
            }
            const auto source = source_runs.find(run.source_id);
            const auto before = baseline_runs.find(run.source_id);
            if (source == source_runs.end() || before == baseline_runs.end() || !simple_run(source->second))
                return false;
            auto next = raw.append_copy(source->second);
            // Copied nodes may have inherited their prefix bindings from a removed paragraph.
            inherit_namespaces(source->second, next);
            properties(
                next, child(before->second, "rPr"), child(desired[index], "rPr"), "rPr", target_prefix);
            for (auto item = next.first_child(); item;)
            {
                const auto following = item.next_sibling();
                if (text_child(item))
                    next.remove_child(item);
                item = following;
            }
            for (auto item : desired[index].children())
                if (text_child(item))
                    rename_prefix(next.append_copy(item), target_prefix);
        }
        return true;
    }

    std::string reconcile_flow(const mirrorfly::WordDocument& document,
        const mirrorfly::WordDocument& original, const std::vector<mirrorfly::WordBlock>& current,
        const std::vector<mirrorfly::WordBlock>& previous, pugi::xml_node container,
        const std::vector<pugi::xml_node>& paragraphs, const std::vector<pugi::xml_node>& old_paragraphs,
        const std::vector<pugi::xml_node>& tables, std::set<std::size_t>& used)
    {
        using Kind = mirrorfly::WordBlock::Kind;
        using Key = std::pair<Kind, std::uint64_t>;
        std::map<Key, std::size_t> positions;
        std::vector<std::size_t> old_tables, new_tables;
        for (std::size_t index = 0; index < previous.size(); ++index)
        {
            const auto& block = previous[index];
            std::uint64_t id = 0;
            if (block.kind == Kind::Paragraph)
                id = original.paragraphs[block.index].source_id;
            else
                id = original.tables[block.index].source_id;
            positions[{block.kind, id}] = index;
            if (block.kind == Kind::Table)
                old_tables.push_back(block.index);
        }
        std::vector<pugi::xml_node> targets;
        bool changed = previous.size() != current.size();
        std::size_t last = 0;
        bool first = true;
        for (std::size_t index = 0; index < current.size(); ++index)
        {
            const auto& block = current[index];
            std::uint64_t id = 0;
            if (block.kind == Kind::Paragraph)
                id = document.paragraphs[block.index].source_id;
            else
                id = document.tables[block.index].source_id;
            if (block.kind == Kind::Paragraph && !used.insert(block.index).second)
                return "同一段落被多个容器引用，未进行写回。";
            if (block.kind == Kind::Table)
                new_tables.push_back(block.index);
            if (id)
            {
                const auto position = positions.find({block.kind, id});
                if (position == positions.end() || (!first && position->second <= last))
                    return "段落跨越了原容器或顺序不明确，未移动受保护结构。";
                last = position->second;
                first = false;
                changed = changed || last != index;
            }
            else
                changed = true;
            targets.push_back(block.kind == Kind::Paragraph ? paragraphs[block.index] : tables[block.index]);
        }
        if (old_tables != new_tables)
            return "表格增删或移动尚未开放，原表格已保留。";
        if (!changed)
            return {};
        for (const auto& block : previous)
        {
            const auto node =
                block.kind == Kind::Paragraph ? old_paragraphs[block.index] : tables[block.index];
            if (node.parent() != container)
                return "此区域包含内容控件或其他嵌套结构，暂不能安全分段或合并。";
        }
        for (std::size_t index = 0; index < current.size(); ++index)
        {
            const auto& block = current[index];
            if (block.kind != Kind::Paragraph || document.paragraphs[block.index].source_id)
                continue;
            pugi::xml_node anchor;
            for (std::size_t following = index + 1; following < targets.size(); ++following)
                if (targets[following].parent() == container)
                {
                    anchor = targets[following];
                    break;
                }
            if (!anchor)
                anchor = child(container, "sectPr");
            if (anchor)
                container.insert_move_before(targets[index], anchor);
            else
                container.append_move(targets[index]);
        }
        return {};
    }
}

namespace mirrorfly::word_detail
{
    WordResult serialize_preserved(const WordDocument& document)
    {
        const auto& package = *document.source_package;
        if (document.default_tab_stop != package.original.default_tab_stop)
            return failure("导入文档的默认制表间距需保留，当前支持编辑段落自定义制表位。");
        if (!same_structure(document, package.original))
            return failure("表格、图片或分节结构写回尚未完成，未丢弃结构改动。");
        auto candidate = document;
        candidate.source_package.reset();
        for (auto& paragraph : candidate.paragraphs)
        {
            const auto id = paragraph.source_id ? paragraph.source_id : paragraph.origin_id;
            if (!id || id > package.original.paragraphs.size())
                continue;
            const auto& previous = package.original.paragraphs[id - 1];
            if (paragraph.list != previous.list && paragraph.list_marker == previous.list_marker &&
                paragraph.list_text == previous.list_text && paragraph.list_start == previous.list_start)
            {
                // Legacy callers change only the kind; do not carry a bullet glyph into numeric formats.
                paragraph.list_marker.clear();
                paragraph.list_text.clear();
                paragraph.list_start = 1;
                paragraph.list_instance = 0;
            }
        }
        const auto current = serialize_text(candidate, false);
        const auto baseline = serialize_text(package.original, false);
        if (!current.success || !baseline.success)
            return failure(current.success ? baseline.error : current.error);
        if (const auto error = validate_word_image_placement(package.original, candidate); !error.empty())
            return failure(error);
        std::set<std::uint64_t> retained;
        bool same_order = candidate.paragraphs.size() == package.original.paragraphs.size();
        bool same_lists = same_order;
        for (std::size_t index = 0; index < candidate.paragraphs.size(); ++index)
        {
            const auto& new_paragraph = candidate.paragraphs[index];
            const auto id = new_paragraph.source_id;
            if (id > package.original.paragraphs.size() ||
                new_paragraph.origin_id > package.original.paragraphs.size() ||
                (id && !retained.insert(id).second))
                return failure("段落标识未知或重复，未覆盖原结构。");
            same_order = same_order && id == index + 1;
            if (id)
            {
                const auto& old = package.original.paragraphs[id - 1];
                same_lists = same_lists && new_paragraph.list == old.list &&
                    new_paragraph.list_level == old.list_level &&
                    new_paragraph.list_instance == old.list_instance &&
                    new_paragraph.list_marker == old.list_marker &&
                    new_paragraph.list_start == old.list_start && new_paragraph.list_text == old.list_text;
            }
            else
                same_lists = false;
        }
        bool unique = true;
        const auto old_visible = referenced_paragraphs(package.original, unique);
        const auto new_visible = referenced_paragraphs(candidate, unique);
        if (!unique)
            return failure("同一段落被多个容器引用，未进行写回。");
        for (std::size_t index = 0; index < candidate.paragraphs.size(); ++index)
        {
            const auto id = candidate.paragraphs[index].source_id;
            if (!new_visible.count(index) && (!id || old_visible.count(id - 1)))
                return failure("正文段落缺少容器归属，未保存悬空内容。");
        }
        for (std::size_t index = 0; index < package.original.paragraphs.size(); ++index)
            if (!old_visible.count(index) && !retained.count(index + 1))
                return failure("合并单元格的保留段落不能隐式删除。");
        std::size_t section_begin = 0;
        for (std::size_t section = 0; section < candidate.sections.size(); ++section)
        {
            std::size_t section_end = candidate.paragraphs.size();
            if (section + 1 < candidate.sections.size())
            {
                const auto& old = package.original.sections[section];
                const auto boundary = old.first_paragraph + old.paragraph_count;
                const auto found = std::find_if(candidate.paragraphs.begin(), candidate.paragraphs.end(),
                    [boundary](const auto& paragraph)
                {
                    return paragraph.source_id == boundary;
                });
                if (found == candidate.paragraphs.end())
                    return failure("不能删除或合并分节边界。");
                section_end = static_cast<std::size_t>(found - candidate.paragraphs.begin()) + 1;
            }
            const auto& current_section = candidate.sections[section];
            if (section_end < section_begin || current_section.first_paragraph != section_begin ||
                current_section.paragraph_count != section_end - section_begin)
                return failure("分节段落范围与正文顺序不一致，未写回不确定布局。");
            section_begin = section_end;
        }
        bool same_flow = same_blocks(document.blocks, package.original.blocks);
        for (std::size_t table = 0; table < document.tables.size(); ++table)
            for (std::size_t cell = 0; cell < document.tables[table].cells.size(); ++cell)
                same_flow = same_flow &&
                    same_blocks(document.tables[table].cells[cell].blocks,
                        package.original.tables[table].cells[cell].blocks);
        if (same_order && same_lists && same_flow && same_cell_styles(document, package.original) &&
            current.parts.back().bytes == baseline.parts.back().bytes)
        {
            WordResult result;
            result.parts = *package.parts;
            result.success = true;
            return result;
        }
        pugi::xml_document original_xml, source_xml, before_xml, after_xml;
        const auto part = std::find_if(package.parts->begin(), package.parts->end(), [](const auto& item)
        {
            return item.path == "word/document.xml";
        });
        if (part == package.parts->end() ||
            !original_xml.load_buffer(part->bytes.data(), part->bytes.size(), word_xml::parse_options) ||
            !before_xml.load_string(baseline.parts.back().bytes.c_str(), word_xml::parse_options) ||
            !after_xml.load_string(current.parts.back().bytes.c_str(), word_xml::parse_options))
            return failure("无法定位原 DOCX 正文，当前草稿已保留。");
        const auto before = children(child(before_xml.document_element(), "body"), "p");
        const auto after = children(child(after_xml.document_element(), "body"), "p");
        if (after.size() != candidate.paragraphs.size() || before.size() != package.paragraph_paths.size())
            return failure("原 DOCX 段落定位不一致，未进行不安全写回。");
        source_xml.reset(original_xml);
        std::vector<pugi::xml_node> old_paragraphs, paragraphs, tables;
        for (const auto& path : package.paragraph_paths)
            old_paragraphs.push_back(locate(original_xml, path));
        for (const auto& path : package.table_paths)
            tables.push_back(locate(original_xml, path));
        const auto body = locate(original_xml, package.body_path);
        std::vector<std::vector<pugi::xml_node>> cells;
        for (const auto& table : package.cell_paths)
        {
            cells.emplace_back();
            for (const auto& path : table)
                cells.back().push_back(locate(original_xml, path));
        }
        std::map<std::uint64_t, pugi::xml_node> runs, source_runs, baseline_runs;
        for (const auto& entry : package.run_paths)
        {
            runs[entry.first] = locate(original_xml, entry.second);
            source_runs[entry.first] = locate(source_xml, entry.second);
        }
        for (std::size_t index = 0; index < before.size(); ++index)
        {
            const auto canonical_runs = children(before[index], "r");
            const auto& values = package.original.paragraphs[index].runs;
            if (canonical_runs.size() != values.size())
                return failure("原段落的文字定位不一致。");
            for (std::size_t run = 0; run < values.size(); ++run)
                if (!values[run].image_id)
                    baseline_runs[values[run].source_id] = canonical_runs[run];
            if (!retained.count(index + 1) && !simple_paragraph(old_paragraphs[index]))
                return failure("删除或合并涉及域、书签、图片或分节标记，未破坏原结构。");
        }
        const auto target_prefix = prefix(original_xml.document_element());
        for (std::size_t table = 0; table < candidate.tables.size(); ++table)
            for (std::size_t cell = 0; cell < candidate.tables[table].cells.size(); ++cell)
            {
                const auto& previous = package.original.tables[table].cells[cell];
                const auto& next = candidate.tables[table].cells[cell];
                if (std::tie(previous.background, previous.vertical_alignment, previous.margins,
                        previous.border_rows) ==
                    std::tie(next.background, next.vertical_alignment, next.margins, next.border_rows))
                    continue;
                for (std::size_t row = 0; row < next.border_rows.size(); ++row)
                    for (std::size_t edge = 0; edge < 4; ++edge)
                    {
                        const auto& border = next.border_rows[row][edge];
                        if (border == previous.border_rows[row][edge])
                            continue;
                        if (!border.cell_specific || border.style.empty() || border.style == "none" ||
                            std::abs(border.width * 8 - std::round(border.width * 8)) > 0.000001)
                            return failure("修改边框必须是可精确写回的显式单元格样式。");
                    }
                const bool rtl = candidate.tables[table].right_to_left;
                patch_cell_style(cells[table][cell], previous, next, 0, target_prefix, rtl);
                // Merged-cell shading must also cover each physical continuation cell.
                const auto rows = children(tables[table], "tr");
                for (std::size_t row = next.row + 1; row < next.row + next.row_span; ++row)
                {
                    if (row >= rows.size())
                        return failure("无法定位合并单元格，未写回不确定样式。");
                    std::size_t column = static_cast<std::size_t>(std::clamp(
                        word_xml::attribute(child(child(rows[row], "trPr"), "gridBefore"), "val").as_int(), 0,
                        255));
                    bool found = false;
                    for (auto raw : children(rows[row], "tc"))
                    {
                        const auto format = child(raw, "tcPr");
                        const auto span = word_xml::attribute(child(format, "gridSpan"), "val").as_uint(1);
                        if (column == next.column && span == next.column_span && child(format, "vMerge"))
                        {
                            patch_cell_style(raw, previous, next, row - next.row, target_prefix, rtl);
                            found = true;
                            break;
                        }
                        column += span;
                    }
                    if (!found)
                        return failure("无法定位合并单元格，未写回不确定样式。");
                }
            }
        auto staging = original_xml.document_element().append_child("mf-staging");
        for (const auto& paragraph : candidate.paragraphs)
        {
            if (paragraph.source_id)
                paragraphs.push_back(old_paragraphs[paragraph.source_id - 1]);
            else if (paragraph.origin_id)
            {
                const auto original = locate(source_xml, package.paragraph_paths[paragraph.origin_id - 1]);
                if (!simple_paragraph(original))
                    return failure("此段落含受保护对象或分节标记，暂不能安全拆分。");
                auto added = staging.append_copy(original);
                for (auto attribute = added.first_attribute(); attribute;)
                {
                    const auto next = attribute.next_attribute();
                    const auto name = local(attribute.name());
                    if ((name == "paraId" || name == "textId") &&
                        word_xml::namespace_is(original, attribute.name(),
                            "http://schemas.microsoft.com/office/word/2010/wordml"))
                        added.remove_attribute(attribute);
                    attribute = next;
                }
                inherit_namespaces(original, added);
                paragraphs.push_back(added);
            }
            else
                paragraphs.push_back(staging.append_child((target_prefix + ":p").c_str()));
        }
        WordResult result;
        result.parts = *package.parts;
        std::set<WordListKind> new_lists;
        for (const auto& paragraph : candidate.paragraphs)
        {
            const auto id = paragraph.source_id ? paragraph.source_id : paragraph.origin_id;
            if (paragraph.list != WordListKind::None &&
                (!id || paragraph.list != package.original.paragraphs[id - 1].list))
                new_lists.insert(paragraph.list);
        }
        std::map<WordListKind, int> list_ids;
        const auto numbering_error = append_numbering(result.parts, new_lists, list_ids);
        if (!numbering_error.empty())
            return failure(numbering_error);
        std::map<std::size_t, int> variant_ids;
        const auto variant_error =
            numbering_variants(result.parts, candidate, &package.original, variant_ids);
        if (!variant_error.empty())
            return failure(variant_error);
        for (std::size_t index = 0; index < after.size(); ++index)
        {
            const auto& new_paragraph = candidate.paragraphs[index];
            const auto old_id = new_paragraph.source_id ? new_paragraph.source_id : new_paragraph.origin_id;
            const auto& old_paragraph = old_id ? package.original.paragraphs[old_id - 1] : WordParagraph{};
            const auto previous = old_id ? before[old_id - 1] : pugi::xml_node{};
            if (new_paragraph.source_id && !variant_ids.count(index) &&
                bytes(previous) == bytes(after[index]))
                continue;
            if (variant_ids.count(index) || old_paragraph.list != new_paragraph.list ||
                old_paragraph.list_level != new_paragraph.list_level)
            {
                auto after_properties = child(after[index], "pPr");
                auto next_number = child(after_properties, "numPr");
                if (!next_number)
                    next_number = after_properties.append_child("w:numPr");
                auto next_id = child(next_number, "numId");
                if (!next_id)
                    next_id = next_number.append_child("w:numId");
                auto value = next_id.attribute("w:val");
                if (!value)
                    value = next_id.append_attribute("w:val");
                if (new_paragraph.list == WordListKind::None)
                    value = 0;
                else if (variant_ids.count(index))
                    value = variant_ids.at(index);
                else if (old_paragraph.list != new_paragraph.list)
                    value = list_ids.at(new_paragraph.list);
                else
                {
                    if (old_paragraph.list_instance <= 0)
                        return failure("原段落的列表编号未能定位，未破坏编号关系。");
                    value = old_paragraph.list_instance;
                }
                auto raw_properties = child(paragraphs[index], "pPr");
                if (!raw_properties)
                    raw_properties = paragraphs[index].prepend_child((target_prefix + ":pPr").c_str());
                if (!child(raw_properties, "numPr"))
                {
                    auto added = ordered_copy(raw_properties, next_number, "pPr");
                    rename_prefix(added, target_prefix);
                }
                else
                {
                    const auto raw_id =
                        word_xml::attribute(child(child(raw_properties, "numPr"), "numId"), "val");
                    pugi::xml_document number_baseline;
                    const auto before_number =
                        number_baseline.append_copy(child(child(previous, "pPr"), "numPr"));
                    if (raw_id)
                        child(before_number, "numId").attribute("w:val") = raw_id.value();
                    properties(raw_properties, before_number, next_number, "numPr", target_prefix);
                }
            }
            properties(paragraphs[index], child(previous, "pPr"), child(after[index], "pPr"), "pPr",
                target_prefix, "numPr");
            if (simple_paragraph(paragraphs[index], true))
            {
                if (!rebuild_runs(paragraphs[index], new_paragraph, after[index], source_runs, baseline_runs,
                        target_prefix))
                    return failure("新增文字引用了未知对象，未丢弃复杂内容。");
                continue;
            }
            const auto previous_runs = children(previous, "r");
            const auto next_runs = children(after[index], "r");
            if (previous_runs.size() != old_paragraph.runs.size() ||
                next_runs.size() != new_paragraph.runs.size())
                return failure("复杂文字结构尚不能安全写回，草稿已保留。");
            std::map<std::uint64_t, pugi::xml_node> old_text;
            std::map<std::uint64_t, std::vector<pugi::xml_node>> new_text;
            for (std::size_t run = 0; run < old_paragraph.runs.size(); ++run)
                if (!old_paragraph.runs[run].image_id)
                    old_text[old_paragraph.runs[run].source_id] = previous_runs[run];
            for (std::size_t run = 0; run < new_paragraph.runs.size(); ++run)
            {
                const auto& value = new_paragraph.runs[run];
                if (value.image_id)
                    continue;
                if (!value.source_id || !old_text.count(value.source_id))
                    return failure("新增文字的原包定位尚未完成，草稿已保留。");
                new_text[value.source_id].push_back(next_runs[run]);
            }
            std::uint64_t previous_id = 0;
            for (const auto& run : new_paragraph.runs)
            {
                if (run.source_id < previous_id)
                    return failure("此复杂段落的文字顺序发生变化，未移动域或书签边界。");
                previous_id = run.source_id;
            }
            for (const auto& entry : old_text)
            {
                const auto raw = runs[entry.first];
                if (!raw || !patch_runs(raw, entry.second, new_text[entry.first], target_prefix))
                    return failure("此文字关联域、公式或其他受保护对象，未破坏原结构（段落 " +
                        std::to_string(old_paragraph.source_id) + "，文字 " + std::to_string(entry.first) +
                        "）。");
            }
        }
        std::set<std::size_t> used;
        auto structural_error = reconcile_flow(candidate, package.original, candidate.blocks,
            package.original.blocks, body, paragraphs, old_paragraphs, tables, used);
        if (!structural_error.empty())
            return failure(structural_error);
        for (std::size_t table = 0; table < candidate.tables.size(); ++table)
            for (std::size_t cell = 0; cell < candidate.tables[table].cells.size(); ++cell)
            {
                structural_error =
                    reconcile_flow(candidate, package.original, candidate.tables[table].cells[cell].blocks,
                        package.original.tables[table].cells[cell].blocks, cells[table][cell], paragraphs,
                        old_paragraphs, tables, used);
                if (!structural_error.empty())
                    return failure(structural_error);
            }
        if (staging.first_child())
            return failure("新增段落缺少正文或单元格归属，未写回悬空段落。");
        original_xml.document_element().remove_child(staging);
        for (std::size_t index = 0; index < old_paragraphs.size(); ++index)
            if (!retained.count(index + 1))
                old_paragraphs[index].parent().remove_child(old_paragraphs[index]);
        for (auto& item : result.parts)
            if (item.path == "word/document.xml")
                item.bytes = bytes(original_xml);
        result.success = true;
        return result;
    }
}
