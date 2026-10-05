#include "markdown_paragraph.hpp"
#include <QFont>
#include <QTextBlock>

namespace mirrorfly
{
    std::vector<MarkdownInlineRun> markdown_fragment_runs(const QTextFragment& fragment)
    {
        const auto format = fragment.charFormat();
        MarkdownInlineRun run{{}, format.fontWeight() >= QFont::Bold, format.fontItalic(),
            format.fontStrikeOut(), format.fontFixedPitch()};
        if (!format.boolProperty(markdown_hard_break_property))
        {
            run.text = fragment.text().toUtf8().toStdString();
            return {run};
        }
        std::vector<MarkdownInlineRun> result;
        const auto parts = fragment.text().split(QChar::LineSeparator);
        for (qsizetype index = 0; index < parts.size(); ++index)
        {
            if (index != 0)
            {
                auto line_break = run;
                line_break.text = "\n";
                line_break.code = false;
                line_break.hard_break = true;
                result.push_back(line_break);
            }
            if (!parts[index].isEmpty())
            {
                run.text = parts[index].toUtf8().toStdString();
                result.push_back(run);
            }
        }
        return result;
    }

}
