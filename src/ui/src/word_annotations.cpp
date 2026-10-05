#include "word_annotations.hpp"
#include "word_distribution.hpp"
#include "word_document.hpp"
#include "word_editor_document.hpp"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextLayout>
#include <algorithm>
#include <map>

namespace
{
    bool apply(QTextDocument& document, int start, int end, const QString& value, bool automatic)
    {
        using namespace mirrorfly;
        QTextCursor cursor(&document);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        if (!cursor.hasSelection())
            cursor.select(QTextCursor::BlockUnderCursor);
        const auto text = cursor.selectedText();
        if (!text.isValidUtf16())
            return false;
        if (!automatic && !value.isEmpty() && text.toUcs4().size() != 1)
            return false;
        if (text.size() > 2048)
            return false;
        if (!automatic && value.isEmpty())
        {
            QTextCharFormat format;
            format.setProperty(word_ruby_property, QString{});
            format.setProperty(word_ruby_base_property, QString{});
            cursor.beginEditBlock();
            cursor.mergeCharFormat(format);
            update_word_annotation_layout(document);
            cursor.endEditBlock();
            return true;
        }
        const auto begin = cursor.selectionStart();
        std::vector<std::pair<QTextCursor, QTextCharFormat>> changes;
        std::map<int, double> blocks;
        for (qsizetype offset = 0; offset < text.size();)
        {
            const auto units = text[offset].isHighSurrogate() ? 2 : 1;
            const auto base = text.mid(offset, units);
            auto ruby = automatic ? QString::fromStdString(word_pinyin(base.toUcs4().front())) : value;
            if (!ruby.isEmpty())
            {
                QTextCursor selected(&document);
                selected.setPosition(begin + static_cast<int>(offset));
                selected.setPosition(begin + static_cast<int>(offset) + units, QTextCursor::KeepAnchor);
                QTextCharFormat format;
                format.setProperty(word_ruby_property, ruby);
                format.setProperty(word_ruby_base_property, base);
                changes.emplace_back(selected, format);
                auto size = selected.charFormat().fontPointSize();
                if (size <= 0)
                    size = document.defaultFont().pointSizeF();
                blocks[selected.block().position()] =
                    std::max(blocks[selected.block().position()], size * 2 / 3);
            }
            offset += units;
        }
        if (changes.empty())
            return false;
        cursor.beginEditBlock();
        for (auto& change : changes)
            change.first.mergeCharFormat(change.second);
        for (const auto& [position, margin] : blocks)
        {
            QTextCursor paragraph(&document);
            paragraph.setPosition(position);
            auto format = paragraph.blockFormat();
            format.setLineHeight(200, QTextBlockFormat::ProportionalHeight);
            format.setTopMargin(std::max(format.topMargin(), margin));
            paragraph.setBlockFormat(format);
        }
        update_word_annotation_layout(document);
        cursor.endEditBlock();
        return true;
    }
}

namespace mirrorfly
{
    void update_word_annotation_layout(QTextDocument& document)
    {
        double margin = 0;
        const auto first = document.begin();
        for (auto it = first.begin(); !it.atEnd(); ++it)
        {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
            const auto base = format.property(word_ruby_base_property).toString();
            if (format.property(word_ruby_property).toString().isEmpty() || base.isEmpty() ||
                !fragment.text().contains(base))
                continue;
            auto font = format.font();
            auto size = format.fontPointSize();
            if (size <= 0)
                size = document.defaultFont().pointSizeF();
            font.setPointSizeF(size / 2);
            margin =
                std::max(margin, QFontMetricsF(font, document.documentLayout()->paintDevice()).height() + 2);
        }
        auto format = document.rootFrame()->frameFormat();
        if (format.topMargin() != margin)
        {
            format.setTopMargin(margin);
            document.rootFrame()->setFrameFormat(format);
        }
    }

    bool format_word_ruby(QTextDocument& document, int start, int end, const QString& value, bool automatic)
    {
        if (start < 0 || end < start || end >= document.characterCount() || !value.isValidUtf16() ||
            value.toUtf8().size() > 128 || value.contains('\n') || value.contains('\r') ||
            value.contains('\t'))
            return false;
        std::unique_ptr<QTextDocument> candidate(document.clone());
        if (!apply(*candidate, start, end, value, automatic) ||
            !extract_word_document(*candidate, &document).success)
            return false;
        return apply(document, start, end, value, automatic);
    }

