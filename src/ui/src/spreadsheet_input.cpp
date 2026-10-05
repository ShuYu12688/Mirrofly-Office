#include "spreadsheet_input.hpp"
#include "spreadsheet_limits.hpp"

#include <QRegularExpression>

namespace mirrorfly
{
    bool spreadsheet_input_value(
        const QString& text, const QString& kind, SpreadsheetValue& value, QString& error)
    {
        if (!text.isValidUtf16() || text.size() > maximum_cell_input_units)
        {
            error = QStringLiteral("单元格输入最多 32767 个有效文本单元。");
            return false;
        }
        if (text.startsWith('=') && kind != QStringLiteral("text"))
        {
            if (kind != "auto" || !spreadsheet_formula_supported(text.mid(1).toStdString()))
            {
                error = QStringLiteral(
                    "支持算术、单元格引用和 SUM/AVERAGE/COUNT/MIN/MAX；请检查公式，或选择文本类型保留原文。");
                return false;
            }
            value = {SpreadsheetValueKind::Formula, text.mid(1).toStdString()};
            return true;
        }
        if (kind != "auto" && kind != "text" && kind != "number" && kind != "boolean")
        {
            error = QStringLiteral("输入类型无效。");
            return false;
        }
        value = {};
        value.text = text.toUtf8().toStdString();
        if (text.isEmpty())
            return true;
        static const QRegularExpression number(QStringLiteral("^-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?$"));
        int digits = 0;
        for (const auto character : text)
            digits += character.isDigit() ? 1 : 0;
        const bool automatic_number = kind == "auto" && digits <= 15 && number.match(text).hasMatch();
        const bool true_value = text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        const bool false_value = text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0;
        if (kind == "number" || automatic_number)
            value.kind = SpreadsheetValueKind::Number;
        else if (kind == "boolean" || (kind == "auto" && (true_value || false_value)))
        {
            value.kind = SpreadsheetValueKind::Boolean;
            if (true_value)
                value.text = "1";
            else if (false_value)
                value.text = "0";
        }
        else
            value.kind = SpreadsheetValueKind::Text;
        return true;
    }

    bool spreadsheet_paste_rows(const QString& text, QVector<QStringList>& rows, QString& error)
    {
        rows.clear();
        if (text.size() > 1024 * 1024 || !text.isValidUtf16() || text.toUtf8().size() > 1024 * 1024)
        {
            error = QStringLiteral("单次粘贴最多 1 MiB 文本。");
            return false;
        }
        QStringList row;
        QString value;
        bool quoted = false;
        bool closed = false;
        std::size_t cells = 0;
        for (qsizetype index = 0; index <= text.size(); ++index)
        {
            const bool end = index == text.size();
            const QChar character = end ? QChar('\n') : text[index];
            if (quoted)
            {
                if (end)
                {
                    error = QStringLiteral("粘贴内容的引号不完整。");
                    return false;
                }
                if (character == '"')
                {
                    if (index + 1 < text.size() && text[index + 1] == '"')
                    {
                        value += '"';
                        ++index;
                    }
                    else
                    {
                        quoted = false;
                        closed = true;
                    }
                }
                else
                    value += character;
                continue;
            }
            if (!closed && value.isEmpty() && character == '"')
            {
                quoted = true;
                continue;
            }
            if (character == '\t' || character == '\n' || character == '\r')
            {
                if (end && row.isEmpty() && value.isEmpty() && !closed && !rows.isEmpty())
                    break;
                row.append(value);
                value.clear();
                closed = false;
                if (++cells > maximum_spreadsheet_batch_cells)
                {
                    error = QStringLiteral("单次粘贴最多 4096 个单元格。");
                    return false;
                }
                if (character != '\t')
                {
                    if (!rows.isEmpty() && row.size() != rows.front().size())
                    {
                        error = QStringLiteral("粘贴区域每行的列数必须相同。");
                        return false;
                    }
                    rows.append(row);
                    row.clear();
                    if (!end && character == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
                        ++index;
                }
            }
            else if (closed)
            {
                error = QStringLiteral("引号后的粘贴分隔符无效。");
                return false;
            }
            else
                value += character;
        }
        return true;
    }

    QString spreadsheet_quote_cell(QString text)
    {
        if (text.contains('"') || text.contains('\t') || text.contains('\n') || text.contains('\r'))
        {
            text.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            return '"' + text + '"';
        }
        return text;
    }
}
