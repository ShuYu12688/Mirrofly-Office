#include "word_numbering_read.hpp"
#include "word_xml.hpp"

namespace mirrorfly::word_detail
{
    namespace
    {
        using namespace word_xml;
        NumberingLevel read_level(pugi::xml_node node)
        {
            NumberingLevel result;
            result.marker = attribute(child(node, "numFmt"), "val").as_string("decimal");
            result.text = attribute(child(node, "lvlText"), "val").value();
            result.start = attribute(child(node, "start"), "val").as_int(0);
            if (result.marker == "bullet")
            {
                result.kind = WordListKind::Bullet;
                if (result.text == "•" || result.text == "●" || result.text == "\xEF\x82\xB7")
                    result.marker = "disc";
                else if (result.text == "◦" || result.text == "○")
                    result.marker = "circle";
                else if (result.text == "▪" || result.text == "■" || result.text == "\xEF\x82\xA7")
                    result.marker = "square";
            }
            return result;
        }
    }

    NumberingLevels read_numbering(const OfficePart* part)
    {
        NumberingLevels result;
        if (!part)
            return result;
        pugi::xml_document xml;
        if (!word_xml::read(part->bytes, xml) || !named(xml.document_element(), "numbering"))
            return result;
        std::map<int, std::map<int, NumberingLevel>> definitions;
        for (auto node : xml.document_element().children())
            if (named(node, "abstractNum"))
            {
                const int id = attribute(node, "abstractNumId").as_int(-1);
                for (auto level : node.children())
                    if (named(level, "lvl"))
                    {
                        const int index = attribute(level, "ilvl").as_int(-1);
                        if (id >= 0 && index >= 0 && index <= 8)
                            definitions[id][index] = read_level(level);
                    }
            }
        for (auto node : xml.document_element().children())
            if (named(node, "num"))
            {
                const int id = attribute(node, "numId").as_int(-1);
                const int abstract = attribute(child(node, "abstractNumId"), "val").as_int(-1);
                if (id <= 0 || !definitions.count(abstract))
                    continue;
                auto levels = definitions.at(abstract);
                for (auto override : node.children())
                    if (named(override, "lvlOverride"))
                    {
                        const int index = attribute(override, "ilvl").as_int(-1);
                        if (index < 0 || index > 8)
                            continue;
                        if (const auto level = child(override, "lvl"))
                            levels[index] = read_level(level);
                        if (const auto start = child(override, "startOverride"))
                            levels[index].start = attribute(start, "val").as_int(1);
                    }
                for (const auto& level : levels)
                    result[{id, level.first}] = level.second;
            }
        return result;
    }
}
