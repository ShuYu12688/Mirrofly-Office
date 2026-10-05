#include "markdown_inline_literal.hpp"
#include "mirrorfly/markdown.hpp"
#include "mirrorfly/text.hpp"

#include <utf8/checked.h>

#include <algorithm>
#include <string_view>

namespace
{
    bool same_style(const mirrorfly::MarkdownInlineRun& first, const mirrorfly::MarkdownInlineRun& second)
    {
        return first.bold == second.bold && first.italic == second.italic && first.strike == second.strike &&
            first.code == second.code && first.hard_break == second.hard_break;
    }

    std::vector<std::string> markers(const mirrorfly::MarkdownInlineRun& run)
    {
        std::vector<std::string> result;
        if (run.strike)
            result.push_back("~~");
        if (run.bold)
            result.push_back("**");
        if (run.italic)
            result.push_back("_");
        return result;
    }

    void transition(
        std::string& output, std::vector<std::string>& active, const std::vector<std::string>& desired)
    {
        std::size_t shared = 0;
        while (shared < active.size() && shared < desired.size() && active[shared] == desired[shared])
            ++shared;
        for (auto index = active.size(); index > shared; --index)
            output += active[index - 1];
        for (auto index = shared; index < desired.size(); ++index)
            output += desired[index];
        active = desired;
    }
}

namespace mirrorfly
{
    std::string markdown_inline_label(const std::vector<MarkdownInlineRun>& input, bool table_cell)
    {
        std::vector<MarkdownInlineRun> runs;
        std::size_t bytes = 0;
        bool styled = false;
        for (const auto& run : input)
        {
            if (run.text.empty())
                continue;
            const bool valid_break = run.hard_break && !run.code &&
                std::all_of(run.text.begin(), run.text.end(), [](char value)
            {
                return value == '\n';
            });
            if ((run.hard_break && (!valid_break || table_cell)) ||
                run.text.size() > maximum_text_bytes - bytes ||
                !utf8::is_valid(run.text.begin(), run.text.end()) ||
                std::any_of(run.text.begin(), run.text.end(), [valid_break](unsigned char value)
            {
                return (value < 0x20 && value != '\t' && !(valid_break && value == '\n')) || value == 0x7F;
            }))
                return {};
            bytes += run.text.size();
            styled = styled || run.bold || run.italic || run.strike;
            if (!runs.empty() && same_style(runs.back(), run))
                runs.back().text += run.text;
            else
                runs.push_back(run);
        }
        std::string output;
        std::vector<std::string> active;
        for (const auto& run : runs)
        {
            if (run.hard_break)
            {
                transition(output, active, markers(run));
                for (std::size_t count = 0; count < run.text.size(); ++count)
                    output += "\\\n";
            }
            else if (run.code)
            {
                transition(output, active, markers(run));
                auto quoted = markdown_code_span(run.text);
                if (table_cell)
                    for (std::size_t index = 0; index < quoted.size(); ++index)
                        if (quoted[index] == '|')
                            quoted.insert(index++, 1, '\\');
                output += quoted;
            }
            else
            {
                auto first = run.text.begin();
                while (first != run.text.end())
                {
                    auto next = first;
                    if (!detail::whitespace(utf8::next(next, run.text.end())))
                        break;
                    first = next;
                }
                auto last = run.text.end();
                while (last != first)
                {
                    auto previous = last;
                    if (!detail::whitespace(utf8::prior(previous, run.text.begin())))
                        break;
                    last = previous;
                }
                if (first != run.text.begin())
                {
                    transition(output, active, markers(run));
                    output += detail::literal_whitespace(std::string(run.text.begin(), first));
                }
                if (first != last)
                {
                    transition(output, active, markers(run));
                    // Source punctuation at these boundaries makes flanking stable for crossing styles.
                    output += detail::boundary_literal(std::string(first, last), styled);
                }
                if (last != run.text.end())
                {
                    transition(output, active, markers(run));
                    output += detail::literal_whitespace(std::string(last, run.text.end()));
                }
            }
            if (output.size() > maximum_text_bytes)
                return {};
        }
        transition(output, active, {});
        return output.size() <= maximum_text_bytes ? output : std::string{};
    }
}
