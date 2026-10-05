#pragma once
#include <QTextCursor>
#include <QTextDocument>
#include <mirrorfly/word.hpp>
namespace mirrorfly
{
    int word_alignment_index(Qt::Alignment value);
    Qt::Alignment word_alignment(int value);
    QTextBlockFormat word_paragraph_format(const WordParagraph& paragraph);
    void extract_word_paragraph_format(const QTextBlockFormat& format, WordParagraph& paragraph);
    bool word_indent_range_valid(
        QTextDocument& document, const QTextCursor& cursor, bool setting_left, double value);
}