    void append_word_ruby_runs(WordParagraph& paragraph, const WordRun& run, const QTextCharFormat& format)
    {
        const auto base = format.property(word_ruby_base_property).toString();
        const auto annotation = format.property(word_ruby_property).toString();
        if (base.isEmpty() || annotation.isEmpty())
        {
            paragraph.runs.push_back(run);
            return;
        }
        const auto text = QString::fromStdString(run.text);
        qsizetype offset = 0;
        while (offset < text.size())
        {
            const auto found = text.indexOf(base, offset);
            const auto next = found < 0 ? text.size() : found;
            if (next > offset)
            {
                auto plain = run;
                plain.text = text.mid(offset, next - offset).toStdString();
                plain.ruby.clear();
                paragraph.runs.push_back(std::move(plain));
            }
            if (found < 0)
                break;
            auto annotated = run;
            annotated.text = base.toStdString();
            annotated.ruby = annotation.toStdString();
            paragraph.runs.push_back(std::move(annotated));
            offset = next + base.size();
        }
    }

    QVariantList word_ruby_decorations(const QTextDocument& document, const QRectF& clip)
    {
        QVariantList result;
        for (const auto& block : word_decoration_blocks(document))
        {
            const auto rect = document.documentLayout()->blockBoundingRect(block);
            if (!clip.isEmpty() && !rect.adjusted(0, -rect.height(), 0, 0).intersects(clip))
                continue;
            const auto* layout = block.layout();
            if (!layout)
                continue;
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto fragment = it.fragment();
                const auto format = fragment.charFormat();
                const auto ruby = format.property(word_ruby_property).toString();
                const auto base = format.property(word_ruby_base_property).toString();
                if (ruby.isEmpty() || base.isEmpty())
                    continue;
                const auto text = fragment.text();
                for (qsizetype offset = text.indexOf(base); offset >= 0;
                    offset = text.indexOf(base, offset + base.size()))
                {
                    const auto position = fragment.position() - block.position() + static_cast<int>(offset);
                    const auto line = layout->lineForTextPosition(position);
                    if (!line.isValid())
                        continue;
                    const auto end = std::min(
                        position + static_cast<int>(base.size()), line.textStart() + line.textLength());
                    auto left = line.cursorToX(position), right = line.cursorToX(end);
                    if (block.blockFormat().property(word_distributed_property).toBool())
                    {
                        QRectF ink;
                        for (const auto& glyphs : layout->glyphRuns(position, end - position))
                        {
                            const auto indices = glyphs.glyphIndexes();
                            const auto positions = glyphs.positions();
                            for (qsizetype index = 0; index < indices.size() && index < positions.size();
                                ++index)
                                ink = ink.united(glyphs.rawFont()
                                        .boundingRect(indices[index])
                                        .translated(positions[index]));
                        }
                        if (!ink.isEmpty())
                        {
                            left = ink.left();
                            right = ink.right();
                        }
                    }
                    const auto width = std::abs(right - left);
                    auto font = format.font();
                    auto size = format.fontPointSize();
                    if (size <= 0)
                        size = document.defaultFont().pointSizeF();
                    font.setPointSizeF(size / 2);
                    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
                    const auto natural =
                        QFontMetricsF(font, document.documentLayout()->paintDevice()).horizontalAdvance(ruby);
                    font.setPointSizeF(
                        std::max(1.0, size / 2 * std::min(1.0, width / std::max(1.0, natural))));
                    const auto metrics = QFontMetricsF(font, document.documentLayout()->paintDevice());
                    auto color = format.foreground().color().name();
                    if (format.textOutline().style() != Qt::NoPen)
                        color = format.textOutline().color().name();
                    result.push_back(QVariantMap{{"kind", "ruby"}, {"ruby", ruby}, {"font", font},
                        {"x", rect.x() + std::min(left, right)},
                        {"y", rect.y() + line.y() - metrics.height()}, {"width", width},
                        {"height", metrics.height()}, {"color", color}, {"bottomOnly", false}});
                }
            }
        }
        return result;
    }
}
