#pragma once
#include "word_list_fixture.hpp"

namespace word_tab_test
{
    inline std::vector<mirrorfly::OfficePart> fixture()
    {
        using word_list_test::part;
        auto parts = mirrorfly::serialize_word(mirrorfly::WordDocument{}).parts;
        part(parts, "word/document.xml").bytes = R"xml(
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:x="urn:keep"><w:body>
<w:p x:keep="paragraph"><w:pPr><w:pStyle w:val="Derived"/><w:tabs x:keep="tabs">
<w:tab w:pos="2160" w:val="clear"/><w:tab w:pos="3600" w:val="right" w:leader="hyphen" x:keep="stop"/>
</w:tabs><w:keepNext/></w:pPr><w:r><w:t>Name</w:t><w:tab/><w:t>12.34</w:t><w:tab/><w:t>end</w:t></w:r></w:p>
<w:p><w:pPr><w:pStyle w:val="Derived"/></w:pPr><w:r><w:t>keep</w:t><w:tab/><w:t>100</w:t></w:r></w:p>
</w:body></w:document>)xml";
        auto& rels = part(parts, "word/_rels/document.xml.rels").bytes;
        rels.insert(rels.find("</Relationships>"),
            "<Relationship Id='styles' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles' "
            "Target='tabs/styles.xml'/>"
            "<Relationship Id='settings' "
            "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings' "
            "Target='tabs/settings.xml'/>");
        parts.push_back({"word/tabs/styles.xml", R"xml(
<q:styles xmlns:q="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<q:docDefaults><q:pPrDefault><q:pPr><q:tabs><q:tab q:pos="720" q:val="left"/>
<q:tab q:pos="1440" q:val="right" q:leader="dot"/></q:tabs></q:pPr></q:pPrDefault></q:docDefaults>
<q:style q:type="paragraph" q:styleId="Base"><q:pPr><q:tabs>
<q:tab q:pos="1440" q:val="center" q:leader="hyphen"/><q:tab q:pos="2160" q:val="left" q:leader="underscore"/>
</q:tabs></q:pPr></q:style>
<q:style q:type="paragraph" q:styleId="Derived"><q:basedOn q:val="Base"/><q:pPr><q:tabs>
<q:tab q:pos="720" q:val="clear"/><q:tab q:pos="2880" q:val="decimal" q:leader="middleDot"/>
</q:tabs></q:pPr></q:style></q:styles>)xml"});
        parts.push_back({"word/tabs/settings.xml", R"xml(
<q:settings xmlns:q="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<q:defaultTabStop q:val="360"/><q:compat/></q:settings>)xml"});
        return parts;
    }
}
