#pragma once

#include <QTextDocument>
#include <QTextFormat>
#include <QVariantList>
#include <mirrorfly/word.hpp>

namespace mirrorfly
{
    inline constexpr int word_ruby_property = QTextFormat::UserProperty + 34;
    inline constexpr int word_ruby_base_property = QTextFormat::UserProperty + 35;
    bool format_word_ruby(QTextDocument& document, int start, int end, const QString& value, bool automatic);
    void append_word_ruby_runs(WordParagraph& paragraph, const WordRun& run, const QTextCharFormat& format);
    QVariantList word_ruby_decorations(const QTextDocument& document, const QRectF& clip = {});
    void update_word_annotation_layout(QTextDocument& document);
}
