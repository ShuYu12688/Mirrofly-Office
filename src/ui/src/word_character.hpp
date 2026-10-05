#pragma once

#include <QTextCharFormat>
#include <QTextDocument>
#include <mirrorfly/word.hpp>

namespace mirrorfly
{
    QString word_character_color(const QTextCharFormat& format);
    QTextCharFormat word_effect_format(bool outline, const QString& color);
    bool word_format_effect(
        QTextDocument& document, int start, int end, const QString& action, const QVariant& value);
    QTextCharFormat::VerticalAlignment word_script_alignment(int script);
    QTextCharFormat word_character_format(const WordRun& run);
    QTextCharFormat word_style_format(const WordRun& run);
}
