#pragma once

#include <QTextDocument>
#include <QTextFormat>

namespace mirrorfly
{
    inline constexpr int word_distributed_property = QTextFormat::UserProperty + 38;
    void install_word_distribution(QTextDocument& document);
    void refresh_word_distribution(QTextDocument& document);
    // Refresh only paragraphs intersecting the half-open edit range [start, end).
    void refresh_word_distribution(QTextDocument& document, int start, int end);
}
