#include "word_character.hpp"
#include "word_annotations.hpp"
#include "word_format_properties.hpp"

#include <QColor>
#include <QPen>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <algorithm>

namespace mirrorfly
{
    namespace
    {
        constexpr int outline_property = QTextFormat::UserProperty + 36;
        constexpr int outline_color_property = QTextFormat::UserProperty + 37;
        constexpr int source_run_property = word_source_run_property;
        constexpr int character_border_property = word_character_border_property;
    }
    QString word_character_color(const QTextCharFormat& format)
    {
        if (format.property(outline_property).toBool())
            return format.property(outline_color_property).toString();
        return format.foreground().style() == Qt::NoBrush ? QString{} : format.foreground().color().name();
    }

    QTextCharFormat word_effect_format(bool outline, const QString& color)
    {
        QTextCharFormat format;
        format.setProperty(outline_property, outline);
        format.setProperty(outline_color_property, color);
        if (outline)
        {
            format.setTextOutline(QPen(color.isEmpty() ? QColor(Qt::black) : QColor(color), 0.7));
            format.setForeground(QColor(Qt::transparent));
        }
        else
        {
            format.setTextOutline(QPen(Qt::NoPen));
            format.setForeground(color.isEmpty() ? QBrush(Qt::NoBrush) : QBrush(QColor(color)));
        }
        return format;
    }

    bool word_format_effect(
        QTextDocument& document, int start, int end, const QString& action, const QVariant& value)
    {
        const auto color = value.toString();
        if (action == "color" && !color.isEmpty() &&
            (color.size() != 7 || !color.startsWith('#') || !QColor(color).isValid()))
            return false;
        QTextCursor cursor(&document);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        if (!cursor.hasSelection())
            cursor.select(QTextCursor::BlockUnderCursor);
        std::vector<std::pair<QTextCursor, QTextCharFormat>> changes;
        for (auto block = document.findBlock(cursor.selectionStart());
            block.isValid() && block.position() <= cursor.selectionEnd(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto fragment = it.fragment();
                const auto first = std::max(fragment.position(), cursor.selectionStart());
                const auto last = std::min(fragment.position() + fragment.length(), cursor.selectionEnd());
                if (last <= first)
                    continue;
                QTextCursor target(&document);
                target.setPosition(first);
                target.setPosition(last, QTextCursor::KeepAnchor);
                const auto format = fragment.charFormat();
                changes.emplace_back(target,
                    word_effect_format(
                        action == "outline" ? value.toBool() : format.property(outline_property).toBool(),
                        action == "color" ? color : word_character_color(format)));
            }
        cursor.beginEditBlock();
        for (auto& change : changes)
            change.first.mergeCharFormat(change.second);
        if (cursor.block().text().isEmpty())
        {
            const auto format = cursor.charFormat();
            cursor.mergeBlockCharFormat(word_effect_format(
                action == "outline" ? value.toBool() : format.property(outline_property).toBool(),
                action == "color" ? color : word_character_color(format)));
        }
        cursor.endEditBlock();
        return true;
    }

    QTextCharFormat::VerticalAlignment word_script_alignment(int script)
    {
        if (script > 0)
            return QTextCharFormat::AlignSuperScript;
        if (script < 0)
            return QTextCharFormat::AlignSubScript;
        return QTextCharFormat::AlignNormal;
    }

    QTextCharFormat word_character_format(const mirrorfly::WordRun& run)
    {
        QTextCharFormat format;
        format.setProperty(source_run_property, QVariant::fromValue<qulonglong>(run.source_id));
        // Latin glyphs must not be claimed by the East Asian fallback font.
        format.setFontFamilies(
            {QString::fromStdString(run.font), QString::fromStdString(run.east_asia_font)});
        format.setFontPointSize(run.size);
        format.setFontWeight(run.bold ? QFont::Bold : QFont::Normal);
        format.setFontItalic(run.italic);
        format.setFontUnderline(run.underline && !run.double_underline);
        format.setFontStrikeOut(run.strike && !run.double_strike);
        format.setProperty(word_double_underline_property, run.underline && run.double_underline);
        format.setProperty(word_double_strike_property, run.strike && run.double_strike);
        format.setVerticalAlignment(word_script_alignment(run.script));
        format.setProperty(character_border_property, QString::fromStdString(run.border_color));
        format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        format.setFontLetterSpacing(run.character_spacing * 4 / 3);
        format.setProperty(mirrorfly::word_ruby_property, QString::fromStdString(run.ruby));
        format.setProperty(mirrorfly::word_ruby_base_property,
            run.ruby.empty() ? QString{} : QString::fromStdString(run.text));
        format.merge(word_effect_format(run.outline, QString::fromStdString(run.color)));
        format.setBackground(run.background.empty() ? QBrush(Qt::NoBrush)
                                                    : QBrush(QColor(QString::fromStdString(run.background))));
        return format;
    }

    QTextCharFormat word_style_format(const mirrorfly::WordRun& run)
    {
        auto format = word_character_format(run);
        format.clearProperty(source_run_property);
        return format;
    }
}
