#pragma once

#include "markdown_break.hpp"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTextBlock>
#include <QVariantList>
#include <mirrorfly/markdown.hpp>
#include <vector>

namespace mirrorfly
{
    constexpr int markdown_container_property = QTextFormat::UserProperty + 68;
    constexpr int markdown_container_head_property = QTextFormat::UserProperty + 69;
    struct MarkdownContainerToken
    {
        QString token;
        QVariantList path;
        int heading = 0;
        bool empty_head = false;
    };
    void set_markdown_containers(const QTextBlock& block, const MarkdownParagraphInfo& paragraph);
    QVariantList markdown_container_values(const std::vector<MarkdownContainerInfo>& containers);
    bool load_markdown_block_containers(
        QTextDocument& document, const QString& source, const QVariantMap& theme);
    class MarkdownContainerState
    {
    public:
        QVariantList path(const QTextBlock& block);
        QVariantList path(const QTextBlock& block, QVariantList imported, int quote);

    private:
        QHash<qulonglong, QVariantList> owners_;
        std::vector<QVariantList> heads_;
        QSet<qulonglong> item_heads_;
    };
    bool restore_markdown_containers(QString& source, const std::vector<MarkdownContainerToken>& tokens);
    std::vector<MarkdownContainerToken> collect_markdown_containers(
        QTextDocument& document, const QStringList& identifiers);
    std::vector<MarkdownInlineToken> protect_markdown_empty_heads(QTextDocument& document);
    bool restore_markdown_block_tokens(QString& source, const std::vector<MarkdownInlineToken>& bodies,
        const std::vector<MarkdownContainerToken>& containers);
}
