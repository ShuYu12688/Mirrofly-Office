#include "word_package.hpp"
#include "word_images.hpp"
#include "word_style_merge.hpp"
#include "word_styles.hpp"
#include "word_table_borders.hpp"
#include "word_xml.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string_view>

namespace
{
    constexpr auto word_ns = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

    std::string_view local(const char* name)
    {
        const std::string_view text(name);
        const auto colon = text.find(':');
        return colon == text.npos ? text : text.substr(colon + 1);
    }

    bool word_node(pugi::xml_node node)
    {
        const std::string name = node.name();
        const auto colon = name.find(':');
        const auto key = colon == name.npos ? std::string("xmlns") : "xmlns:" + name.substr(0, colon);
        for (; node; node = node.parent())
            if (const auto space = node.attribute(key.c_str()))
                return std::string_view(space.value()) == word_ns;
        return false;
    }

    std::vector<std::size_t> node_path(pugi::xml_node node)
    {
        std::vector<std::size_t> result;
        while (node.parent())
        {
            std::size_t index = 0;
            for (auto previous = node.previous_sibling(); previous; previous = previous.previous_sibling())
                ++index;
            result.push_back(index);
            node = node.parent();
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    pugi::xml_node child(pugi::xml_node node, const char* name)
    {
        for (auto item : node.children())
            if (local(item.name()) == name)
                return item;
        return {};
    }

    pugi::xml_attribute attribute(pugi::xml_node node, const char* name)
    {
        for (auto item : node.attributes())
            if (local(item.name()) == name)
                return item;
        return {};
    }

    bool enabled(pugi::xml_node node)
    {
        const std::string_view value = attribute(node, "val").value();
        return node && value != "0" && value != "false" && value != "off";
    }

    pugi::xml_node descendant(pugi::xml_node node, const char* name)
    {
        if (local(node.name()) == name)
            return node;
        for (auto item : node.children())
            if (auto found = descendant(item, name))
                return found;
        return {};
    }

    std::string color(pugi::xml_node node, const char* name)
    {
        const std::string value = attribute(node, name).value();
        return value.size() == 6 && value.find_first_not_of("0123456789ABCDEFabcdef") == value.npos
            ? "#" + value
            : std::string{};
    }

    std::string package_path(const std::string& target)
    {
        if (target.empty() || target.find_first_of("\\:\0", 0, 3) != target.npos)
            return {};
        std::vector<std::string> segments;
        if (target.front() != '/')
            segments.push_back("word");
        std::istringstream input(target);
        for (std::string item; std::getline(input, item, '/');)
        {
            if (item.empty() || item == ".")
                continue;
            if (item == "..")
            {
                if (segments.empty())
                    return {};
                segments.pop_back();
            }
            else
                segments.push_back(item);
        }
        std::string result;
        for (const auto& item : segments)
            result += (result.empty() ? "" : "/") + item;
        return result;
    }

    class StructureReader
    {
    public:
        StructureReader(mirrorfly::WordDocument& document, mirrorfly::WordPackageState& package,
            mirrorfly::word_detail::StyleResolver& styles)
            : document_(document), parts_(package.parts), package_(package), styles_(styles)
        {
            for (const auto& part : *parts_)
            {
                resources_[part.path] = &part;
                if (part.path == "word/_rels/document.xml.rels")
                {
                    pugi::xml_document xml;
                    if (part.bytes.find("<!DOCTYPE") != part.bytes.npos ||
                        part.bytes.find("<!ENTITY") != part.bytes.npos ||
                        !xml.load_buffer(part.bytes.data(), part.bytes.size()))
                        continue;
                    for (auto rel : xml.document_element().children())
                    {
                        const std::string type = rel.attribute("Type").value();
                        if (std::string_view(rel.attribute("TargetMode").value()) != "External" &&
                            type ==
                                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image")
                            images_[rel.attribute("Id").value()] =
                                package_path(rel.attribute("Target").value());
                    }
                }
            }
        }

        std::vector<mirrorfly::WordBlock> blocks(pugi::xml_node parent)
        {
            using Kind = mirrorfly::WordBlock::Kind;
            std::vector<mirrorfly::WordBlock> result;
            for (auto node : parent.children())
            {
                const auto name = local(node.name());
                if (name == "p" && word_node(node))
                {
                    if (paragraph_ >= document_.paragraphs.size())
                        continue;
                    const auto index = paragraph_++;
                    paragraph(node, document_.paragraphs[index], index);
                    result.push_back({Kind::Paragraph, index});
                }
                else if (name == "tbl" && word_node(node))
                {
                    result.push_back({Kind::Table, table(node)});
                }
                else if (name == "sectPr" && word_node(node))
                    section(node);
                else
                {
                    auto nested = blocks(node);
                    result.insert(result.end(), nested.begin(), nested.end());
                }
            }
            return result;
        }

    private:
        void section(pugi::xml_node node)
        {
            mirrorfly::WordSection result;
            result.first_paragraph = section_start_;
            result.paragraph_count = paragraph_ - section_start_;
            const auto size = child(node, "pgSz");
            result.width = std::clamp(attribute(size, "w").as_double(11906) / 20, 72.0, 3168.0);
            result.height = std::clamp(attribute(size, "h").as_double(16838) / 20, 72.0, 3168.0);
            const auto margins = child(node, "pgMar");
            const char* names[] = {"left", "top", "right", "bottom"};
            for (std::size_t edge = 0; edge < 4; ++edge)
                result.margins[edge] =
                    std::clamp(attribute(margins, names[edge]).as_double(1440) / 20, 0.0, 720.0);
            result.continuous =
                std::string_view(attribute(child(node, "type"), "val").value()) == "continuous";
            document_.sections.push_back(result);
            section_start_ = paragraph_;
        }

        std::uint64_t image(pugi::xml_node node)
        {
            auto reference = mirrorfly::read_word_image(node);
            const auto relationship = images_.find(reference.relationship);
            if (relationship == images_.end())
                return 0;
            const auto resource = resources_.find(relationship->second);
            if (resource == resources_.end())
                return 0;
            auto result = std::move(reference.image);
            result.id = document_.images.size() + 1;
            result.path = relationship->second;
            result.bytes = std::shared_ptr<const std::string>(parts_, &resource->second->bytes);
            const auto extension = result.path.substr(result.path.find_last_of('.') + 1);
            static const std::map<std::string, std::string> mime_types{{"emf", "image/x-emf"},
                {"wmf", "image/x-wmf"}, {"png", "image/png"}, {"svg", "image/svg+xml"}, {"gif", "image/gif"},
                {"GIF", "image/gif"}};
            const auto mime = mime_types.find(extension);
            result.mime_type = mime == mime_types.end() ? "image/jpeg" : mime->second;
            if (reference.approximate)
            {
                const std::string warning = "部分图片的尺寸或裁剪参数无法精确显示，原始对象仍保留。";
                if (std::find(document_.warnings.begin(), document_.warnings.end(), warning) ==
                    document_.warnings.end())
                    document_.warnings.push_back(warning);
            }
            document_.images.push_back(std::move(result));
            return document_.images.back().id;
        }

        void paragraph(pugi::xml_node node, mirrorfly::WordParagraph& result, std::size_t index)
        {
            result.source_id = index + 1;
            package_.paragraph_paths.push_back(node_path(node));
            package_.paragraph_structure_editable.push_back(mirrorfly::word_xml::plain_paragraph(node) &&
                (mirrorfly::word_xml::named(node.parent(), "body") ||
                    mirrorfly::word_xml::named(node.parent(), "tc")));
            // Word permits hanging indents past the text margin.
            result.left_indent = std::max(result.left_indent, -result.first_line_indent);
            std::vector<mirrorfly::WordRun> runs;
            std::size_t run_index = 0;
            const auto visit = [&](const auto& self, pugi::xml_node parent) -> void
            {
                for (auto item : parent.children())
                {
                    const auto name = local(item.name());
                    if (name == "r" && word_node(item))
                    {
                        if (run_index >= result.runs.size())
                            continue;
                        auto run = result.runs[run_index++];
                        run.source_id = ++run_id_;
                        package_.run_paths[run.source_id] = node_path(item);
                        if (!run.text.empty() || !descendant(item, "drawing"))
                            runs.push_back(run);
                        for (auto object : item.children())
                        {
                            const auto kind = local(object.name());
                            if (kind != "drawing" && kind != "pict" && kind != "object" &&
                                kind != "AlternateContent")
                                continue;
                            if (const auto image_id = image(object))
                            {
                                run.text.clear();
                                run.ruby.clear();
                                run.image_id = image_id;
                                runs.push_back(run);
                            }
                        }
                    }
                    else if (name != "pPr")
                        self(self, item);
                }
            };
            visit(visit, node);
            result.runs = std::move(runs);
            if (const auto section_node = child(child(node, "pPr"), "sectPr"))
                section(section_node);
        }

        std::size_t table(pugi::xml_node node)
        {
            mirrorfly::WordTable result;
            const auto index = document_.tables.size();
            document_.tables.emplace_back();
            package_.table_paths.push_back(node_path(node));
            package_.cell_paths.emplace_back();
            result.source_id = index + 1;
            for (auto column : child(node, "tblGrid").children())
                if (local(column.name()) == "gridCol")
                    result.column_widths.push_back(
                        std::clamp(attribute(column, "w").as_double(1440) / 20, 1.0, 3168.0));
            pugi::xml_document table_xml;
            const auto properties = mirrorfly::word_detail::property_root(table_xml, "w:tblPr");
            styles_.table_properties(node, properties);
            result.left_indent =
                std::clamp(attribute(child(properties, "tblInd"), "w").as_double() / 20, 0.0, 504.0);
            std::map<std::size_t, std::size_t> vertical;
            for (auto row : node.children())
            {
                if (local(row.name()) != "tr" || !word_node(row))
                    continue;
                std::size_t column = static_cast<std::size_t>(
                    std::clamp(attribute(child(child(row, "trPr"), "gridBefore"), "val").as_int(), 0, 255));
                if (enabled(child(child(row, "trPr"), "tblHeader")) && result.header_rows == result.rows)
                    ++result.header_rows;
                for (auto cell : row.children())
                {
                    if (local(cell.name()) != "tc" || !word_node(cell))
                        continue;
                    mirrorfly::WordTableCell value;
                    value.row = result.rows;
                    value.column = column;
                    pugi::xml_document cell_xml;
                    const auto format = mirrorfly::word_detail::property_root(cell_xml, "w:tcPr");
                    styles_.cell_properties(cell, format);
                    value.column_span = static_cast<std::size_t>(
                        std::clamp(attribute(child(format, "gridSpan"), "val").as_int(1), 1, 256));
                    value.background = color(child(format, "shd"), "fill");
                    if (const auto resolved = styles_.theme_color(child(format, "shd"), true);
                        !resolved.empty())
                        value.background = resolved;
                    const std::string_view align = attribute(child(format, "vAlign"), "val").value();
                    value.vertical_alignment = align == "center" ? 1 : align == "bottom" ? 2 : 0;
                    const auto margins = child(format, "tcMar");
                    const char* names[] = {"left", "top", "right", "bottom"};
                    for (std::size_t edge = 0; edge < 4; ++edge)
                    {
                        auto margin = child(margins, names[edge]);
                        if (!margin)
                            margin = child(child(properties, "tblCellMar"), names[edge]);
                        value.margins[edge] =
                            std::clamp(attribute(margin, "w").as_double(70) / 20, 0.0, 144.0);
                    }
                    value.blocks = blocks(cell);
                    const auto merge = child(format, "vMerge");
                    const bool continuation =
                        merge && std::string_view(attribute(merge, "val").value()) != "restart";
                    const auto above = vertical.find(column);
                    if (continuation && above != vertical.end() &&
                        result.cells[above->second].column_span == value.column_span &&
                        result.cells[above->second].row + result.cells[above->second].row_span == result.rows)
                    {
                        auto& owner = result.cells[above->second];
                        ++owner.row_span;
                        // Continuation paragraphs remain in the source model, not duplicated on screen.
                    }
                    else
                    {
                        if (merge)
                            vertical[column] = result.cells.size();
                        else
                            vertical.erase(column);
                        result.cells.push_back(std::move(value));
                        package_.cell_paths[index].push_back(node_path(cell));
                    }
                    column += static_cast<std::size_t>(
                        std::clamp(attribute(child(format, "gridSpan"), "val").as_int(1), 1, 256));
                }
                if (column <= 256 && result.column_widths.size() < column)
                    result.column_widths.resize(column, 72);
                ++result.rows;
            }
            mirrorfly::word_detail::read_table_borders(result, node, styles_);
            document_.tables[index] = std::move(result);
            return index;
        }

        mirrorfly::WordDocument& document_;
        std::shared_ptr<const std::vector<mirrorfly::OfficePart>> parts_;
        mirrorfly::WordPackageState& package_;
        mirrorfly::word_detail::StyleResolver& styles_;
        std::map<std::string, const mirrorfly::OfficePart*> resources_;
        std::map<std::string, std::string> images_;
        std::size_t paragraph_ = 0;
        std::size_t section_start_ = 0;
        std::uint64_t run_id_ = 0;
    };
}

namespace mirrorfly
{
    WordParagraphCapabilities word_paragraph_capabilities(const WordDocument& document, std::size_t index)
    {
        if (index >= document.paragraphs.size())
            return {false, false, "段落索引越界。"};
        const auto& paragraph = document.paragraphs[index];
        const auto id = paragraph.source_id ? paragraph.source_id : paragraph.origin_id;
        if (!document.source_package || !id)
            return {true, true, {}};
        const auto& capabilities = document.source_package->paragraph_structure_editable;
        if (id > capabilities.size())
            return {false, false, "段落标识不属于当前文档。"};
        if (!capabilities[id - 1])
            return {false, false, "此段落含域、书签、图片、分节或嵌套控件，暂不支持拆分与删除。"};
        return {true, true, {}};
    }
}

namespace mirrorfly::word_detail
{
    std::string validate_structure(const WordDocument& document)
    {
        if (document.tables.size() > 4096 || document.images.size() > 4096 || document.sections.size() > 4096)
            return "Word 表格、图片或分节数量超过上限。";
        std::set<std::uint64_t> image_ids;
        for (const auto& image : document.images)
        {
            if (image.id == 0 || !image_ids.insert(image.id).second || !image.bytes ||
                image.bytes->size() > maximum_word_part_bytes || !std::isfinite(image.width) ||
                !std::isfinite(image.height) || !std::isfinite(image.rotation) || image.width < 1 ||
                image.height < 1 || image.width > 3168 || image.height > 3168)
                return "Word 图片尺寸、标识或资源无效。";
            for (const auto crop : image.crop)
                if (!std::isfinite(crop) || crop < -1 || crop > 1)
                    return "Word 图片裁剪参数无效。";
            if (image.crop[0] + image.crop[2] >= 1 || image.crop[1] + image.crop[3] >= 1)
                return "Word 图片裁剪没有可见区域。";
        }
        for (const auto& paragraph : document.paragraphs)
        {
            if (!std::isfinite(paragraph.right_indent) || paragraph.right_indent < 0 ||
                paragraph.right_indent > 504 || paragraph.line_spacing_rule < 0 ||
                paragraph.line_spacing_rule > 2 || !std::isfinite(paragraph.line_spacing_points) ||
                paragraph.line_spacing_points < 0 || paragraph.line_spacing_points > 144)
                return "Word 段落排版参数无效。";
            for (const auto& run : paragraph.runs)
                if (run.image_id && !image_ids.count(run.image_id))
                    return "Word 图片引用缺失。";
        }
        std::size_t grid_cells = 0;
        for (const auto& table : document.tables)
        {
            const auto columns = table.column_widths.size();
            if (columns == 0 || columns > 256 || table.rows == 0 || table.rows > 65536 / columns ||
                table.header_rows > table.rows || !std::isfinite(table.border_width) ||
                table.border_width < 0 || table.border_width > 12 || !std::isfinite(table.left_indent))
                return "Word 表格网格或边框无效。";
            grid_cells += table.rows * columns;
            if (grid_cells > 262144)
                return "Word 表格网格总数超过上限。";
            for (const auto width : table.column_widths)
                if (!std::isfinite(width) || width < 1 || width > 3168)
                    return "Word 表格列宽无效。";
            for (const auto& border : table.borders)
                if (!valid_border(border))
                    return "Word 表格边框参数无效。";
            std::vector<bool> occupied(table.rows * columns);
            for (const auto& cell : table.cells)
            {
                if (cell.vertical_alignment < 0 || cell.vertical_alignment > 2 ||
                    (!cell.background.empty() &&
                        (cell.background.size() != 7 || cell.background.front() != '#' ||
                            cell.background.find_first_not_of("0123456789abcdefABCDEF", 1) !=
                                std::string::npos)))
                    return "Word 单元格底色或垂直对齐无效。";
                if (!cell.border_rows.empty() && cell.border_rows.size() != cell.row_span)
                    return "Word 单元格边框行数不匹配。";
                for (const auto& row : cell.border_rows)
                    for (const auto& border : row)
                        if (!valid_border(border))
                            return "Word 单元格边框参数无效。";
                if (cell.row >= table.rows || cell.column >= columns || cell.row_span == 0 ||
                    cell.column_span == 0 || cell.row_span > table.rows - cell.row ||
                    cell.column_span > columns - cell.column)
                    return "Word 表格合并区域越界。";
                for (std::size_t row = cell.row; row < cell.row + cell.row_span; ++row)
                    for (std::size_t column = cell.column; column < cell.column + cell.column_span; ++column)
                    {
                        const auto index = row * columns + column;
                        if (occupied[index])
                            return "Word 表格合并区域重叠。";
                        occupied[index] = true;
                    }
                for (const auto margin : cell.margins)
                    if (!std::isfinite(margin) || margin < 0 || margin > 144)
                        return "Word 表格内边距无效。";
            }
        }
        std::vector<bool> visited(document.tables.size());
        const auto inspect = [&](const auto& self, const std::vector<WordBlock>& blocks, int depth) -> bool
        {
            if (depth > 32)
                return false;
            for (const auto& block : blocks)
            {
                if (block.kind == WordBlock::Kind::Paragraph)
                {
                    if (block.index >= document.paragraphs.size())
                        return false;
                }
                else
                {
                    if (block.index >= document.tables.size() || visited[block.index])
                        return false;
                    visited[block.index] = true;
                    for (const auto& cell : document.tables[block.index].cells)
                        if (!self(self, cell.blocks, depth + 1))
                            return false;
                }
            }
            return true;
        };
        return inspect(inspect, document.blocks, 0) ? std::string{} : "Word 结构引用无效或循环。";
    }

    void attach_structure(
        WordDocument& document, pugi::xml_node body, std::vector<OfficePart> parts, StyleResolver& styles)
    {
        auto package = std::make_shared<WordPackageState>();
        package->parts = std::make_shared<const std::vector<OfficePart>>(std::move(parts));
        package->body_path = node_path(body);
        StructureReader reader(document, *package, styles);
        document.blocks = reader.blocks(body);
        package->original = document;
        document.source_package = std::move(package);
    }

}
