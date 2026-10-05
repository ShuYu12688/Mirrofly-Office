#pragma once

#include <mirrorfly/word.hpp>

#include <QTextDocument>
#include <QTextOption>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace mirrorfly
{
    // Editor resources are asynchronous; export keeps synchronous worker-thread rendering.
    // Prepare with the target control's layout options to avoid a second full layout on attachment.
    std::unique_ptr<QTextDocument> create_word_document(const WordDocument& source, qreal text_width = -1,
        bool asynchronous_images = false, const QTextOption& text_option = {},
        const std::function<void(std::size_t, std::size_t)>& progress = {});
    bool prepare_word_document_images(
        QTextDocument& document, const std::function<void(std::size_t, std::size_t)>& progress = {});
    // Distinguish resource-only repaint notifications from user edits.
    bool word_image_refresh_in_progress(const QTextDocument& document);
    // Consume object changes separately from plain typing so protected images cannot disappear silently.
    bool take_word_image_structure_change(QTextDocument& document);
    // Source supplies package provenance for an isolated QTextDocument preflight clone.
    WordResult extract_word_document(const QTextDocument& document, const QTextDocument* source = nullptr);
    // Clipboard imports keep styles, not source IDs from another package.
    void clear_word_package_identity(QTextDocument& document);
    QVariantMap inspect_word_document(const QTextDocument& document, int position);
    bool format_word_document(
        QTextDocument& document, int start, int end, const QString& action, const QVariant& value);
    bool insert_word_paragraphs(
        QTextDocument& document, std::size_t paragraph_index, const std::vector<WordParagraph>& paragraphs);
    bool paint_word_format(QTextDocument& document, int start, int end, const WordParagraph& sample);
    QVariantList word_paragraph_decorations(const QTextDocument& document, const QRectF& clip = {});
    void paint_word_decoration(QPainter& painter, const QVariantMap& decoration, qreal border_width = 1);
    bool sort_word_paragraphs(QTextDocument& document, int start, int end, const QString& order);
}
