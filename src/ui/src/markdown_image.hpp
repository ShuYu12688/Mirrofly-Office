#pragma once
#include "markdown_break.hpp"
#include <QTextCursor>
#include <QVariantMap>
#include <mirrorfly/markdown.hpp>
namespace mirrorfly
{
    constexpr int markdown_image_url_property = QTextFormat::UserProperty + 80;
    struct MarkdownImageToken
    {
        QString token;
        MarkdownImageInfo image;
    };
    struct PreparedMarkdownImages
    {
        QString source;
        std::vector<MarkdownImageToken> tokens;
    };
    PreparedMarkdownImages prepare_markdown_images(const QString& source);
    bool restore_markdown_images(
        QTextDocument& document, const PreparedMarkdownImages& prepared, const QVariantMap& theme);
    std::vector<MarkdownInlineToken> protect_markdown_images(QTextDocument& document);
    bool restore_markdown_image_tokens(QString& source, const std::vector<MarkdownInlineToken>& tokens);
    void insert_markdown_image(QTextCursor& cursor, const MarkdownImageInfo& image, const QVariantMap& theme);
    bool edit_markdown_image(
        QTextCursor& cursor, const QString& action, const QVariantMap& options, const QVariantMap& theme);
    QVariantMap inspect_markdown_image(QTextCursor cursor);
}
