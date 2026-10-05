#include "presentation_preservation.hpp"
#include "presentation_find_replace.hpp"
#include "presentation_group_edit.hpp"
#include "presentation_group_layer.hpp"
#include "presentation_sections.hpp"
#include "presentation_table.hpp"
#include "presentation_table_edges.hpp"
#include "presentation_transition.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
    using Node = pugi::xml_node;
    using State = mirrorfly::PresentationPackageState;
    using Action = mirrorfly::PresentationEditAction;
    constexpr const char* drawing_ns = "http://schemas.openxmlformats.org/drawingml/2006/main";
    constexpr const char* slide_ns = "http://schemas.openxmlformats.org/presentationml/2006/main";
    constexpr const char* relationship_ns =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    constexpr const char* package_ns = "http://schemas.openxmlformats.org/package/2006/relationships";

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

    pugi::xml_attribute attribute(Node node, const char* name)
    {
        auto value = node.attribute(name);
        return value ? value : node.append_attribute(name);
    }

    Node append_package_child(Node parent, const char* name)
    {
        const std::string qualified = parent.name();
        const auto colon = qualified.find(':');
        const auto prefix = colon == std::string::npos ? std::string{} : qualified.substr(0, colon + 1);
        return parent.append_child((prefix + name).c_str());
    }

    int child_rank(Node parent, const std::string& name)
    {
        const auto kind = local(parent.name());
        if (kind == "rPr" || kind == "defRPr" || kind == "endParaRPr")
        {
            if (name == "ln")
                return 0;
            if (name == "noFill" || name == "solidFill" || name == "gradFill" || name == "blipFill" ||
                name == "pattFill" || name == "grpFill")
                return 1;
            if (name == "effectLst" || name == "effectDag")
                return 2;
            if (name == "highlight")
                return 3;
            if (name == "uLnTx" || name == "uLn")
                return 4;
            if (name == "uFillTx" || name == "uFill")
                return 5;
            if (name == "latin")
                return 6;
            if (name == "ea")
                return 7;
            if (name == "cs")
                return 8;
            if (name == "sym")
                return 9;
            if (name == "hlinkClick")
                return 10;
            if (name == "hlinkMouseOver")
                return 11;
            if (name == "rtl")
                return 12;
        }
        else if (kind == "effectLst")
        {
            const std::vector<std::string> names{"blur", "fillOverlay", "glow", "innerShdw", "outerShdw",
                "prstShdw", "reflection", "softEdge"};
            const auto found = std::find(names.begin(), names.end(), name);
            if (found != names.end())
                return static_cast<int>(found - names.begin());
        }
        else if (kind == "spPr")
        {
            if (name == "xfrm")
                return 0;
            if (name == "prstGeom" || name == "custGeom")
                return 1;
            if (name == "noFill" || name == "solidFill" || name == "gradFill" || name == "blipFill" ||
                name == "pattFill" || name == "grpFill")
                return 2;
            if (name == "ln")
                return 3;
            if (name == "effectLst" || name == "effectDag")
                return 4;
            if (name == "scene3d")
                return 5;
            if (name == "sp3d")
                return 6;
        }
        else if (kind == "ln")
        {
            if (name == "noFill" || name == "solidFill" || name == "gradFill" || name == "pattFill")
                return 0;
            if (name == "prstDash" || name == "custDash")
                return 1;
            if (name == "round" || name == "bevel" || name == "miter")
                return 2;
            if (name == "headEnd")
                return 3;
            if (name == "tailEnd")
                return 4;
        }
        else if (kind == "pPr")
        {
            if (name == "lnSpc")
                return 0;
            if (name == "spcBef")
                return 1;
            if (name == "spcAft")
                return 2;
            if (name == "buClrTx" || name == "buClr")
                return 3;
            if (name == "buSzTx" || name == "buSzPct" || name == "buSzPts")
                return 4;
            if (name == "buFontTx" || name == "buFont")
                return 5;
            if (name == "buNone" || name == "buAutoNum" || name == "buChar" || name == "buBlip")
                return 6;
            if (name == "tabLst")
                return 7;
            if (name == "defRPr")
                return 8;
        }
        else if (kind == "tcPr")
        {
            if (name == "lnL" || name == "lnR" || name == "lnT" || name == "lnB" || name == "lnTlToBr" ||
                name == "lnBlToTr")
                return 0;
            if (name == "cell3D")
                return 1;
            if (name == "noFill" || name == "solidFill" || name == "gradFill" || name == "blipFill" ||
                name == "pattFill" || name == "grpFill")
                return 2;
            if (name == "headers")
                return 3;
            if (name == "extLst")
                return 4;
        }
        else if (kind == "bodyPr")
        {
            if (name == "prstTxWarp")
                return 0;
            if (name == "noAutofit" || name == "normAutofit" || name == "spAutoFit")
                return 1;
            if (name == "scene3d")
                return 2;
            if (name == "sp3d")
                return 3;
            if (name == "flatTx")
                return 4;
            if (name == "extLst")
                return 5;
        }
        return 100;
    }

    Node ordered_anchor(Node parent, const std::string& name)
    {
        const auto rank = child_rank(parent, name);
        for (auto item : parent.children())
            if (item.type() == pugi::node_element && child_rank(parent, local(item.name())) > rank)
                return item;
        return {};
    }

    Node ensure(Node node, const char* name, bool first = false)
    {
        auto found = child(node, local(name));
        if (found)
            return found;
        if (first)
            return node.prepend_child(name);
        const auto anchor = ordered_anchor(node, local(name));
        return anchor ? node.insert_child_before(name, anchor) : node.append_child(name);
    }

    std::string relation_path(const std::string& source)
    {
        const auto split = source.rfind('/');
        if (split == std::string::npos)
            return "_rels/" + source + ".rels";
        return source.substr(0, split + 1) + "_rels/" + source.substr(split + 1) + ".rels";
    }

    std::string absolute_target(const std::string& owner, const std::string& target)
    {
        if (target.empty())
            throw std::runtime_error("文件关联缺少目标。");
        if (target.front() == '/')
            return target;
        const auto slash = owner.rfind('/');
        const auto joined = (slash == std::string::npos ? "" : owner.substr(0, slash + 1)) + target;
        std::vector<std::string> pieces;
        std::istringstream input(joined);
        std::string piece;
        while (std::getline(input, piece, '/'))
        {
            if (piece == "..")
            {
                if (pieces.empty())
                    throw std::runtime_error("文件关联超出包目录。");
                pieces.pop_back();
            }
            else if (!piece.empty() && piece != ".")
                pieces.push_back(piece);
        }
        std::string result;
        for (const auto& item : pieces)
            result += "/" + item;
        return result;
    }

    void load(
        const State& state, const std::string& path, pugi::xml_document& document, bool optional = false)
    {
        const auto found = state.parts.find(path);
        if (found == state.parts.end())
        {
            if (!optional)
                throw std::runtime_error("原包中缺少待编辑的文件部件。");
            document.append_child("Relationships").append_attribute("xmlns") = package_ns;
            return;
        }
        if (!document.load_buffer(
                found->second->data(), found->second->size(), pugi::parse_default | pugi::parse_ws_pcdata))
            throw std::runtime_error("原包中的 XML 无法编辑。");
    }

    void store(State& state, const std::string& path, const pugi::xml_document& document)
    {
        std::ostringstream stream;
        document.save(stream, "", pugi::format_raw, pugi::encoding_utf8);
        auto bytes = stream.str();
        if (bytes.size() > mirrorfly::maximum_presentation_xml_bytes)
            throw std::runtime_error("编辑后的 XML 超过资源上限。");
        state.parts[path] = std::make_shared<const std::string>(std::move(bytes));
    }

    void namespaces(Node root)
    {
        const std::pair<const char*, const char*> bindings[]{
            {"xmlns:a", drawing_ns}, {"xmlns:p", slide_ns}, {"xmlns:r", relationship_ns}};
        for (const auto& item : bindings)
        {
            const auto existing = root.attribute(item.first);
            if (existing && std::string(existing.value()) != item.second)
                throw std::runtime_error("此文件使用特殊命名空间，当前保留为只读。");
            attribute(root, item.first) = item.second;
        }
    }

    Node properties(Node shape)
    {
        for (auto node : shape.children())
            if (const auto found = child(node, "cNvPr"))
                return found;
        return {};
    }

    Node shape_node(Node tree, const std::string& id)
    {
        if (id.empty())
            return {};
        Node result;
        for (auto item : tree.children())
        {
            Node found;
            if (properties(item).attribute("id").value() == id)
                found = item;
            if (local(item.name()) == "grpSp")
                if (const auto nested = shape_node(item, id))
                {
                    if (found)
                        throw std::runtime_error("对象标识重复，无法安全编辑。");
                    found = nested;
                }
            if (found)
            {
                if (result)
                    throw std::runtime_error("对象标识重复，无法安全编辑。");
                result = found;
            }
        }
        return result;
    }

    unsigned long long largest_id(Node node)
    {
        auto result = local(node.name()) == "cNvPr" ? node.attribute("id").as_ullong() : 1;
        for (auto item : node.children())
            result = std::max(result, largest_id(item));
        return result;
    }

    std::string next_shape_id(Node tree)
    {
        const auto value = largest_id(tree) + 1;
        if (value >= std::numeric_limits<unsigned int>::max())
            throw std::runtime_error("对象标识数量已达上限。");
        return std::to_string(value);
    }

    std::string unique_relation(Node root)
    {
        std::set<std::string> used;
        for (auto item : root.children())
            used.insert(item.attribute("Id").value());
        for (unsigned int id = 1; id < 100000; ++id)
        {
            const auto candidate = "mf" + std::to_string(id);
            if (!used.count(candidate))
                return candidate;
        }
        throw std::runtime_error("文件关联数量已达上限。");
    }

    std::string unique_path(const State& state, const std::string& prefix, const std::string& suffix)
    {
        for (unsigned int id = 1; id < 100000; ++id)
        {
            const auto path = prefix + std::to_string(id) + suffix;
            if (!state.parts.count(path))
                return path;
        }
        throw std::runtime_error("文件部件数量已达上限。");
    }

    void content_type(State& state, const std::string& path, const std::string& type)
    {
        pugi::xml_document document;
        load(state, "[Content_Types].xml", document);
        auto item = append_package_child(document.document_element(), "Override");
        item.append_attribute("PartName") = ("/" + path).c_str();
        item.append_attribute("ContentType") = type.c_str();
        store(state, "[Content_Types].xml", document);
    }

    std::string append_image_resource(
        State& state, const std::string& slide_path, const mirrorfly::PresentationImage& image)
    {
        const std::map<std::string, std::string> extensions{{"image/png", ".png"}, {"image/jpeg", ".jpg"},
            {"image/gif", ".gif"}, {"image/bmp", ".bmp"}, {"image/webp", ".webp"}};
        const auto extension = extensions.find(image.mime_type);
        if (extension == extensions.end() || !image.bytes || image.bytes->empty())
            throw std::runtime_error("替换图片格式无效。");
        const auto path = unique_path(state, "ppt/media/mirrorfly", extension->second);
        state.parts[path] = image.bytes;
        content_type(state, path, image.mime_type);
        pugi::xml_document relations;
        load(state, relation_path(slide_path), relations, true);
        auto root = relations.document_element();
        const auto id = unique_relation(root);
        auto relation = append_package_child(root, "Relationship");
        relation.append_attribute("Id") = id.c_str();
        relation.append_attribute("Type") = (std::string(relationship_ns) + "/image").c_str();
        relation.append_attribute("Target") = ("/" + path).c_str();
        store(state, relation_path(slide_path), relations);
        return id;
    }

    void replace_child(Node parent, const std::string& name, Node replacement)
    {
        const auto previous = child(parent, name);
        if (previous)
        {
            parent.insert_copy_before(replacement, previous);
            parent.remove_child(previous);
        }
        else
            parent.append_copy(replacement);
    }

    void copy_fill(Node target, Node source)
    {
        const std::set<std::string> names{
            "noFill", "solidFill", "gradFill", "blipFill", "pattFill", "grpFill"};
        Node insertion;
        for (auto node : target.children())
        {
            if (names.count(local(node.name())))
            {
                insertion = node;
                break;
            }
        }
        for (auto node : source.children())
        {
            if (!names.count(local(node.name())))
                continue;
            if (insertion)
                target.insert_copy_before(node, insertion);
            else
            {
                const auto anchor = ordered_anchor(target, local(node.name()));
                if (anchor)
                    target.insert_copy_before(node, anchor);
                else
                    target.append_copy(node);
            }
            break;
        }
        if (insertion)
            target.remove_child(insertion);
    }

    void copy_ordered_child(Node target, Node source, const std::string& name)
    {
        const auto previous = child(target, name);
        const auto replacement = child(source, name);
        if (replacement)
        {
            if (previous)
                target.insert_copy_before(replacement, previous);
            else
            {
                const auto anchor = ordered_anchor(target, name);
                if (anchor)
                    target.insert_copy_before(replacement, anchor);
                else
                    target.append_copy(replacement);
            }
        }
        if (previous)
            target.remove_child(previous);
    }

    void remove_children(Node parent, const std::initializer_list<const char*>& names)
    {
        for (const auto* name : names)
        {
            while (const auto node = child(parent, name))
                parent.remove_child(node);
        }
    }

    void prototype(const mirrorfly::PresentationShape& shape, pugi::xml_document& document, bool transform)
    {
        auto scene = mirrorfly::make_presentation(mirrorfly::PresentationSlideLayout::Blank);
        auto copy = shape;
        copy.image_path.clear();
        copy.geometry = "rect";
        copy.geometry_definition.clear();
        if (transform)
        {
            const auto& p = shape.source_parent_transform;
            const double determinant = p[0] * p[3] - p[1] * p[2];
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
                throw std::runtime_error("组合坐标不可逆，原始内容已保留。");
            const auto& m = shape.transform;
            const auto x = [&](double a, double b)
            {
                return (p[3] * a - p[2] * b) / determinant;
            };
            const auto y = [&](double a, double b)
            {
                return (-p[1] * a + p[0] * b) / determinant;
            };
            copy.transform = {x(m[0], m[1]), y(m[0], m[1]), x(m[2], m[3]), y(m[2], m[3]),
                x(m[4] - p[4], m[5] - p[5]), y(m[4] - p[4], m[5] - p[5])};
        }
        if (!transform)
        {
            copy.transform = {1, 0, 0, 1, 0, 0};
            copy.width = std::max(1.0, copy.width);
            copy.height = std::max(1.0, copy.height);
        }
        scene.slides.front().shapes.push_back(std::move(copy));
        const auto package = mirrorfly::serialize_presentation(scene);
        if (package.error != mirrorfly::PresentationError::None)
            throw std::runtime_error("此对象的变换无法安全编辑，原始内容已保留。");
        for (const auto& part : package.parts)
            if (part.path == "ppt/slides/slide1.xml")
                document.load_buffer(part.bytes.data(), part.bytes.size());
    }

    Node first_shape(const pugi::xml_document& document)
    {
        const auto tree = child(child(document.document_element(), "cSld"), "spTree");
        for (auto node : tree.children())
            if (local(node.name()) == "sp" || local(node.name()) == "pic" || local(node.name()) == "cxnSp")
                return node;
        return {};
    }

    void patch_run(Node properties, const mirrorfly::PresentationEditCommand& command)
    {
        if (command.font_size)
            attribute(properties, "sz") = static_cast<int>(std::llround(*command.font_size * 100));
        if (command.bold)
            attribute(properties, "b") = *command.bold ? "1" : "0";
        if (command.italic)
            attribute(properties, "i") = *command.italic ? "1" : "0";
        if (command.underline)
            attribute(properties, "u") = *command.underline ? "sng" : "none";
        if (command.strike)
            attribute(properties, "strike") = *command.strike ? "sngStrike" : "noStrike";
        if (command.character_spacing)
            attribute(properties, "spc") = static_cast<int>(std::llround(*command.character_spacing * 100));
        if (command.baseline)
            attribute(properties, "baseline") = static_cast<int>(std::llround(*command.baseline * 100000));
        if (command.font_family)
        {
            attribute(ensure(properties, "a:latin"), "typeface") = command.font_family->c_str();
            attribute(ensure(properties, "a:ea"), "typeface") = command.font_family->c_str();
        }
        if (command.text_color)
        {
            pugi::xml_document fill;
            auto node = fill.append_child("holder").append_child("a:solidFill");
            node.append_child("a:srgbClr").append_attribute("val") = command.text_color->substr(1).c_str();
            copy_fill(properties, fill.document_element());
        }
    }

    void patch_paragraph_format(
        Node body, Node generated_body, const mirrorfly::PresentationEditCommand& command)
    {
        auto generated = generated_body.first_child();
        int index = 0;
        for (auto paragraph : body.children())
        {
            if (local(paragraph.name()) != "p")
                continue;
            while (generated && local(generated.name()) != "p")
                generated = generated.next_sibling();
            if (!generated)
                break;
            if (command.paragraph_index && index++ != *command.paragraph_index)
            {
                generated = generated.next_sibling();
                continue;
            }
            auto properties = ensure(paragraph, "a:pPr", true);
            const auto generated_properties = child(generated, "pPr");
            if (command.alignment)
                attribute(properties, "algn") = generated_properties.attribute("algn").value();
            if (command.list_level)
                attribute(properties, "lvl") = *command.list_level;
            if (command.paragraph_margin_left || command.list_level)
                attribute(properties, "marL") = generated_properties.attribute("marL").value();
            if (command.first_line_indent)
                attribute(properties, "indent") = generated_properties.attribute("indent").value();
            if (command.line_spacing)
                copy_ordered_child(properties, generated_properties, "lnSpc");
            if (command.space_before)
                copy_ordered_child(properties, generated_properties, "spcBef");
            if (command.space_after)
                copy_ordered_child(properties, generated_properties, "spcAft");
            if (command.bullet || command.numbered || command.number_start)
            {
                remove_children(properties, {"buNone", "buAutoNum", "buChar", "buBlip"});
                for (const auto* name : {"buNone", "buAutoNum", "buChar", "buBlip"})
                {
                    if (const auto list = child(generated_properties, name))
                    {
                        const auto anchor = ordered_anchor(properties, name);
                        if (anchor)
                            properties.insert_copy_before(list, anchor);
                        else
                            properties.append_copy(list);
                        break;
                    }
                }
            }
            generated = generated.next_sibling();
        }
    }

    void patch_text_effect(Node effects, Node generated, const std::string& name)
    {
        auto previous = child(effects, name);
        const auto replacement = child(generated, name);
        if (!previous || !replacement)
        {
            copy_ordered_child(effects, generated, name);
            return;
        }
        for (const auto value : replacement.attributes())
        {
            // Preserve imported geometry that the editor does not model.
            if (std::string(value.name()) != "sy" || !previous.attribute("sy"))
                attribute(previous, value.name()) = value.value();
        }
        if (name != "reflection")
        {
            remove_children(previous, {"scrgbClr", "srgbClr", "hslClr", "sysClr", "schemeClr", "prstClr"});
            for (const auto color : replacement.children())
                previous.append_copy(color);
        }
    }

    void patch_text_style(Node body, Node generated_body, const mirrorfly::PresentationTextStylePatch& patch)
    {
        const auto patch_properties = [&](Node properties, Node source)
        {
            if (patch.fill)
                copy_fill(properties, source);
            if (patch.outline)
            {
                copy_ordered_child(properties, source, "ln");
                if (!child(source, "ln"))
                    ensure(ensure(properties, "a:ln"), "a:noFill");
            }
            if (patch.shadow || patch.glow || patch.reflection)
            {
                if (child(properties, "effectDag"))
                    throw std::runtime_error("此文字包含复杂效果链，暂不覆盖；其他文字格式仍可编辑。");
                // A local list must carry inherited, untouched effects too.
                auto effects = child(properties, "effectLst");
                if (!effects)
                {
                    copy_ordered_child(properties, source, "effectLst");
                    effects = ensure(properties, "a:effectLst");
                }
                if (patch.shadow)
                    patch_text_effect(effects, child(source, "effectLst"), "outerShdw");
                if (patch.glow)
                    patch_text_effect(effects, child(source, "effectLst"), "glow");
                if (patch.reflection)
                    patch_text_effect(effects, child(source, "effectLst"), "reflection");
            }
        };
        auto generated_paragraph = child(generated_body, "p");
        for (auto paragraph : body.children())
        {
            if (local(paragraph.name()) != "p")
                continue;
            auto generated_run = child(generated_paragraph, "r");
            auto source = child(generated_run, "rPr");
            patch_properties(ensure(ensure(paragraph, "a:pPr", true), "a:defRPr"), source);
            for (auto run : paragraph.children())
            {
                const auto kind = local(run.name());
                if (kind == "r" || kind == "fld" || kind == "br")
                {
                    source = child(generated_run, "rPr");
                    patch_properties(ensure(run, "a:rPr", true), source);
                    generated_run = generated_run.next_sibling();
                }
                else if (kind == "tab" || kind == "m")
                    generated_run = generated_run.next_sibling();
                else if (kind == "endParaRPr")
                    patch_properties(run, source);
            }
            generated_paragraph = generated_paragraph.next_sibling();
        }
        auto properties = ensure(body, "a:bodyPr", true);
        const auto generated_properties = child(generated_body, "bodyPr");
        if (patch.warp || patch.warp_adjustment)
        {
            copy_ordered_child(properties, generated_properties, "prstTxWarp");
            if (!child(generated_properties, "prstTxWarp"))
                attribute(ensure(properties, "a:prstTxWarp"), "prst") = "textNoShape";
        }
        if (patch.rotation)
            attribute(properties, "rot") = generated_properties.attribute("rot").value();
        if (patch.vertical)
            attribute(properties, "vert") = generated_properties.attribute("vert").value();
        if (patch.clip_vertical)
            attribute(properties, "vertOverflow") = generated_properties.attribute("vertOverflow").value();
        if (patch.clip_horizontal)
            attribute(properties, "horzOverflow") = generated_properties.attribute("horzOverflow").value();
    }

    void patch_text_box(Node body, Node generated_body, const mirrorfly::PresentationEditCommand& command)
    {
        auto properties = ensure(body, "a:bodyPr", true);
        const auto generated = child(generated_body, "bodyPr");
        if (command.inset_left)
            attribute(properties, "lIns") = generated.attribute("lIns").value();
        if (command.inset_right)
            attribute(properties, "rIns") = generated.attribute("rIns").value();
        if (command.inset_top)
            attribute(properties, "tIns") = generated.attribute("tIns").value();
        if (command.inset_bottom)
            attribute(properties, "bIns") = generated.attribute("bIns").value();
        if (command.vertical_alignment)
            attribute(properties, "anchor") = generated.attribute("anchor").value();
        if (command.wrap)
            attribute(properties, "wrap") = generated.attribute("wrap").value();
        if (command.auto_fit)
        {
            remove_children(properties, {"noAutofit", "normAutofit", "spAutoFit"});
            if (const auto autofit = child(generated, "normAutofit"))
            {
                const auto anchor = ordered_anchor(properties, "normAutofit");
                if (anchor)
                    properties.insert_copy_before(autofit, anchor);
                else
                    properties.append_copy(autofit);
            }
        }
    }

    void patch_text_format(Node body, const mirrorfly::PresentationEditCommand& command)
    {
        for (auto paragraph : body.children())
        {
            if (local(paragraph.name()) != "p")
                continue;
            auto paragraph_properties = ensure(paragraph, "a:pPr", true);
            if (command.alignment)
            {
                const std::map<std::string, const char*> alignments{
                    {"left", "l"}, {"right", "r"}, {"center", "ctr"}, {"justify", "just"}};
                attribute(paragraph_properties, "algn") = alignments.at(*command.alignment);
            }
            if (command.bullet)
            {
                for (const auto* name : {"buNone", "buAutoNum", "buChar", "buBlip"})
                    paragraph_properties.remove_child(child(paragraph_properties, name));
                auto bullet = ensure(paragraph_properties, *command.bullet ? "a:buChar" : "a:buNone");
                if (*command.bullet)
                    bullet.append_attribute("char") = "•";
            }
            patch_run(ensure(paragraph_properties, "a:defRPr"), command);
            for (auto run : paragraph.children())
            {
                const auto name = local(run.name());
                if (name == "r" || name == "fld" || name == "br")
                    patch_run(ensure(run, "a:rPr", true), command);
                else if (name == "endParaRPr")
                    patch_run(run, command);
            }
        }
    }

    void patch_image(Node node, const mirrorfly::PresentationShape& after)
    {
        auto fill = child(node, "blipFill");
        auto blip = child(fill, "blip");
        if (!fill || !blip)
            throw std::runtime_error("当前对象不是可编辑图片。");
        auto crop = child(fill, "srcRect");
        if (!crop)
        {
            auto anchor = child(fill, "tile");
            if (!anchor)
                anchor = child(fill, "stretch");
            crop = anchor ? fill.insert_child_before("a:srcRect", anchor) : fill.append_child("a:srcRect");
        }
        const char* sides[]{"l", "t", "r", "b"};
        for (std::size_t index = 0; index < after.image_crop.size(); ++index)
            attribute(crop, sides[index]) =
                static_cast<long long>(std::llround(after.image_crop[index] * 100000));
        auto alpha = child(blip, "alphaModFix");
        if (after.image_opacity >= 0.999)
        {
            if (alpha)
                blip.remove_child(alpha);
        }
        else
        {
            if (!alpha)
            {
                const auto extensions = child(blip, "extLst");
                if (extensions)
                    alpha = blip.insert_child_before("a:alphaModFix", extensions);
                else
                    alpha = blip.append_child("a:alphaModFix");
            }
            attribute(alpha, "amt") = static_cast<long long>(std::llround(after.image_opacity * 100000));
        }
    }

    void patch_shape(Node node, const mirrorfly::PresentationShape& after,
        const mirrorfly::PresentationEditCommand& command)
    {
        const auto action = command.action;
        const bool transform = action == Action::TransformShape || action == Action::AlignShape ||
            action == Action::DuplicateShape;
        pugi::xml_document generated;
        prototype(after, generated, transform);
        const auto source = first_shape(generated);
        auto properties = ensure(node, "p:spPr");
        const auto generated_properties = child(source, "spPr");
        if (transform)
        {
            auto old = child(properties, "xfrm");
            if (old)
                replace_child(properties, "xfrm", child(generated_properties, "xfrm"));
            else
                properties.prepend_copy(child(generated_properties, "xfrm"));
        }
        else if (action == Action::FormatShape)
        {
            if (command.fill_color || command.fill_opacity || command.gradient_start_color ||
                command.gradient_end_color || command.gradient_angle || command.fill_pattern ||
                command.pattern_foreground_color || command.pattern_background_color)
                copy_fill(properties, generated_properties);
            if (command.outline_color || command.outline_opacity || command.outline_width ||
                command.line_dash || command.line_head || command.line_tail)
            {
                auto line = ensure(properties, "a:ln");
                const auto generated_line = child(generated_properties, "ln");
                if (command.outline_color || command.outline_opacity)
                    copy_fill(line, generated_line);
                if (command.outline_width)
                    attribute(line, "w") = static_cast<long long>(std::llround(after.outline_width * 12700));
                if (command.line_dash)
                {
                    remove_children(line, {"prstDash", "custDash"});
                    const auto dash = child(generated_line, "prstDash") ? child(generated_line, "prstDash")
                                                                        : child(generated_line, "custDash");
                    if (dash)
                    {
                        const auto anchor = ordered_anchor(line, local(dash.name()));
                        if (anchor)
                            line.insert_copy_before(dash, anchor);
                        else
                            line.append_copy(dash);
                    }
                }
                const auto patch_end = [&](const char* name, const mirrorfly::PresentationLineEnd& value)
                {
                    if ((std::string(name) == "headEnd" && !command.line_head) ||
                        (std::string(name) == "tailEnd" && !command.line_tail))
                        return;
                    auto end = ensure(line, (std::string("a:") + name).c_str());
                    attribute(end, "type") = value.type.c_str();
                    attribute(end, "w") = value.width.c_str();
                    attribute(end, "len") = value.length.c_str();
                };
                patch_end("headEnd", after.line_style.head);
                patch_end("tailEnd", after.line_style.tail);
            }
            if (command.shadow_enabled || command.shadow_color || command.shadow_opacity ||
                command.shadow_blur || command.shadow_x || command.shadow_y || command.glow_enabled ||
                command.glow_color || command.glow_opacity || command.glow_radius)
            {
                const bool patch_shadow = command.shadow_enabled || command.shadow_color ||
                    command.shadow_opacity || command.shadow_blur || command.shadow_x || command.shadow_y;
                const bool patch_glow =
                    command.glow_enabled || command.glow_color || command.glow_opacity || command.glow_radius;
                const auto generated_effects = child(generated_properties, "effectLst");
                auto effects = child(properties, "effectLst");
                const bool overrides_theme = static_cast<bool>(child(child(node, "style"), "effectRef"));
                if (!effects && generated_effects)
                {
                    const auto anchor = ordered_anchor(properties, "effectLst");
                    if (anchor)
                        effects = properties.insert_copy_before(generated_effects, anchor);
                    else
                        effects = properties.append_copy(generated_effects);
                }
                else if (!effects && overrides_theme)
                {
                    const auto anchor = ordered_anchor(properties, "effectLst");
                    if (anchor)
                        properties.insert_child_before("a:effectLst", anchor);
                    else
                        properties.append_child("a:effectLst");
                }
                else if (effects)
                {
                    const auto remove_effect = [&](const char* name)
                    {
                        for (auto item = effects.first_child(); item;)
                        {
                            const auto next = item.next_sibling();
                            if (local(item.name()) == name)
                                effects.remove_child(item);
                            item = next;
                        }
                    };
                    const auto effect_rank = [](const std::string& name)
                    {
                        if (name == "blur")
                            return 0;
                        if (name == "fillOverlay")
                            return 1;
                        if (name == "glow")
                            return 2;
                        if (name == "innerShdw")
                            return 3;
                        if (name == "outerShdw")
                            return 4;
                        if (name == "prstShdw")
                            return 5;
                        if (name == "reflection")
                            return 6;
                        if (name == "softEdge")
                            return 7;
                        return 8;
                    };
                    const auto insert_effect = [&](const char* name)
                    {
                        const auto generated = child(generated_effects, name);
                        if (!generated)
                            return;
                        Node anchor;
                        const int rank = effect_rank(name);
                        for (auto item : effects.children())
                        {
                            if (item.type() == pugi::node_element && effect_rank(local(item.name())) > rank)
                            {
                                anchor = item;
                                break;
                            }
                        }
                        if (anchor)
                            effects.insert_copy_before(generated, anchor);
                        else
                            effects.append_copy(generated);
                    };
                    if (patch_shadow)
                    {
                        remove_effect("innerShdw");
                        remove_effect("outerShdw");
                        insert_effect("outerShdw");
                    }
                    if (patch_glow)
                    {
                        remove_effect("glow");
                        insert_effect("glow");
                    }
                    bool has_effect = false;
                    for (auto item : effects.children())
                    {
                        has_effect = has_effect || item.type() == pugi::node_element;
                    }
                    if (!has_effect && !overrides_theme)
                    {
                        properties.remove_child(effects);
                    }
                }
            }
        }
        else if (action == Action::FormatImage)
            patch_image(node, after);
        else if (action == Action::UpdateText)
        {
            auto body = child(node, "txBody");
            if (!body)
                node.append_copy(child(source, "txBody"));
            else
            {
                for (auto item = body.first_child(); item;)
                {
                    auto next = item.next_sibling();
                    if (local(item.name()) == "p")
                        body.remove_child(item);
                    item = next;
                }
                for (auto item : child(source, "txBody").children())
                    if (local(item.name()) == "p")
                        body.append_copy(item);
            }
        }
        else if (action == Action::FormatText)
        {
            if (!child(node, "txBody"))
                node.append_copy(child(source, "txBody"));
            patch_text_format(child(node, "txBody"), command);
        }
        else if (action == Action::FormatParagraph)
        {
            if (!child(node, "txBody"))
                node.append_copy(child(source, "txBody"));
            patch_paragraph_format(child(node, "txBody"), child(source, "txBody"), command);
        }
        else if (action == Action::FormatTextBox)
        {
            if (!child(node, "txBody"))
                node.append_copy(child(source, "txBody"));
            patch_text_box(child(node, "txBody"), child(source, "txBody"), command);
        }
        else if (action == Action::FormatTextStyle)
        {
            if (!child(node, "txBody"))
                node.append_copy(child(source, "txBody"));
            patch_text_style(child(node, "txBody"), child(source, "txBody"), command.text_style);
        }
    }

    void reset_placeholder_fill(
        State& state, const mirrorfly::PresentationSlide& slide, const mirrorfly::PresentationShape& shape)
    {
        pugi::xml_document document;
        load(state, slide.source_part, document);
        const auto tree = child(child(document.document_element(), "cSld"), "spTree");
        const auto node = shape_node(tree, shape.source_id);
        if (!node || shape.source_part != slide.source_part)
            throw std::runtime_error("只能恢复当前页占位符的填充继承。");
        remove_children(
            child(node, "spPr"), {"noFill", "solidFill", "gradFill", "blipFill", "pattFill", "grpFill"});
        auto style = child(node, "style");
        if (style)
            style.remove_child(child(style, "fillRef"));
        store(state, slide.source_part, document);
    }

    void reset_placeholder_outline(
        State& state, const mirrorfly::PresentationSlide& slide, const mirrorfly::PresentationShape& shape)
    {
        pugi::xml_document document;
        load(state, slide.source_part, document);
        const auto tree = child(child(document.document_element(), "cSld"), "spTree");
        const auto node = shape_node(tree, shape.source_id);
        if (!node || shape.source_part != slide.source_part)
            throw std::runtime_error("只能恢复当前页占位符的轮廓继承。");
        auto properties = child(node, "spPr");
        if (properties)
            properties.remove_child(child(properties, "ln"));
        auto style = child(node, "style");
        if (style)
            style.remove_child(child(style, "lnRef"));
        store(state, slide.source_part, document);
    }

    bool clear_local_text_property(Node properties, mirrorfly::PresentationTextProperty property)
    {
        if (!properties)
            return false;
        bool changed = false;
        if (property == mirrorfly::PresentationTextProperty::FontFamily)
        {
            changed = properties.remove_child(child(properties, "latin")) || changed;
            changed = properties.remove_child(child(properties, "ea")) || changed;
        }
        else if (property == mirrorfly::PresentationTextProperty::FontSize)
            changed = properties.remove_attribute("sz");
        else
        {
            for (const auto* name : {"noFill", "solidFill", "gradFill"})
                changed = properties.remove_child(child(properties, name)) || changed;
        }
        return changed;
    }

    Node table_cell_node(Node tree, const mirrorfly::PresentationTableCell& address)
    {
        const auto frame = shape_node(tree, address.frame_id);
        const auto table = child(child(child(frame, "graphic"), "graphicData"), "tbl");
        const auto nth = [](Node parent, const char* name, std::size_t index)
        {
            for (auto item : parent.children())
                if (local(item.name()) == name && index-- == 0)
                    return item;
            return Node{};
        };
        auto cell = nth(nth(table, "tr", address.row), "tc", address.xml_cell);
        if (cell && !cell.attribute("hMerge").as_bool() && !cell.attribute("vMerge").as_bool())
            return cell;
        return {};
    }

    void reset_text_inheritance(State& state, const mirrorfly::PresentationSlide& slide,
        const mirrorfly::PresentationShape& shape, mirrorfly::PresentationTextProperty property)
    {
        pugi::xml_document document;
        load(state, slide.source_part, document);
        const auto tree = child(child(document.document_element(), "cSld"), "spTree");
        const auto node =
            shape.table_cell ? table_cell_node(tree, *shape.table_cell) : shape_node(tree, shape.source_id);
        if (!node || shape.source_part != slide.source_part)
            throw std::runtime_error("只能恢复当前页文字对象或单元格的直接格式。");
        auto body = child(node, "txBody");
        bool changed = false;
        const auto list_style = child(body, "lstStyle");
        for (auto style : list_style.children())
            changed = clear_local_text_property(child(style, "defRPr"), property) || changed;
        for (auto paragraph : body.children())
        {
            if (local(paragraph.name()) != "p")
                continue;
            changed =
                clear_local_text_property(child(child(paragraph, "pPr"), "defRPr"), property) || changed;
            for (auto run : paragraph.children())
            {
                const auto name = local(run.name());
                if (name == "r" || name == "fld" || name == "br" || name == "tab")
                    changed = clear_local_text_property(child(run, "rPr"), property) || changed;
                else if (name == "endParaRPr")
                    changed = clear_local_text_property(run, property) || changed;
            }
        }
        if (!changed)
            throw std::runtime_error("没有可恢复的本页直接文字格式。");
        store(state, slide.source_part, document);
    }

    void patch_cell(Node tree, const mirrorfly::PresentationShape& shape,
        const mirrorfly::PresentationEditCommand& command)
    {
        auto cell = table_cell_node(tree, *shape.table_cell);
        if (!cell)
            throw std::runtime_error("表格单元格定位无效，原始内容已保留。");
        if (command.action == Action::FormatTableCell)
        {
            pugi::xml_document temporary;
            auto replacement = temporary.append_child("a:tcPr");
            auto color = replacement.append_child("a:solidFill").append_child("a:srgbClr");
            color.append_attribute("val") = shape.fill.color.substr(1).c_str();
            if (shape.fill.opacity < 1)
                color.append_child("a:alpha").append_attribute("val") =
                    static_cast<long long>(std::llround(shape.fill.opacity * 100000));
            copy_fill(ensure(cell, "a:tcPr"), replacement);
            return;
        }
        pugi::xml_document temporary;
        auto proxy = temporary.append_child("p:sp");
        proxy.append_copy(child(cell, "txBody"));
        patch_shape(proxy, shape, command);
        auto body = cell.insert_copy_before(child(proxy, "txBody"), child(cell, "txBody"));
        cell.remove_child(body.next_sibling());
        body.set_name("a:txBody");
        auto properties = ensure(cell, "a:tcPr");
        if (command.action == Action::FormatTextBox)
        {
            const std::pair<const char*, std::optional<double>> margins[]{{"marL", command.inset_left},
                {"marR", command.inset_right}, {"marT", command.inset_top}, {"marB", command.inset_bottom}};
            for (const auto& [name, value] : margins)
                if (value)
                    attribute(properties, name) = static_cast<long long>(std::llround(*value * 12700));
            if (command.vertical_alignment)
                attribute(properties, "anchor") = shape.text.vertical_alignment == "center" ? "ctr"
                    : shape.text.vertical_alignment == "bottom"                             ? "b"
                                                                                            : "t";
        }
        if (command.action == Action::FormatTextStyle && command.text_style.vertical)
            attribute(properties, "vert") = shape.text.vertical.c_str();
    }

    void append_shape(State& state, Node tree, mirrorfly::PresentationScene& scene,
        mirrorfly::PresentationShape& shape, const std::string& slide_path)
    {
        auto native = mirrorfly::make_presentation(mirrorfly::PresentationSlideLayout::Blank);
        native.slides.front().shapes.push_back(shape);
        for (const auto& image : scene.images)
            if (image.path == shape.image_path)
                native.images.push_back(image);
        const auto package = mirrorfly::serialize_presentation(native);
        if (package.error != mirrorfly::PresentationError::None)
            throw std::runtime_error(package.message);
        pugi::xml_document generated;
        for (const auto& part : package.parts)
            if (part.path == "ppt/slides/slide1.xml")
                generated.load_buffer(part.bytes.data(), part.bytes.size());
        auto node = tree.append_copy(first_shape(generated));
        shape.source_id = next_shape_id(tree);
        shape.source_part = slide_path;
        attribute(properties(node), "id") = shape.source_id.c_str();
        if (!shape.image_path.empty())
        {
            const auto& image = native.images.front();
            const auto id = append_image_resource(state, slide_path, image);
            attribute(child(child(node, "blipFill"), "blip"), "r:embed") = id.c_str();
        }
    }

    void replace_image(State& state, Node node, const mirrorfly::PresentationScene& scene,
        const mirrorfly::PresentationShape& shape, const std::string& slide_path)
    {
        const auto image = std::find_if(scene.images.begin(), scene.images.end(), [&shape](const auto& value)
        {
            return value.path == shape.image_path;
        });
        auto blip = child(child(node, "blipFill"), "blip");
        if (image == scene.images.end() || !blip)
            throw std::runtime_error("当前对象不是可替换图片。");
        const auto id = append_image_resource(state, slide_path, *image);
        attribute(blip, "r:embed") = id.c_str();
    }

    void patch_slide_list(State& state, const mirrorfly::PresentationScene& scene)
    {
        pugi::xml_document presentation;
        pugi::xml_document relations;
        load(state, state.presentation_part, presentation);
        load(state, relation_path(state.presentation_part), relations);
        namespaces(presentation.document_element());
        const auto list = child(presentation.document_element(), "sldIdLst");
        std::map<std::string, Node> old;
        unsigned long long next_id = 255;
        for (auto item : list.children())
        {
            if (local(item.name()) != "sldId")
                continue;
            next_id = std::max(next_id, item.attribute("id").as_ullong());
            std::string relation_id;
            for (auto attr : item.attributes())
                if (local(attr.name()) == "id" && std::strchr(attr.name(), ':'))
                    relation_id = attr.value();
            for (auto relation : relations.document_element().children())
                if (relation.attribute("Id").value() == relation_id)
                    old[absolute_target(state.presentation_part, relation.attribute("Target").value())
                            .substr(1)] = item;
        }
        pugi::xml_document new_list;
        auto output = new_list.append_child("p:sldIdLst");
        std::vector<std::string> numeric_ids;
        for (const auto& slide : scene.slides)
        {
            const auto found = old.find(slide.source_part);
            if (found != old.end())
            {
                output.append_copy(found->second);
                numeric_ids.emplace_back(found->second.attribute("id").value());
                continue;
            }
            const auto id = unique_relation(relations.document_element());
            auto relation = append_package_child(relations.document_element(), "Relationship");
            relation.append_attribute("Id") = id.c_str();
            relation.append_attribute("Type") = (std::string(relationship_ns) + "/slide").c_str();
            relation.append_attribute("Target") = ("/" + slide.source_part).c_str();
            auto item = output.append_child("p:sldId");
            item.append_attribute("id") = ++next_id;
            item.append_attribute("r:id") = id.c_str();
            numeric_ids.push_back(std::to_string(next_id));
        }
        replace_child(presentation.document_element(), "sldIdLst", output);
        mirrorfly::patch_presentation_sections(presentation.document_element(), scene, numeric_ids);
        store(state, state.presentation_part, presentation);
        store(state, relation_path(state.presentation_part), relations);
    }

    void patch_section_metadata(State& state, const mirrorfly::PresentationScene& scene)
    {
        pugi::xml_document presentation;
        load(state, state.presentation_part, presentation);
        std::vector<std::string> numeric_ids;
        for (auto item : child(presentation.document_element(), "sldIdLst").children())
            if (local(item.name()) == "sldId")
                numeric_ids.emplace_back(item.attribute("id").value());
        mirrorfly::patch_presentation_sections(presentation.document_element(), scene, numeric_ids);
        store(state, state.presentation_part, presentation);
    }

    void patch_click_action(State& state, const mirrorfly::PresentationScene& scene,
        const mirrorfly::PresentationEditCommand& command)
    {
        const auto& slide = scene.slides[command.slide_index];
        const auto& shape = slide.shapes[command.shape_index];
        if (slide.source_part.empty() || shape.source_part != slide.source_part)
            throw std::runtime_error("当前对象的原始页面无法安全编辑超链接。");
        pugi::xml_document document;
        load(state, slide.source_part, document);
        namespaces(document.document_element());
        const auto node =
            shape_node(child(child(document.document_element(), "cSld"), "spTree"), shape.source_id);
        auto non_visual = properties(node);
        if (!node || !non_visual)
            throw std::runtime_error("未找到需要设置超链接的原始对象。");
        non_visual.remove_child(child(non_visual, "hlinkClick"));
        for (auto paragraph : child(node, "txBody").children())
            if (local(paragraph.name()) == "p")
                for (auto run : paragraph.children())
                {
                    auto run_properties = child(run, "rPr");
                    if (!run_properties)
                        run_properties = child(run, "endParaRPr");
                    if (!run_properties && local(run.name()) == "endParaRPr")
                        run_properties = run;
                    if (run_properties)
                        run_properties.remove_child(child(run_properties, "hlinkClick"));
                }
        if (!command.click_kind.empty())
        {
            auto link = non_visual.append_child("a:hlinkClick");
            if (command.click_kind == "slide")
            {
                if (!command.click_target_slide || *command.click_target_slide >= scene.slides.size())
                    throw std::runtime_error("超链接目标页不存在。");
                const auto& target_path = scene.slides[*command.click_target_slide].source_part;
                if (target_path.empty() || !state.parts.count(target_path))
                    throw std::runtime_error("超链接目标页缺少文件部件。");
                pugi::xml_document relations;
                load(state, relation_path(slide.source_part), relations, true);
                auto root = relations.document_element();
                std::string id;
                for (auto relation : root.children())
                    if (local(relation.name()) == "Relationship" && !relation.attribute("TargetMode") &&
                        std::string(relation.attribute("Type").value()) ==
                            std::string(relationship_ns) + "/slide" &&
                        absolute_target(slide.source_part, relation.attribute("Target").value()) ==
                            "/" + target_path)
                    {
                        id = relation.attribute("Id").value();
                        break;
                    }
                if (id.empty())
                {
                    id = unique_relation(root);
                    auto relation = append_package_child(root, "Relationship");
                    relation.append_attribute("Id") = id.c_str();
                    relation.append_attribute("Type") = (std::string(relationship_ns) + "/slide").c_str();
                    relation.append_attribute("Target") = ("/" + target_path).c_str();
                    store(state, relation_path(slide.source_part), relations);
                }
                link.append_attribute("r:id") = id.c_str();
                link.append_attribute("action") = "ppaction://hlinksldjump";
            }
            else
            {
                link.append_attribute("r:id") = "";
                link.append_attribute("action") =
                    ("ppaction://hlinkshowjump?jump=" + command.click_kind).c_str();
            }
        }
        store(state, slide.source_part, document);
    }

    void replace_format_child(Node target, Node generated, const char* name)
    {
        remove_children(target, {name});
        if (const auto replacement = child(generated, name))
        {
            const auto anchor = ordered_anchor(target, name);
            if (anchor)
                target.insert_copy_before(replacement, anchor);
            else
                target.append_copy(replacement);
        }
    }

    void preserve_run_links(Node replacement, Node original)
    {
        for (const auto* name : {"hlinkClick", "hlinkMouseOver"})
        {
            remove_children(replacement, {name});
            if (const auto link = child(original, name))
                replacement.append_copy(link);
        }
    }

    void patch_format_brush(State& state, const mirrorfly::PresentationScene& before,
        const mirrorfly::PresentationScene& after, const mirrorfly::PresentationEditCommand& command)
    {
        const auto& source_slide = before.slides[command.format_source_slide];
        const auto& source_shape = source_slide.shapes[command.format_source_shape];
        const auto& target_slide = before.slides[command.slide_index];
        const auto& target_shape = target_slide.shapes[command.shape_index];
        if (source_shape.source_part != source_slide.source_part ||
            target_shape.source_part != target_slide.source_part || source_shape.source_id.empty() ||
            target_shape.source_id.empty())
            throw std::runtime_error("格式刷只支持页面内的可编辑对象。");
        pugi::xml_document document;
        load(state, target_slide.source_part, document);
        auto target =
            shape_node(child(child(document.document_element(), "cSld"), "spTree"), target_shape.source_id);
        if (!target || child(child(target, "spPr"), "effectDag") || child(child(target, "spPr"), "scene3d") ||
            child(child(target, "spPr"), "sp3d"))
            throw std::runtime_error("目标对象包含暂不能覆盖的复杂效果。");
        pugi::xml_document generated;
        prototype(after.slides[command.slide_index].shapes[command.shape_index], generated, false);
        const auto styled = first_shape(generated);
        auto properties = ensure(target, "p:spPr");
        const auto styled_properties = child(styled, "spPr");
        if (source_shape.image_path.empty())
        {
            remove_children(
                properties, {"noFill", "solidFill", "gradFill", "blipFill", "pattFill", "grpFill"});
            copy_fill(properties, styled_properties);
        }
        replace_format_child(properties, styled_properties, "ln");
        replace_format_child(properties, styled_properties, "effectLst");
        if (!source_shape.image_path.empty())
        {
            auto blip = child(child(target, "blipFill"), "blip");
            if (!blip)
                throw std::runtime_error("目标图片缺少可编辑内容。");
            const auto& styled_shape = after.slides[command.slide_index].shapes[command.shape_index];
            const double opacity = styled_shape.image_opacity;
            remove_children(blip, {"alphaModFix"});
            if (opacity < 0.999)
                blip.prepend_child("a:alphaModFix").append_attribute("amt") =
                    static_cast<long long>(std::llround(opacity * 100000));
        }
        else
        {
            auto body = child(target, "txBody");
            const auto styled_body = child(styled, "txBody");
            if (body && styled_body)
            {
                replace_format_child(body, styled_body, "bodyPr");
                const auto styled_paragraph = child(styled_body, "p");
                const auto styled_paragraph_properties = child(styled_paragraph, "pPr");
                auto styled_run_properties = child(child(styled_paragraph, "r"), "rPr");
                if (!styled_run_properties)
                    styled_run_properties = child(styled_paragraph, "endParaRPr");
                for (auto paragraph : body.children())
                {
                    if (local(paragraph.name()) != "p")
                        continue;
                    auto old_paragraph_properties = child(paragraph, "pPr");
                    if (styled_paragraph_properties)
                    {
                        Node replacement;
                        if (old_paragraph_properties)
                            replacement = paragraph.insert_copy_before(
                                styled_paragraph_properties, old_paragraph_properties);
                        else
                            replacement = paragraph.prepend_copy(styled_paragraph_properties);
                        const auto old_default = child(old_paragraph_properties, "defRPr");
                        if (child(old_default, "hlinkClick") || child(old_default, "hlinkMouseOver"))
                            preserve_run_links(ensure(replacement, "a:defRPr"), old_default);
                    }
                    if (old_paragraph_properties)
                        paragraph.remove_child(old_paragraph_properties);
                    for (auto run : paragraph.children())
                    {
                        const auto kind = local(run.name());
                        if (kind != "r" && kind != "fld" && kind != "br" && kind != "endParaRPr")
                            continue;
                        auto old_properties = kind == "endParaRPr" ? run : child(run, "rPr");
                        if (kind == "endParaRPr")
                        {
                            run.remove_attributes();
                            for (auto item = run.first_child(); item;)
                            {
                                const auto next = item.next_sibling();
                                if (local(item.name()) != "hlinkClick" &&
                                    local(item.name()) != "hlinkMouseOver")
                                    run.remove_child(item);
                                item = next;
                            }
                            if (styled_run_properties)
                            {
                                for (auto attribute : styled_run_properties.attributes())
                                    ::attribute(run, attribute.name()) = attribute.value();
                                for (auto item : styled_run_properties.children())
                                    if (local(item.name()) != "hlinkClick" &&
                                        local(item.name()) != "hlinkMouseOver")
                                    {
                                        auto anchor = child(run, "hlinkClick");
                                        if (!anchor)
                                            anchor = child(run, "hlinkMouseOver");
                                        if (anchor)
                                            run.insert_copy_before(item, anchor);
                                        else
                                            run.append_copy(item);
                                    }
                            }
                            continue;
                        }
                        if (!styled_run_properties)
                        {
                            if (old_properties)
                            {
                                old_properties.remove_attributes();
                                for (auto item = old_properties.first_child(); item;)
                                {
                                    const auto next = item.next_sibling();
                                    if (local(item.name()) != "hlinkClick" &&
                                        local(item.name()) != "hlinkMouseOver")
                                        old_properties.remove_child(item);
                                    item = next;
                                }
                            }
                            continue;
                        }
                        auto replacement = old_properties
                            ? run.insert_copy_before(styled_run_properties, old_properties)
                            : run.prepend_copy(styled_run_properties);
                        preserve_run_links(replacement, old_properties);
                        if (old_properties)
                            run.remove_child(old_properties);
                    }
                }
            }
        }
        store(state, target_slide.source_part, document);
    }

    void remap_click_targets(const mirrorfly::PresentationScene& before, mirrorfly::PresentationScene& after)
    {
        std::map<std::string, int> slide_indices;
        for (std::size_t index = 0; index < after.slides.size(); ++index)
            slide_indices.emplace(after.slides[index].source_part, static_cast<int>(index));
        const auto remap = [&](mirrorfly::PresentationClickAction& action)
        {
            if (action.kind != "slide")
                return;
            if (action.target_slide < 0 || action.target_slide >= static_cast<int>(before.slides.size()))
            {
                action = {};
                return;
            }
            const auto target = slide_indices.find(before.slides[action.target_slide].source_part);
            if (target == slide_indices.end())
                action = {};
            else
                action.target_slide = target->second;
        };
        for (auto& slide : after.slides)
            for (auto& shape : slide.shapes)
            {
                remap(shape.click_action);
                for (auto& paragraph : shape.text.paragraphs)
                    for (auto& run : paragraph.runs)
                        remap(run.click_action);
            }
    }

    void add_slide(State& state, mirrorfly::PresentationScene& scene, std::size_t index)
    {
        auto native = mirrorfly::make_presentation(mirrorfly::PresentationSlideLayout::Blank);
        native.width = scene.width;
        native.height = scene.height;
        native.slides.front() = scene.slides[index];
        const auto package = mirrorfly::serialize_presentation(native);
        if (package.error != mirrorfly::PresentationError::None)
            throw std::runtime_error(package.message);
        const auto marker = unique_path(state, "ppt/mirrorfly", "/ppt/slides/slide1.xml");
        const auto prefix = marker.substr(0, marker.size() - std::string("ppt/slides/slide1.xml").size());
        for (const auto& part : package.parts)
        {
            if (part.path == "[Content_Types].xml")
            {
                pugi::xml_document types;
                types.load_buffer(part.bytes.data(), part.bytes.size());
                for (auto item : types.document_element().children())
                    if (local(item.name()) == "Override")
                        content_type(state,
                            prefix + std::string(item.attribute("PartName").value()).substr(1),
                            item.attribute("ContentType").value());
            }
            else if (part.path != "_rels/.rels")
                state.parts[prefix + part.path] = std::make_shared<const std::string>(part.bytes);
        }
        auto& slide = scene.slides[index];
        slide.source_part = marker;
        for (std::size_t shape = 0; shape < slide.shapes.size(); ++shape)
        {
            slide.shapes[shape].source_part = marker;
            slide.shapes[shape].source_id = std::to_string(shape + 2);
        }
    }

    void duplicate_slide(State& state, mirrorfly::PresentationSlide& slide)
    {
        const auto original = slide.source_part;
        const auto copy_path = unique_path(state, "ppt/slides/mirrorfly", ".xml");
        state.parts[copy_path] = state.parts.at(original);
        const auto original_relations = relation_path(original);
        if (state.parts.count(original_relations))
        {
            pugi::xml_document relations;
            load(state, original_relations, relations);
            for (auto relation : relations.document_element().children())
            {
                if (local(relation.name()) != "Relationship")
                    continue;
                if (std::string(relation.attribute("TargetMode").value()) == "External")
                    continue;
                const auto target = absolute_target(original, relation.attribute("Target").value());
                attribute(relation, "Target") = target.c_str();
                const std::string type = relation.attribute("Type").value();
                if (type.size() < 11 || type.substr(type.size() - 11) != "/notesSlide")
                    continue;
                const auto note_path = target.substr(1);
                const auto new_note = unique_path(state, "ppt/notesSlides/mirrorfly", ".xml");
                state.parts[new_note] = state.parts.at(note_path);
                attribute(relation, "Target") = ("/" + new_note).c_str();
                content_type(state, new_note,
                    "application/vnd.openxmlformats-officedocument.presentationml.notesSlide+xml");
                if (state.parts.count(relation_path(note_path)))
                {
                    pugi::xml_document notes_relations;
                    load(state, relation_path(note_path), notes_relations);
                    for (auto item : notes_relations.document_element().children())
                    {
                        if (local(item.name()) != "Relationship")
                            continue;
                        if (std::string(item.attribute("TargetMode").value()) == "External")
                            continue;
                        const auto absolute = absolute_target(note_path, item.attribute("Target").value());
                        attribute(item, "Target") =
                            (absolute == "/" + original ? "/" + copy_path : absolute).c_str();
                    }
                    store(state, relation_path(new_note), notes_relations);
                }
            }
            store(state, relation_path(copy_path), relations);
        }
        content_type(
            state, copy_path, "application/vnd.openxmlformats-officedocument.presentationml.slide+xml");
        slide.source_part = copy_path;
        for (auto& group : slide.groups)
            if (group.source_part == original)
                group.source_part = copy_path;
        for (auto& shape : slide.shapes)
            if (shape.source_part == original)
                shape.source_part = copy_path;
    }

    void check_budget(const State& state)
    {
        if (state.parts.size() > mirrorfly::maximum_presentation_parts)
            throw std::runtime_error("演示文稿部件数量超过上限。");
        std::size_t bytes = 0;
        for (const auto& part : state.parts)
        {
            bytes += part.second->size();
            if (part.second->size() > mirrorfly::maximum_presentation_part_bytes ||
                bytes > mirrorfly::maximum_presentation_expanded_bytes)
                throw std::runtime_error("演示文稿内容超过资源上限。");
        }
    }

    std::string related_part(const State& state, const std::string& owner, const std::string& kind)
    {
        pugi::xml_document relations;
        load(state, relation_path(owner), relations);
        for (auto item : relations.document_element().children())
        {
            const std::string type = item.attribute("Type").value();
            if (type.size() < kind.size() + 1 ||
                type.compare(type.size() - kind.size(), kind.size(), kind) != 0 ||
                type[type.size() - kind.size() - 1] != '/' ||
                std::string(item.attribute("TargetMode").value()) == "External")
                continue;
            const auto target = absolute_target(owner, item.attribute("Target").value());
            if (target.size() < 2 || !state.parts.count(target.substr(1)))
                throw std::runtime_error("当前主题关联无效。");
            return target.substr(1);
        }
        throw std::runtime_error("当前幻灯片缺少可编辑的主题关联。");
    }

    void patch_theme(State& state, const mirrorfly::PresentationSlide& slide,
        const mirrorfly::PresentationEditCommand& command)
    {
        const auto layout = related_part(state, slide.source_part, "slideLayout");
        const auto master = related_part(state, layout, "slideMaster");
        const auto path = related_part(state, master, "theme");
        pugi::xml_document document;
        load(state, path, document);
        namespaces(document.document_element());
        const auto colors = child(child(document.document_element(), "themeElements"), "clrScheme");
        const auto fonts = child(child(document.document_element(), "themeElements"), "fontScheme");
        if ((!colors && !command.theme_colors.empty()) || (!fonts && !command.theme_fonts.empty()))
            throw std::runtime_error("当前主题缺少颜色方案。");
        for (const auto& [slot, color] : command.theme_colors)
        {
            auto entry = child(colors, slot);
            if (!entry)
                throw std::runtime_error("当前主题缺少颜色槽位。");
            for (auto item = entry.first_child(); item;)
            {
                const auto next = item.next_sibling();
                if (local(item.name()) == "srgbClr" || local(item.name()) == "sysClr" ||
                    local(item.name()) == "schemeClr" || local(item.name()) == "scrgbClr" ||
                    local(item.name()) == "hslClr" || local(item.name()) == "prstClr")
                    entry.remove_child(item);
                item = next;
            }
            entry.prepend_child("a:srgbClr").append_attribute("val") = color.substr(1).c_str();
        }
        for (const auto& [slot, family] : command.theme_fonts)
        {
            const bool major = slot.compare(0, 5, "major") == 0;
            const bool east_asian = slot.find("EastAsian") != std::string::npos;
            const auto entry =
                child(child(fonts, major ? "majorFont" : "minorFont"), east_asian ? "ea" : "latin");
            if (!entry)
                throw std::runtime_error("当前主题缺少字体槽位。");
            attribute(entry, "typeface") = family.c_str();
        }
        store(state, path, document);
    }

    void refresh_presentation_scene(const State& state, mirrorfly::PresentationScene& scene)
    {
        std::vector<mirrorfly::PresentationPart> parts;
        parts.reserve(state.parts.size());
        for (const auto& [path, bytes] : state.parts)
            parts.push_back({path, *bytes});
        auto parsed = mirrorfly::parse_presentation(std::move(parts));
        if (parsed.error != mirrorfly::PresentationError::None)
            throw std::runtime_error(parsed.message.empty() ? "主题重新解析失败。" : parsed.message);
        auto refreshed = std::move(parsed.scene);
        refreshed.native_editable = true;
        auto shared = std::make_shared<State>(state);
        for (auto& image : refreshed.images)
            if (const auto found = shared->parts.find(image.path); found != shared->parts.end())
                image.bytes = found->second;
        for (auto& media : refreshed.media)
            if (const auto found = shared->parts.find(media.path); found != shared->parts.end())
                media.bytes = found->second;
        for (auto& font : refreshed.embedded_fonts)
            if (const auto found = shared->parts.find(font.path); found != shared->parts.end())
                font.bytes = found->second;
        refreshed.source_package = std::move(shared);
        scene = std::move(refreshed);
    }

    Node nth_child(Node parent, const char* name, std::size_t index)
    {
        for (auto item : parent.children())
            if (local(item.name()) == name && index-- == 0)
                return item;
        return {};
    }

    Node append_blank_table_cell(Node row, std::size_t row_index)
    {
        auto cell = row.append_child("a:tc");
        auto body = cell.append_child("a:txBody");
        body.append_child("a:bodyPr");
        body.append_child("a:lstStyle");
        body.append_child("a:p").append_child("a:endParaRPr").append_attribute("lang") = "zh-CN";
        auto properties = cell.append_child("a:tcPr");
        if (row_index == 0 || row_index % 2 == 0)
        {
            auto color = properties.append_child("a:solidFill").append_child("a:schemeClr");
            color.append_attribute("val") = "accent1";
            color.append_child("a:tint").append_attribute("val") = row_index == 0 ? 80000 : 95000;
        }
        return cell;
    }

    void patch_table(State& state, const mirrorfly::PresentationScene& before,
        const mirrorfly::PresentationEditCommand& command)
    {
        const auto& slide = before.slides[command.slide_index];
        pugi::xml_document document;
        load(state, slide.source_part, document);
        namespaces(document.document_element());
        auto tree = child(child(document.document_element(), "cSld"), "spTree");
        if (!tree)
            throw std::runtime_error("当前页缺少对象树。");
        if (command.action == Action::InsertTable)
        {
            const auto id = next_shape_id(tree);
            const double x = command.x.value_or(before.width * 0.1);
            const double y = command.y.value_or(before.height * 0.2);
            const double width = command.width.value_or(before.width * 0.8);
            const double height = command.height.value_or(before.height * 0.45);
            auto frame = tree.append_child("p:graphicFrame");
            auto metadata = frame.append_child("p:nvGraphicFramePr");
            auto properties = metadata.append_child("p:cNvPr");
            properties.append_attribute("id") = id.c_str();
            properties.append_attribute("name") = "表格";
            metadata.append_child("p:cNvGraphicFramePr");
            metadata.append_child("p:nvPr");
            auto transform = frame.append_child("p:xfrm");
            auto offset = transform.append_child("a:off");
            offset.append_attribute("x") = static_cast<long long>(std::llround(x * 12700));
            offset.append_attribute("y") = static_cast<long long>(std::llround(y * 12700));
            auto extent = transform.append_child("a:ext");
            extent.append_attribute("cx") = static_cast<long long>(std::llround(width * 12700));
            extent.append_attribute("cy") = static_cast<long long>(std::llround(height * 12700));
            auto data = frame.append_child("a:graphic").append_child("a:graphicData");
            data.append_attribute("uri") = "http://schemas.openxmlformats.org/drawingml/2006/table";
            auto table = data.append_child("a:tbl");
            auto table_properties = table.append_child("a:tblPr");
            table_properties.append_attribute("firstRow") = "1";
            table_properties.append_attribute("bandRow") = "1";
            if (state.parts.find("ppt/tableStyles.xml") != state.parts.end())
            {
                pugi::xml_document styles;
                load(state, "ppt/tableStyles.xml", styles);
                const auto default_style = styles.document_element().attribute("def").value();
                if (*default_style)
                    table_properties.append_child("a:tableStyleId").text().set(default_style);
            }
            auto grid = table.append_child("a:tblGrid");
            const auto column_width =
                static_cast<long long>(std::llround(width * 12700 / command.table_columns));
            for (int column = 0; column < command.table_columns; ++column)
                grid.append_child("a:gridCol").append_attribute("w") = column_width;
            const auto row_height = static_cast<long long>(std::llround(height * 12700 / command.table_rows));
            for (int row_index = 0; row_index < command.table_rows; ++row_index)
            {
                auto row = table.append_child("a:tr");
                row.append_attribute("h") = row_height;
                for (int column = 0; column < command.table_columns; ++column)
                    append_blank_table_cell(row, static_cast<std::size_t>(row_index));
            }
        }
        else
        {
            const auto& shape = slide.shapes[command.shape_index];
            const auto& address = *shape.table_cell;
            auto frame = shape_node(tree, address.frame_id);
            auto table = child(child(child(frame, "graphic"), "graphicData"), "tbl");
            auto grid = child(table, "tblGrid");
            auto row = nth_child(table, "tr", address.row);
            auto cell = nth_child(row, "tc", address.xml_cell);
            if (!frame || frame.parent() != tree || !table || !grid || !row || !cell)
                throw std::runtime_error("表格单元格定位无效。");
            auto extent = child(child(frame, "xfrm"), "ext");
            std::size_t row_count = 0;
            std::size_t column_count = 0;
            for (auto item : table.children())
                row_count += local(item.name()) == "tr";
            for (auto item : grid.children())
                column_count += local(item.name()) == "gridCol";
            if (command.action == Action::InsertTableRow || command.action == Action::InsertTableColumn ||
                command.action == Action::DeleteTableRow || command.action == Action::DeleteTableColumn ||
                command.action == Action::MergeTableCell)
            {
                const auto cells = mirrorfly::presentation_table_cell_grid(table, column_count);
                const auto options =
                    mirrorfly::presentation_table_structure(cells).options(address.row, address.column);
                const bool allowed = (command.action == Action::InsertTableRow && options.insert_row) ||
                    (command.action == Action::InsertTableColumn && options.insert_column) ||
                    (command.action == Action::DeleteTableRow && options.delete_row) ||
                    (command.action == Action::DeleteTableColumn && options.delete_column) ||
                    (command.action == Action::MergeTableCell &&
                        ((command.table_direction == "right" && options.merge_right) ||
                            (command.table_direction == "down" && options.merge_down)));
                if (!allowed || address.xml_cell != address.column)
                    throw std::runtime_error("目标行列与已有合并区域相交，不能安全修改。");
            }
            if (command.action == Action::FormatTableStyle)
            {
                auto properties = child(table, "tblPr");
                if (!properties || (!child(properties, "tableStyleId") && !child(properties, "tableStyle")) ||
                    !command.table_style_options)
                    throw std::runtime_error("当前表格没有可编辑的整表样式。");
                const auto& options = *command.table_style_options;
                const std::pair<const char*, bool> flags[]{{"firstRow", options.first_row},
                    {"lastRow", options.last_row}, {"firstCol", options.first_column},
                    {"lastCol", options.last_column}, {"bandRow", options.band_rows},
                    {"bandCol", options.band_columns}};
                for (const auto& [name, enabled] : flags)
                    attribute(properties, name) = enabled ? "1" : "0";
            }
            else if (command.action == Action::InsertTableRow)
            {
                if (!extent || !extent.attribute("cy") || row_count >= 512 || column_count == 0 ||
                    column_count > 256 || (row_count + 1) * column_count > 2000)
                    throw std::runtime_error("表格行数或尺寸已达上限。");
                const auto height = row.attribute("h").as_llong();
                if (height <= 0 || extent.attribute("cy").as_llong() > 127000000 - height)
                    throw std::runtime_error("表格高度超出范围。");
                auto inserted = table.insert_child_after("a:tr", row);
                inserted.append_attribute("h") = height;
                for (std::size_t column = 0; column < column_count; ++column)
                    append_blank_table_cell(inserted, address.row + 1);
                attribute(extent, "cy") = extent.attribute("cy").as_llong() + height;
            }
            else if (command.action == Action::InsertTableColumn)
            {
                if (!extent || !extent.attribute("cx") || column_count >= 256 || row_count == 0 ||
                    row_count > 512 || (column_count + 1) * row_count > 2000)
                    throw std::runtime_error("表格列数或尺寸已达上限。");
                auto selected = nth_child(grid, "gridCol", address.column);
                const auto width = selected.attribute("w").as_llong();
                if (!selected || width <= 0 || extent.attribute("cx").as_llong() > 127000000 - width)
                    throw std::runtime_error("表格宽度超出范围。");
                grid.insert_copy_after(selected, selected);
                std::size_t row_index = 0;
                for (auto current : table.children())
                    if (local(current.name()) == "tr")
                    {
                        auto previous = nth_child(current, "tc", address.column);
                        std::size_t cells = 0;
                        for (auto item : current.children())
                            cells += local(item.name()) == "tc";
                        if (!previous || cells != column_count)
                            throw std::runtime_error("表格各行的列数不一致。");
                        auto blank = append_blank_table_cell(current, row_index++);
                        current.insert_move_after(blank, previous);
                    }
                attribute(extent, "cx") = extent.attribute("cx").as_llong() + width;
            }
            else if (command.action == Action::DeleteTableRow)
            {
                const auto height = row.attribute("h").as_llong();
                if (!extent || row_count <= 1 || height <= 0 || extent.attribute("cy").as_llong() <= height)
                    throw std::runtime_error("表格至少保留一行，且行高必须有效。");
                table.remove_child(row);
                attribute(extent, "cy") = extent.attribute("cy").as_llong() - height;
            }
            else if (command.action == Action::DeleteTableColumn)
            {
                auto selected = nth_child(grid, "gridCol", address.column);
                const auto width = selected.attribute("w").as_llong();
                if (!extent || column_count <= 1 || !selected || width <= 0 ||
                    extent.attribute("cx").as_llong() <= width)
                    throw std::runtime_error("表格至少保留一列，且列宽必须有效。");
                for (auto current : table.children())
                {
                    if (local(current.name()) != "tr")
                        continue;
                    std::size_t cells = 0;
                    for (auto item : current.children())
                        cells += local(item.name()) == "tc";
                    if (cells != column_count || !nth_child(current, "tc", address.column))
                        throw std::runtime_error("表格各行的列数不一致。");
                }
                for (auto current : table.children())
                    if (local(current.name()) == "tr")
                        current.remove_child(nth_child(current, "tc", address.column));
                grid.remove_child(selected);
                attribute(extent, "cx") = extent.attribute("cx").as_llong() - width;
            }
            else if (command.action == Action::MergeTableCell)
            {
                const bool right = command.table_direction == "right";
                auto next = right
                    ? nth_child(row, "tc", address.xml_cell + 1)
                    : nth_child(nth_child(table, "tr", address.row + 1), "tc", address.xml_cell);
                if (!next || mirrorfly::presentation_table_cell_has_text(next))
                    throw std::runtime_error("相邻单元格不存在或包含文字，不能直接合并。");
                attribute(cell, right ? "gridSpan" : "rowSpan") = 2;
                attribute(next, right ? "hMerge" : "vMerge") = "1";
            }
            else if (command.action == Action::UnmergeTableCell)
            {
                const auto cells = mirrorfly::presentation_table_cell_grid(table, column_count);
                const auto merge = mirrorfly::presentation_table_merge(cells, address.row, address.column);
                if (!merge || merge->cells.front() != cell || merge->row_span != address.row_span ||
                    merge->column_span != address.column_span)
                    throw std::runtime_error("合并单元格结构不适合安全拆分。");
                for (auto item : merge->cells)
                {
                    item.remove_attribute("gridSpan");
                    item.remove_attribute("rowSpan");
                    item.remove_attribute("hMerge");
                    item.remove_attribute("vMerge");
                }
            }
            else if (command.action == Action::FormatTableBorder)
            {
                auto properties = ensure(cell, "a:tcPr");
                for (std::size_t index = 0; index < mirrorfly::detail::presentation_table_edge_names.size();
                    ++index)
                {
                    if (!mirrorfly::detail::presentation_table_edge_selected(command.table_edge, index))
                        continue;
                    const auto* edge = mirrorfly::detail::presentation_table_edge_xml_names[index];
                    properties.remove_child(child(properties, edge));
                    auto line = properties.prepend_child((std::string("a:") + edge).c_str());
                    line.append_attribute("w") =
                        static_cast<long long>(std::llround(*command.outline_width * 12700));
                    line.append_child("a:solidFill").append_child("a:srgbClr").append_attribute("val") =
                        command.outline_color->substr(1).c_str();
                }
            }
            else if (command.action == Action::ResetTableCellFill)
            {
                auto properties = child(cell, "tcPr");
                if (!properties || !shape.table_cell->local_fill_override)
                    throw std::runtime_error("此单元格没有可恢复的直接底色。");
                remove_children(
                    properties, {"noFill", "solidFill", "gradFill", "blipFill", "pattFill", "grpFill"});
            }
            else if (command.action == Action::ResetTableBorder)
            {
                auto properties = child(cell, "tcPr");
                if (!properties || !shape.table_cell->local_border_override)
                    throw std::runtime_error("此单元格没有可恢复的直接边框。");
                bool changed = false;
                for (std::size_t index = 0; index < mirrorfly::detail::presentation_table_edge_names.size();
                    ++index)
                {
                    if (!mirrorfly::detail::presentation_table_edge_selected(command.table_edge, index))
                        continue;
                    const auto* edge = mirrorfly::detail::presentation_table_edge_xml_names[index];
                    auto line = child(properties, edge);
                    if (line)
                    {
                        properties.remove_child(line);
                        changed = true;
                    }
                }
                if (!changed)
                    throw std::runtime_error("指定边框没有可恢复的直接覆盖。");
            }
        }
        store(state, slide.source_part, document);
    }

    void patch_group(State& state, const mirrorfly::PresentationScene& before,
        const mirrorfly::PresentationEditCommand& command)
    {
        const auto& slide = before.slides[command.slide_index];
        pugi::xml_document document;
        load(state, slide.source_part, document);
        namespaces(document.document_element());
        auto tree = child(child(document.document_element(), "cSld"), "spTree");
        if (!tree)
            throw std::runtime_error("当前页缺少对象树。");
        mirrorfly::detail::patch_presentation_group(tree, slide, command);
        store(state, slide.source_part, document);
    }
}

