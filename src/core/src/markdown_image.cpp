#include "markdown_image.hpp"
#include <algorithm>

namespace mirrorfly
{
    std::string markdown_image_source(
        const std::string& url, const std::string& alt, const std::string& title)
    {
        auto suffix = markdown_link_suffix(url.empty() ? "x" : url, title);
        const auto label = markdown_link_label_literal(alt);
        if (suffix.empty() || (!alt.empty() && label.empty()))
            return {};
        if (url.empty())
            suffix.erase(1, 1);
        return "![" + label + "]" + suffix;
    }
}

namespace mirrorfly::detail
{
    MarkdownEdit edit_markdown_image(const std::string& source, std::size_t start, std::size_t end,
        const std::string& action, const MarkdownOptions& options)
    {
        const auto images = markdown_images(source);
        const auto existing = std::find_if(images.begin(), images.end(), [&](const auto& image)
        {
            return start >= image.start && start < image.end && end <= image.end;
        });
        std::string alt = options.image_alt_set ? options.image_alt : source.substr(start, end - start);
        if (existing != images.end())
        {
            start = existing->start;
            end = existing->end;
            if (!options.image_alt_set)
                alt = existing->alt;
        }
        else if (action == "removeImage" ||
            std::any_of(images.begin(), images.end(), [&](const auto& image)
        {
            return start < image.end && end > image.start;
        }))
            return {};
        MarkdownEdit edit;
        edit.start = start;
        edit.end = end;
        if (action == "removeImage")
        {
            edit.replacement = markdown_link_label_literal(alt);
            if (!alt.empty() && edit.replacement.empty())
                return {};
        }
        else
        {
            edit.replacement = markdown_image_source(options.url, alt, options.title);
            if (edit.replacement.empty())
                return {};
            // Parsing the candidate rejects code/destination/HTML locations and broken surrounding syntax.
            const auto candidate = source.substr(0, start) + edit.replacement + source.substr(end);
            const auto parsed = markdown_images(candidate);
            const auto found = std::find_if(parsed.begin(), parsed.end(), [&](const auto& image)
            {
                return image.start == start && image.end == start + edit.replacement.size() &&
                    image.url == options.url && image.title == options.title && image.alt == alt;
            });
            if (found == parsed.end() || parsed.size() != images.size() + (existing == images.end() ? 1 : 0))
                return {};
        }
        edit.selection_start = 0;
        edit.selection_end = edit.replacement.size();
        edit.valid = true;
        return edit;
    }
}
