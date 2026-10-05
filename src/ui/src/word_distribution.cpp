#include "word_distribution.hpp"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextList>
#include <algorithm>

namespace
{
    class DistributionHighlighter final : public QSyntaxHighlighter
    {
    public:
        explicit DistributionHighlighter(QTextDocument* document) : QSyntaxHighlighter(document)
        {
            width_ = document->textWidth();
            setObjectName("mirrorflyWordDistribution");
            connect(document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                [this](const QSizeF&)
            {
                const auto width = this->document()->textWidth();
                if (width != width_ && !refreshing_)
                {
                    width_ = width;
                    refreshing_ = true;
                    rehighlight();
                    refreshing_ = false;
                }
            });
        }

    protected:
        void highlightBlock(const QString& text) override
        {
            const auto block = currentBlock();
            const auto format = block.blockFormat();
            if (!format.property(mirrorfly::word_distributed_property).toBool() || text.size() < 2)
                return;
            auto* device = document()->documentLayout()->paintDevice();
            const auto root = document()->rootFrame()->frameFormat();
            const auto font = document()->defaultFont();
            const auto dpi = QFontMetricsF(font, device).fontDpi() / QFontMetricsF(font).fontDpi();
            const auto list_indent = block.textList() ? block.textList()->format().indent() : 0;
            const auto width = document()->textWidth() - root.leftMargin() - root.rightMargin() -
                format.leftMargin() - format.rightMargin() -
                (format.indent() + list_indent) * document()->indentWidth() * dpi;
            if (width < 1)
                return;
            QList<QTextLayout::FormatRange> runs;
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto fragment = it.fragment();
                runs.push_back(
                    {fragment.position() - block.position(), fragment.length(), fragment.charFormat()});
            }
            QTextLayout layout(text, font, device);
            auto option = document()->defaultTextOption();
            option.setAlignment(Qt::AlignLeft);
            option.setTextDirection(block.textDirection());
            option.setTabs(format.tabPositions());
            layout.setTextOption(option);
            layout.setFormats(runs);
            layout.beginLayout();
            bool first = true;
            for (;;)
            {
                auto line = layout.createLine();
                if (!line.isValid())
                    break;
                const auto available = width - (first ? format.textIndent() : 0);
                first = false;
                line.setLineWidth(std::max(1.0, available));
                const auto start = line.textStart();
                auto end = start + line.textLength();
                while (end > start && text[end - 1].isSpace())
                    --end;
                const auto content = text.mid(start, end - start);
                QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, content);
                int last = 0, count = 0;
                for (auto next = boundary.toNextBoundary(); next >= 0; next = boundary.toNextBoundary())
                {
                    if (next < content.size())
                        last = next;
                    ++count;
                }
                if (count < 2 || last <= 0 || content.contains('\t'))
                    continue;
                QList<QTextLayout::FormatRange> fragments;
                for (const auto& run : runs)
                {
                    const auto begin = std::max(start, run.start);
                    const auto finish = std::min(end, run.start + run.length);
                    if (finish > begin)
                        fragments.push_back({begin - start, finish - begin, run.format});
                }
                const auto spaced = [&](qreal extra)
                {
                    QList<QTextLayout::FormatRange> result;
                    for (auto run : fragments)
                    {
                        if (run.start >= last)
                        {
                            result.push_back(run);
                            continue;
                        }
                        if (run.start + run.length > last)
                        {
                            result.push_back({last, run.start + run.length - last, run.format});
                            run.length = last - run.start;
                        }
                        const auto original = run.format.fontLetterSpacingType() == QFont::AbsoluteSpacing
                            ? run.format.fontLetterSpacing()
                            : 0;
                        run.format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                        run.format.setFontLetterSpacing(original + extra);
                        result.push_back(run);
                    }
                    return result;
                };
                const auto measured = [&](qreal extra)
                {
                    QTextLayout sample(content, font, device);
                    auto single = option;
                    single.setWrapMode(QTextOption::NoWrap);
                    sample.setTextOption(single);
                    sample.setFormats(spaced(extra));
                    sample.beginLayout();
                    auto measured_line = sample.createLine();
                    measured_line.setLineWidth(1000000);
                    sample.endLayout();
                    return measured_line.naturalTextWidth();
                };
                const auto natural = measured(0);
                if (natural >= available - 0.25)
                    continue;
                qreal low = 0, high = (available - natural) / (count - 1);
                // Leave a fraction of a pixel for Qt's fixed-point shaping arithmetic.
                for (int iteration = 0; iteration < 14; ++iteration)
                {
                    const auto middle = (low + high) / 2;
                    if (measured(middle) <= available - 0.25)
                        low = middle;
                    else
                        high = middle;
                }
                for (const auto& run : spaced(low))
                {
                    if (run.start >= last)
                        continue;
                    QTextCharFormat visual;
                    visual.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                    visual.setFontLetterSpacing(run.format.fontLetterSpacing());
                    setFormat(start + run.start, run.length, visual);
                }
            }
            layout.endLayout();
        }

    private:
        qreal width_ = -1;
        bool refreshing_ = false;
    };
}

namespace mirrorfly
{
    void install_word_distribution(QTextDocument& document)
    {
        if (!document.findChild<QSyntaxHighlighter*>("mirrorflyWordDistribution"))
            new DistributionHighlighter(&document);
        refresh_word_distribution(document);
    }
    void refresh_word_distribution(QTextDocument& document)
    {
        if (auto* highlighter = document.findChild<QSyntaxHighlighter*>("mirrorflyWordDistribution"))
            highlighter->rehighlight();
    }

    void refresh_word_distribution(QTextDocument& document, int start, int end)
    {
        if (start < 0 || end < start || end >= document.characterCount())
            return;
        auto* highlighter = document.findChild<QSyntaxHighlighter*>("mirrorflyWordDistribution");
        if (!highlighter)
            return;
        const auto last = document.findBlock(std::max(start, end - 1));
        for (auto block = document.findBlock(start); block.isValid(); block = block.next())
        {
            highlighter->rehighlightBlock(block);
            if (block == last)
                break;
        }
    }
}
