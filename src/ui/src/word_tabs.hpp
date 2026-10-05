#pragma once
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariantList>
#include <mirrorfly/word.hpp>

namespace mirrorfly
{
    void apply_word_tabs(QTextBlockFormat& format, const std::vector<WordTabStop>& tabs);
    std::vector<WordTabStop> word_tabs_from_format(const QTextBlockFormat& format);
    QVariantList inspect_word_tabs(const QTextBlockFormat& format);
    bool format_word_tabs(QTextDocument& document, QTextCursor& cursor, const QVariant& value);
    bool word_has_tab_decoration(const QTextBlock& block);
    void append_word_tab_leaders(
        QVariantList& result, const QTextDocument& document, const QTextBlock& block, const QRectF& bounds);
}
