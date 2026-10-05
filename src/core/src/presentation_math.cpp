#include "presentation_math.hpp"

#include <utf8/checked.h>

#include <cstring>

namespace
{
    using Node = pugi::xml_node;
    constexpr std::size_t maximum_math_text_bytes = 64 * 1024;

    std::string local_name(const char* name)
    {
        const auto* separator = std::strchr(name, ':');
        return separator ? separator + 1 : name;
    }

    Node child(Node parent, const std::string& name)
    {
        for (auto candidate : parent.children())
        {
            if (local_name(candidate.name()) == name)
            {
                return candidate;
            }
        }
        return {};
    }

    pugi::xml_attribute attribute(Node node, const std::string& name)
    {
        for (auto candidate : node.attributes())
        {
            if (local_name(candidate.name()) == name)
            {
                return candidate;
            }
        }
        return {};
    }

    void append_math(Node node, std::string& output, unsigned depth);

    void append_math_literal(std::string& output, const std::string& text)
    {
        if (output.size() + text.size() > maximum_math_text_bytes)
        {
            throw mirrorfly::presentation_math::Limit{"单个公式的文字近似超过预览限制。"};
        }
        output += text;
    }

    void append_math_child(Node parent, const char* name, std::string& output, unsigned depth)
    {
        if (const auto value = child(parent, name))
        {
            append_math(value, output, depth + 1);
        }
    }

    std::string math_property(Node properties, const char* name, const char* fallback = "")
    {
        if (const auto value = child(properties, name))
        {
            return attribute(value, "val").as_string(fallback);
        }
        return fallback;
    }

