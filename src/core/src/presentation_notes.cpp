#include "presentation_notes.hpp"

#include <cstring>

namespace
{
    std::string local_name(const char* name)
    {
        const auto* separator = std::strchr(name, ':');
        return separator ? separator + 1 : name;
    }

    pugi::xml_node child(pugi::xml_node parent, const char* name)
    {
        for (auto item : parent.children())
            if (local_name(item.name()) == name)
                return item;
        return {};
    }
}

namespace mirrorfly
{
    std::string read_presentation_speaker_notes(pugi::xml_node notes)
    {
        std::string result;
        const auto tree = child(child(notes, "cSld"), "spTree");
        for (auto shape : tree.children())
        {
            if (local_name(shape.name()) != "sp")
                continue;
            const auto placeholder = child(child(shape, "nvSpPr"), "nvPr");
            const auto marker = child(placeholder, "ph");
            if (std::strcmp(marker.attribute("type").value(), "body") != 0)
                continue;
            for (auto paragraph : child(shape, "txBody").children())
            {
                if (local_name(paragraph.name()) != "p")
                    continue;
                std::string line;
                for (auto run : paragraph.children())
                {
                    const auto kind = local_name(run.name());
                    if (kind == "r" || kind == "fld")
                        line += child(run, "t").text().as_string();
                    else if (kind == "br")
                        line += '\n';
                }
                if (!line.empty())
                {
                    if (!result.empty())
                        result += '\n';
                    result += line;
                }
            }
        }
        return result;
    }
}
