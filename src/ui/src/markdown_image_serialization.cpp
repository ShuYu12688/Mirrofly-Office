#include "markdown_image.hpp"
#include <QTextBlock>
#include <QTextDocument>
#include <QUuid>
namespace mirrorfly
{
    std::vector<MarkdownInlineToken> protect_markdown_images(QTextDocument& document)
    {
        struct Range
        {
            int start;
            QTextCharFormat format;
        };
        std::vector<Range> ranges;
        for (auto block = document.begin(); block.isValid(); block = block.next())
            for (auto item = block.begin(); !item.atEnd(); ++item)
            {
                const auto fragment = item.fragment();
                if (fragment.isValid() && fragment.charFormat().isImageFormat())
                    for (int offset = 0; offset < fragment.length(); ++offset)
                        ranges.push_back({fragment.position() + offset, fragment.charFormat()});
            }
        std::vector<MarkdownInlineToken> tokens;
        for (auto item = ranges.rbegin(); item != ranges.rend(); ++item)
        {
            QString token;
            do
            {
                token = "MIRRORFLYIMAGE" + QUuid::createUuid().toString(QUuid::Id128);
            } while (document.toRawText().contains(token));
            std::string source;
            if (item->format.hasProperty(markdown_image_url_property))
                source = markdown_image_source(
                    item->format.stringProperty(markdown_image_url_property).toUtf8().toStdString(),
                    item->format.stringProperty(QTextFormat::ImageAltText).toUtf8().toStdString(),
                    item->format.stringProperty(QTextFormat::ImageTitle).toUtf8().toStdString());
            tokens.push_back({token, QString::fromStdString(source)});
            auto format = item->format;
            format.setObjectType(QTextFormat::NoObject);
            format.setFontFixedPitch(false);
            QTextCursor cursor(&document);
            cursor.setPosition(item->start);
            cursor.setPosition(item->start + 1, QTextCursor::KeepAnchor);
            cursor.insertText(token, format);
        }
        return tokens;
    }

    bool restore_markdown_image_tokens(QString& source, const std::vector<MarkdownInlineToken>& tokens)
    {
        for (const auto& item : tokens)
        {
            if (item.source.isEmpty())
                return false;
            int matches = 0;
            // Emphasis canonicalization may encode either boundary character of the placeholder.
            for (int mask = 0; mask < 4; ++mask)
            {
                auto token = item.token;
                if (mask & 2)
                    token = token.chopped(1) + "&#" + QString::number(token.back().unicode()) + ';';
                if (mask & 1)
                    token = "&#" + QString::number(token.front().unicode()) + ';' + token.mid(1);
                matches += source.count(token);
                source.replace(token, item.source);
            }
            if (matches != 1)
                return false;
        }
        return true;
    }
}