namespace mirrorfly
{
    std::size_t presentation_source_metadata_bytes(const PresentationScene& scene)
    {
        if (!scene.source_package)
            return 0;
        std::size_t bytes = sizeof(PresentationPackageState);
        for (const auto& part : scene.source_package->parts)
        {
            bytes += part.first.size() + sizeof(part) + 4 * sizeof(void*);
            const auto dot = part.first.find_last_of('.');
            const auto suffix = dot == std::string::npos ? std::string{} : part.first.substr(dot);
            if (suffix == ".xml" || suffix == ".rels")
                bytes += part.second->size();
        }
        return bytes;
    }

    PresentationEditResult preserve_presentation_edit(const PresentationScene& before,
        PresentationScene& after, const PresentationEditCommand& command, PresentationEditResult result)
    {
        try
        {
            auto state = std::make_shared<PresentationPackageState>(*before.source_package);
            const auto action = command.action;
            if (action == Action::SetSlideTransition)
            {
                const auto& slide = before.slides[command.slide_index];
                pugi::xml_document document;
                load(*state, slide.source_part, document);
                namespaces(document.document_element());
                patch_presentation_transition(
                    document.document_element(), after.slides[command.slide_index].transition);
                store(*state, slide.source_part, document);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::ReorderGroup)
            {
                const auto& slide = before.slides[command.slide_index];
                pugi::xml_document document;
                load(*state, slide.source_part, document);
                auto root = document.document_element();
                namespaces(root);
                auto tree = child(child(root, "cSld"), "spTree");
                detail::patch_presentation_group_layer(tree, command.group_id, command.group_layer_position);
                store(*state, slide.source_part, document);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::ApplyTheme)
            {
                patch_theme(*state, before.slides[command.slide_index], command);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::ResetPlaceholderFill)
            {
                reset_placeholder_fill(*state, before.slides[command.slide_index],
                    before.slides[command.slide_index].shapes[command.shape_index]);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::ResetPlaceholderOutline)
            {
                reset_placeholder_outline(*state, before.slides[command.slide_index],
                    before.slides[command.slide_index].shapes[command.shape_index]);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::ResetTextInheritance)
            {
                reset_text_inheritance(*state, before.slides[command.slide_index],
                    before.slides[command.slide_index].shapes[command.shape_index],
                    *command.reset_text_property);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::ReplaceTextMatches)
            {
                patch_presentation_text_matches(*state, before, after);
                check_budget(*state);
                after.source_package = std::move(state);
                return result;
            }
            if (action == Action::InsertTable || action == Action::FormatTableStyle ||
                action == Action::InsertTableRow || action == Action::InsertTableColumn ||
                action == Action::DeleteTableRow || action == Action::DeleteTableColumn ||
                action == Action::MergeTableCell || action == Action::UnmergeTableCell ||
                action == Action::FormatTableBorder || action == Action::ResetTableCellFill ||
                action == Action::ResetTableBorder)
            {
                patch_table(*state, before, command);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::GroupAdjacent || action == Action::AddToGroup || action == Action::Ungroup)
            {
                patch_group(*state, before, command);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::CreateSection || action == Action::RenameSection ||
                action == Action::RemoveSection)
            {
                patch_section_metadata(*state, after);
                check_budget(*state);
                after.source_package = std::move(state);
                return result;
            }
            if (action == Action::SetClickAction)
            {
                patch_click_action(*state, after, command);
                check_budget(*state);
                refresh_presentation_scene(*state, after);
                return result;
            }
            if (action == Action::ApplyFormat)
            {
                patch_format_brush(*state, before, after, command);
                check_budget(*state);
                after.source_package = std::move(state);
                return result;
            }
            if (action == Action::AddSlide)
                add_slide(*state, after, result.slide_index);
            else if (action == Action::DuplicateSlide)
                duplicate_slide(*state, after.slides[result.slide_index]);
            if (action == Action::AddSlide || action == Action::DuplicateSlide ||
                action == Action::DeleteSlide || action == Action::MoveSlide)
            {
                patch_slide_list(*state, after);
                remap_click_targets(before, after);
            }
            else
            {
                auto& slide = after.slides[command.slide_index];
                pugi::xml_document document;
                load(*state, slide.source_part, document);
                auto root = document.document_element();
                namespaces(root);
                auto common = child(root, "cSld");
                auto tree = child(common, "spTree");
                if (action == Action::SetSlideHidden)
                    attribute(root, "show") = slide.hidden ? "0" : "1";
                else if (action == Action::SetBackground)
                {
                    common.remove_child(child(common, "bg"));
                    auto background = common.prepend_child("p:bg").append_child("p:bgPr");
                    background.append_child("a:solidFill").append_child("a:srgbClr").append_attribute("val") =
                        slide.background.color.substr(1).c_str();
                    background.append_child("a:effectLst");
                }
                else if (action == Action::MoveGroup)
                {
                    const auto group = shape_node(tree, command.group_id);
                    if (!group || local(group.name()) != "grpSp" || group.parent() != tree)
                        throw std::runtime_error("只能移动当前页的顶层组合。");
                    const auto transform = child(child(group, "grpSpPr"), "xfrm");
                    const auto offset = child(transform, "off");
                    if (!offset || !offset.attribute("x") || !offset.attribute("y"))
                        throw std::runtime_error("组合坐标缺失，原始内容已保留。");
                    const auto move = [&](const char* name, double points)
                    {
                        const auto current = offset.attribute(name).as_llong();
                        const auto delta = static_cast<long long>(std::llround(points * 12700));
                        if ((delta > 0 && current > std::numeric_limits<long long>::max() - delta) ||
                            (delta < 0 && current < std::numeric_limits<long long>::min() - delta))
                            throw std::runtime_error("组合坐标超出范围，原始内容已保留。");
                        attribute(offset, name) = current + delta;
                    };
                    move("x", command.x.value_or(0));
                    move("y", command.y.value_or(0));
                }
                else if (action == Action::AddShape || action == Action::AddText ||
                    action == Action::AddImage)
                    append_shape(*state, tree, after, slide.shapes[*result.shape_index], slide.source_part);
                else
                {
                    const auto& original = before.slides[command.slide_index].shapes[command.shape_index];
                    if (original.table_cell)
                    {
                        if (original.source_part != slide.source_part)
                            throw std::runtime_error("母版表格当前锁定。");
                        patch_cell(tree, slide.shapes[*result.shape_index], command);
                        store(*state, slide.source_part, document);
                        check_budget(*state);
                        after.source_package = std::move(state);
                        return result;
                    }
                    auto node = shape_node(tree, original.source_id);
                    if (original.source_part != slide.source_part || !node)
                        throw std::runtime_error("母版、组合内部或兼容对象当前锁定；其原始内容会完整保留。");
                    if (action == Action::DeleteShape)
                        node.parent().remove_child(node);
                    else if (action == Action::MoveShape)
                    {
                        const auto& target_shape =
                            before.slides[command.slide_index].shapes[*result.shape_index];
                        const auto target = shape_node(tree, target_shape.source_id);
                        if (target_shape.source_part != slide.source_part || !target ||
                            target.parent() != node.parent())
                            throw std::runtime_error("暂不能跨越母版或组合内部对象调整层次。");
                        Node copy;
                        if (*result.shape_index > command.shape_index)
                            copy = tree.insert_copy_after(node, target);
                        else
                            copy = tree.insert_copy_before(node, target);
                        if (!copy)
                            throw std::runtime_error("无法调整对象层次。");
                        tree.remove_child(node);
                    }
                    else
                    {
                        auto& shape = slide.shapes[*result.shape_index];
                        if (action == Action::DuplicateShape)
                        {
                            const auto id = next_shape_id(tree);
                            node = node.parent().insert_copy_after(node, node);
                            attribute(properties(node), "id") = id.c_str();
                            shape.source_id = id;
                        }
                        if (action == Action::ReplaceImage)
                            replace_image(*state, node, after, shape, slide.source_part);
                        else
                            patch_shape(node, shape, command);
                    }
                }
                store(*state, slide.source_part, document);
            }
            check_budget(*state);
            after.source_package = std::move(state);
            return result;
        }
        catch (const std::exception& error)
        {
            result.error = PresentationEditError::InvalidValue;
            result.message = error.what();
            return result;
        }
    }

    PresentationPackageResult preserved_presentation_package(const PresentationScene& scene)
    {
        PresentationPackageResult result;
        if (!scene.native_editable || !scene.source_package)
        {
            result.error = PresentationError::InvalidPackage;
            result.message = "请先创建可编辑副本。";
            return result;
        }
        try
        {
            check_budget(*scene.source_package);
            for (const auto& part : scene.source_package->parts)
                result.parts.push_back({part.first, *part.second});
        }
        catch (const std::exception& error)
        {
            result.parts.clear();
            result.error = PresentationError::TooLarge;
            result.message = error.what();
        }
        return result;
    }
}
