#pragma once

#include <algorithm>
#include <mirrorfly/word.hpp>
#include <string>

namespace word_list_test
{
    inline mirrorfly::OfficePart& part(std::vector<mirrorfly::OfficePart>& parts, const std::string& path)
    {
        return *std::find_if(parts.begin(), parts.end(), [&](const auto& value)
        {
            return value.path == path;
        });
    }
    inline std::vector<mirrorfly::OfficePart> fixture()
    {
        auto parts = mirrorfly::serialize_word(mirrorfly::WordDocument{}).parts;
        part(parts, "word/numbering.xml").bytes = R"xml(
<w:numbering xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:x="urn:keep">
<w:abstractNum w:abstractNumId="9" x:keep="abstract"><w:multiLevelType w:val="multilevel"/>
<w:lvl w:ilvl="0"><w:start w:val="3"/><w:numFmt w:val="upperRoman"/><w:lvlText w:val="(%1)"/>
<w:lvlJc w:val="left"/><w:pPr><w:tabs><w:tab w:val="num" w:pos="720"/></w:tabs>
<w:ind w:left="720" w:hanging="360"/></w:pPr></w:lvl>
<w:lvl w:ilvl="1"><w:start w:val="1"/><w:numFmt w:val="lowerLetter"/><w:lvlText w:val="%2)"/></w:lvl>
</w:abstractNum>
<w:num w:numId="11" x:keep="instance"><w:abstractNumId w:val="9"/>
<w:lvlOverride w:ilvl="0"><w:startOverride w:val="5"/></w:lvlOverride></w:num>
<w:num w:numId="12"><w:abstractNumId w:val="9"/>
<w:lvlOverride w:ilvl="0"><w:startOverride w:val="27"/><w:lvl w:ilvl="0">
<w:start w:val="9"/><w:numFmt w:val="upperLetter"/><w:lvlText w:val="%1."/>
</w:lvl></w:lvlOverride></w:num>
</w:numbering>)xml";
        part(parts, "word/document.xml").bytes = R"xml(
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:x="urn:keep"><w:body>
<w:p x:keep="paragraph"><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="11"/></w:numPr>
<w:keepNext/></w:pPr><w:r><w:rPr><w:b/></w:rPr><w:t>first</w:t></w:r></w:p>
<w:p><w:r><w:t>gap</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="11"/></w:numPr></w:pPr><w:r><w:t>second</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="12"/></w:numPr></w:pPr><w:r><w:t>other</w:t></w:r></w:p>
</w:body></w:document>)xml";
        return parts;
    }
}
