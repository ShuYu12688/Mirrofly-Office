#include "word_tab_fixture.hpp"
#include <iostream>
#include <limits>
#include <pugixml.hpp>

namespace
{
    using namespace mirrorfly;
    using word_list_test::part;
    int failures = 0;
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << message << '\n';
        }
    }
    void inherited()
    {
        const auto original = word_tab_test::fixture();
        const auto parsed = parse_word(original);
        check(parsed.success && parsed.document.default_tab_stop == 18,
            "settings relationship resolves custom default tab interval");
        if (!parsed.success)
            return;
        const auto& tabs = parsed.document.paragraphs[0].tabs;
        check(tabs.size() == 3 && tabs[0].position == 72 && tabs[0].alignment == "center" &&
                tabs[1].position == 144 && tabs[1].leader == "middleDot" && tabs[2].position == 180 &&
                parsed.document.paragraphs[1].tabs[1].position == 108,
            "all repeated tab children merge by position across defaults, style chain and direct clear");
        auto untouched = serialize_word(parsed.document);
        for (const auto& source : original)
            check(untouched.success && part(untouched.parts, source.path).bytes == source.bytes,
                "no-op preserves all original package bytes");
        for (bool clear : {false, true})
        {
            auto edited = parsed.document;
            edited.paragraphs[0].tabs = clear
                ? std::vector<WordTabStop>{}
                : std::vector<WordTabStop>{{90, "left", "dot"}, {180, "decimal", "none"}};
            auto saved = serialize_word(edited);
            const auto reopened = parse_word(saved.parts);
            check(saved.success && reopened.success &&
                    reopened.document.paragraphs[0].tabs.size() == (clear ? 0 : 2) &&
                    reopened.document.paragraphs[1].tabs.size() == 3 &&
                    reopened.document.default_tab_stop == 18,
                "replace and clear inherited stops persist without changing other paragraphs or default "
                "interval");
            auto parts = original;
            check(part(saved.parts, "word/tabs/styles.xml").bytes ==
                        part(parts, "word/tabs/styles.xml").bytes &&
                    part(saved.parts, "word/tabs/settings.xml").bytes ==
                        part(parts, "word/tabs/settings.xml").bytes,
                "shared style and document settings remain byte identical");
            pugi::xml_document xml;
            xml.load_string(part(saved.parts, "word/document.xml").bytes.c_str());
            const auto properties = xml.child("w:document").child("w:body").child("w:p").child("w:pPr");
            check(properties.child("w:keepNext") &&
                    std::string(properties.child("w:tabs").attribute("x:keep").value()) == "tabs" &&
                    std::string(properties.child("w:tabs")
                            .find_child_by_attribute("w:tab", "w:pos", "3600")
                            .attribute("x:keep")
                            .value()) == "stop",
                "local stop patch keeps extension attributes and unrelated paragraph properties");
        }
    }
    void authored()
    {
        WordDocument document;
        document.default_tab_stop = 24;
        document.paragraphs[0].runs = {{"left\t12.34\tend"}};
        for (const std::string alignment : {"left", "center", "right", "decimal", "bar"})
            for (const std::string leader : {"none", "dot", "hyphen", "underscore", "heavy", "middleDot"})
            {
                document.paragraphs[0].tabs = {{72.05, alignment, leader}, {144, "left", "none"}};
                const auto saved = serialize_word(document);
                const auto reopened = parse_word(saved.parts);
                check(saved.success && reopened.success && reopened.document.default_tab_stop == 24 &&
                        reopened.document.paragraphs[0].tabs[0].position == 72.05 &&
                        reopened.document.paragraphs[0].tabs[0].alignment == alignment &&
                        reopened.document.paragraphs[0].tabs[0].leader == leader &&
                        reopened.document.paragraphs[0].runs[0].text == "left\t12.34\tend",
                    "authored stops and leader metadata round trip without inserting fake text");
            }
        for (const double invalid : {std::numeric_limits<double>::infinity(), 0.01, 2000.0})
        {
            document.paragraphs[0].tabs = {{invalid, "left", "none"}};
            check(!serialize_word(document).success, "invalid tab positions fail atomically");
        }
        document.paragraphs[0].tabs = {{72, "left", "none"}, {72, "right", "none"}};
        check(!serialize_word(document).success, "duplicate positions reject");
    }
}
int run_word_tab_core_tests()
{
    inherited();
    authored();
    return failures ? 1 : 0;
}
int main()
{
    return run_word_tab_core_tests();
}