    bool is_combining_mark(const std::string& text)
    {
        if (text.empty())
        {
            return false;
        }
        auto current = text.begin();
        const auto codepoint = utf8::next(current, text.end());
        return current == text.end() &&
            ((codepoint >= 0x0300 && codepoint <= 0x036F) || (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||
                (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||
                (codepoint >= 0x20D0 && codepoint <= 0x20FF) || (codepoint >= 0xFE20 && codepoint <= 0xFE2F));
    }

    void append_math_script(Node node, std::string& output, unsigned depth, bool subscript, bool superscript)
    {
        append_math_child(node, "e", output, depth);
        if (subscript)
        {
            append_math_literal(output, "_(");
            append_math_child(node, "sub", output, depth);
            append_math_literal(output, ")");
        }
        if (superscript)
        {
            append_math_literal(output, "^(");
            append_math_child(node, "sup", output, depth);
            append_math_literal(output, ")");
        }
    }

    void append_math(Node node, std::string& output, unsigned depth)
    {
        if (depth > 32)
        {
            throw mirrorfly::presentation_math::Limit{"公式结构层级超过预览限制。"};
        }
        const std::string kind = local_name(node.name());
        if (kind == "t")
        {
            append_math_literal(output, node.text().as_string());
            return;
        }
        if (kind.size() >= 2 && kind.compare(kind.size() - 2, 2, "Pr") == 0)
        {
            return;
        }
        if (kind == "f")
        {
            append_math_literal(output, "(");
            append_math_child(node, "num", output, depth);
            append_math_literal(output, ")/(");
            append_math_child(node, "den", output, depth);
            append_math_literal(output, ")");
            return;
        }
        if (kind == "sSub" || kind == "sSup" || kind == "sSubSup")
        {
            append_math_script(node, output, depth, kind != "sSup", kind != "sSub");
            return;
        }
        if (kind == "sPre")
        {
            append_math_literal(output, "_(");
            append_math_child(node, "sub", output, depth);
            append_math_literal(output, ")^(");
            append_math_child(node, "sup", output, depth);
            append_math_literal(output, ")");
            append_math_child(node, "e", output, depth);
            return;
        }
        if (kind == "rad")
        {
            const auto degree = child(node, "deg");
            std::string degree_text;
            if (degree)
            {
                append_math(degree, degree_text, depth + 1);
            }
            append_math_literal(output, "\xE2\x88\x9A");
            if (!degree_text.empty())
            {
                append_math_literal(output, "[");
                append_math_literal(output, degree_text);
                append_math_literal(output, "]");
            }
            append_math_literal(output, "(");
            append_math_child(node, "e", output, depth);
            append_math_literal(output, ")");
            return;
        }
        if (kind == "acc")
        {
            const std::string mark = math_property(child(node, "accPr"), "chr", "\xCC\x82");
            if (!is_combining_mark(mark))
            {
                append_math_literal(output, mark + "(");
            }
            append_math_child(node, "e", output, depth);
            append_math_literal(output, is_combining_mark(mark) ? mark : ")");
            return;
        }
        if (kind == "bar")
        {
            const bool bottom = math_property(child(node, "barPr"), "pos", "top") == "bot";
            append_math_child(node, "e", output, depth);
            append_math_literal(output, bottom ? "\xCC\xB2" : "\xCC\x85");
            return;
        }
        if (kind == "d")
        {
            const auto properties = child(node, "dPr");
            const std::string begin = math_property(properties, "begChr", "(");
            const std::string end = math_property(properties, "endChr", ")");
            const std::string separator = math_property(properties, "sepChr", "|");
            append_math_literal(output, begin);
            bool first = true;
            for (const auto value : node.children())
            {
                if (local_name(value.name()) != "e")
                {
                    continue;
                }
                if (!first)
                {
                    append_math_literal(output, separator);
                }
                append_math(value, output, depth + 1);
                first = false;
            }
            append_math_literal(output, end);
            return;
        }
        if (kind == "nary")
        {
            const std::string symbol = math_property(child(node, "naryPr"), "chr", "\xE2\x88\xAB");
            append_math_literal(output, symbol);
            if (child(node, "sub"))
            {
                append_math_literal(output, "_(");
                append_math_child(node, "sub", output, depth);
                append_math_literal(output, ")");
            }
            if (child(node, "sup"))
            {
                append_math_literal(output, "^(");
                append_math_child(node, "sup", output, depth);
                append_math_literal(output, ")");
            }
            append_math_literal(output, "(");
            append_math_child(node, "e", output, depth);
            append_math_literal(output, ")");
            return;
        }
        if (kind == "func")
        {
            append_math_child(node, "fName", output, depth);
            append_math_literal(output, "(");
            append_math_child(node, "e", output, depth);
            append_math_literal(output, ")");
            return;
        }
        if (kind == "limLow" || kind == "limUpp")
        {
            append_math_child(node, "e", output, depth);
            append_math_literal(output, kind == "limLow" ? "_(" : "^(");
            append_math_child(node, "lim", output, depth);
            append_math_literal(output, ")");
            return;
        }
        if (kind == "groupChr")
        {
            const auto properties = child(node, "groupChrPr");
            const std::string mark = math_property(properties, "chr", "\xE2\x8F\x9F");
            const bool bottom = math_property(properties, "pos", "bot") == "bot";
            if (!bottom)
            {
                append_math_literal(output, mark);
            }
            append_math_literal(output, "(");
            append_math_child(node, "e", output, depth);
            append_math_literal(output, ")");
            if (bottom)
            {
                append_math_literal(output, mark);
            }
            return;
        }
        if (kind == "eqArr")
        {
            bool first = true;
            for (const auto row : node.children())
            {
                if (local_name(row.name()) != "e")
                {
                    continue;
                }
                if (!first)
                {
                    append_math_literal(output, "; ");
                }
                append_math(row, output, depth + 1);
                first = false;
            }
            return;
        }
        if (kind == "m" && child(node, "mr"))
        {
            append_math_literal(output, "[");
            bool first_row = true;
            for (const auto row : node.children())
            {
                if (local_name(row.name()) != "mr")
                {
                    continue;
                }
                if (!first_row)
                {
                    append_math_literal(output, "; ");
                }
                bool first_cell = true;
                for (const auto cell : row.children())
                {
                    if (local_name(cell.name()) != "e")
                    {
                        continue;
                    }
                    if (!first_cell)
                    {
                        append_math_literal(output, ", ");
                    }
                    append_math(cell, output, depth + 1);
                    first_cell = false;
                }
                first_row = false;
            }
            append_math_literal(output, "]");
            return;
        }
        for (const auto value : node.children())
        {
            append_math(value, output, depth + 1);
        }
    }

    bool contains_math(Node node, unsigned depth)
    {
        if (depth > 32)
        {
            return false;
        }
        if (local_name(node.name()) == "m" && (child(node, "oMath") || child(node, "oMathPara")))
        {
            return true;
        }
        for (const auto value : node.children())
        {
            if (contains_math(value, depth + 1))
            {
                return true;
            }
        }
        return false;
    }

}

namespace mirrorfly::presentation_math
{
    void append_text(pugi::xml_node node, std::string& output, unsigned depth)
    {
        append_math(node, output, depth);
    }

    bool contains(pugi::xml_node node, unsigned depth)
    {
        return contains_math(node, depth);
    }
}
