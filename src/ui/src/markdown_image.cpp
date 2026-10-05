#include "markdown_image.hpp"
#include "markdown_code.hpp"
#include <QTextBlock>
#include <QTextDocument>
#include <QUuid>

namespace
{
    bool image_range(QTextCursor& cursor)
    {
        if (cursor.hasSelection() && cursor.selectionEnd() - cursor.selectionStart() != 1)
            return false;
        const int position = cursor.selectionStart();
        cursor.setPosition(position);
        if (cursor.document()->characterAt(position) != QChar::ObjectReplacementCharacter)
            return false;
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        return cursor.charFormat().isImageFormat();
    }
}
namespace mirrorfly
{
    PreparedMarkdownImages prepare_markdown_images(const QString& source)
    {
        PreparedMarkdownImages result;
        auto bytes = source.toUtf8().toStdString();
        for (const auto& image : markdown_images(bytes))
        {
            QString token;
            do
            {
                token = "MIRRORFLYIMAGE" + QUuid::createUuid().toString(QUuid::Id128);
            } while (source.contains(token));
            result.tokens.push_back({token, image});
        }
        for (auto item = result.tokens.rbegin(); item != result.tokens.rend(); ++item)
            bytes.replace(item->image.start, item->image.end - item->image.start, item->token.toStdString());
        result.source = QString::fromStdString(bytes);
        return result;
    }

    bool restore_markdown_images(
        QTextDocument& document, const PreparedMarkdownImages& prepared, const QVariantMap& theme)
    {
        for (const auto& token : prepared.tokens)
        {
            auto cursor = document.find(token.token, 0, QTextDocument::FindCaseSensitively);
            if (cursor.isNull())
                return false;
            insert_markdown_image(cursor, token.image, theme);
        }
        return true;
    }

    QVariantMap inspect_markdown_image(QTextCursor cursor)
    {
        const bool found = image_range(cursor);
        const auto format = cursor.charFormat();
        return {{"inImage", found},
            {"imageUrl", found ? format.stringProperty(markdown_image_url_property) : QString{}},
            {"imageAlt", found ? format.stringProperty(QTextFormat::ImageAltText) : QString{}},
            {"imageTitle", found ? format.stringProperty(QTextFormat::ImageTitle) : QString{}}};
    }

    bool edit_markdown_image(
        QTextCursor& cursor, const QString& action, const QVariantMap& options, const QVariantMap& theme)
    {
        if (code_frame(cursor) || code_block(cursor.block()) || cursor.hasComplexSelection() ||
            cursor.document()->findBlock(cursor.selectionStart()) !=
                cursor.document()->findBlock(cursor.selectionEnd()))
            return false;
        auto whole = cursor;
        const bool existing = image_range(whole);
        if (!existing && cursor.selectedText().contains(QChar::ObjectReplacementCharacter))
            return false;
        if (action == "removeImage")
        {
            if (!existing)
                return false;
            const auto alt = whole.charFormat().stringProperty(QTextFormat::ImageAltText);
            auto format = whole.charFormat();
            format.setObjectType(QTextFormat::NoObject);
            whole.insertText(alt, format);
            cursor = whole;
            return true;
        }
        for (const auto& key : {"url", "alt", "title"})
            if (options.contains(key) &&
                (options.value(key).metaType().id() != QMetaType::QString ||
                    !options.value(key).toString().isValidUtf16()))
                return false;
        MarkdownImageInfo image;
        image.url = options.value("url").toString().toUtf8().toStdString();
        image.title = options.value("title").toString().toUtf8().toStdString();
        const auto previous_alt =
            existing ? whole.charFormat().stringProperty(QTextFormat::ImageAltText) : cursor.selectedText();
        image.alt = options.value("alt", previous_alt).toString().toUtf8().toStdString();
        if (markdown_image_source(image.url, image.alt, image.title).empty())
            return false;
        if (existing)
            cursor = whole;
        insert_markdown_image(cursor, image, theme);
        return true;
    }
}
