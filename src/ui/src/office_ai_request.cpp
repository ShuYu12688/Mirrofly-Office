#include "office_ai_request.hpp"

#include <QRegularExpression>

#include <utility>

namespace
{
    QString command_text(QString text)
    {
        text = text.trimmed();
        // Small explicit command grammar. Unknown/conditional/descriptive clauses stay unconstrained.
        const QStringList prefixes{QStringLiteral("请帮我"), QStringLiteral("请你"), QStringLiteral("帮我"),
            QStringLiteral("首先"), QStringLiteral("然后"), QStringLiteral("最后"), QStringLiteral("接着"),
            QStringLiteral("继续"), QStringLiteral("请"), QStringLiteral("先"), QStringLiteral("再"),
            QStringLiteral("并"), QStringLiteral("直接")};
        bool removed = true;
        while (removed)
        {
            removed = false;
            for (const auto& prefix : prefixes)
                if (text.startsWith(prefix))
                {
                    text = text.mid(prefix.size()).trimmed();
                    removed = true;
                    break;
                }
        }
        const QRegularExpression english("^(?:please|first|then|finally|continue|also|and|now)\\b\\s*",
            QRegularExpression::CaseInsensitiveOption);
        while (true)
        {
            const auto match = english.match(text);
            if (!match.hasMatch())
                break;
            text = text.mid(match.capturedLength()).trimmed();
        }
        return text;
    }

    QStringList request_clauses(const QString& request)
    {
        return request.split(QRegularExpression(
            "[，,。；;！!？?\\n]|但是|但|\\bbut\\b", QRegularExpression::CaseInsensitiveOption));
    }

    bool work_command(const QString& text)
    {
        const auto command = command_text(text);
        static const QRegularExpression work(
            QStringLiteral("^(?:制作|创建|生成|新建|修改|编辑|插入|追加|完成|交付|输出)|") +
                QStringLiteral("^(?:create|make|write|generate|edit|modify|append|produce|deliver|bold)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        if (work.match(command).hasMatch())
            return true;
        static const QRegularExpression object_edit(
            QStringLiteral("^(?:只|仅)?(?:把|将)[^，,。；;！!？?\\n]{1,100}(?:加粗|修改|编辑|替换|改为)"));
        static const QRegularExpression negated(QStringLiteral("不要|不用|不需要|禁止|无需"));
        const auto match = object_edit.match(command);
        return match.hasMatch() && !negated.match(match.captured()).hasMatch();
    }

    bool save_command(const QString& text)
    {
        const auto command = command_text(text);
        if (command.startsWith(QStringLiteral("保存了")) || command.startsWith(QStringLiteral("保存成功")) ||
            command.startsWith(QStringLiteral("保存完成")))
            return false;
        static const QRegularExpression save(
            QStringLiteral("^(?:保存|存为|存到|存盘|写入文件|另存)|^(?:save)\\b|^(?:改为|调整为)|") +
                QStringLiteral("^(?:将|把)[^，,。；;！!？?\\n]{1,40}(?:保存|另存)"),
            QRegularExpression::CaseInsensitiveOption);
        return work_command(text) || save.match(command).hasMatch();
    }

    bool requested_save(const QString& request)
    {
        static const QRegularExpression save(QStringLiteral("保存|存为|存到|存盘|写入文件|\\bsave\\b"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression skip(
            QStringLiteral("(不要|不用|无需|暂不|不得|禁止|不需要|不必|不能|不许|不|") +
                QStringLiteral("(?<!分)别)\\s*(保存|存盘|另存)|\\b(do not|don't)\\s+save\\b"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression query(
            QStringLiteral("^(?:请|请你)?\\s*(?:检查|查询|读取|查看|解释|如何|怎么)|") +
                QStringLiteral("^(?:please\\s+)?(?:inspect|read|check|explain|how)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression also_save(
            QStringLiteral("(?:并|再|然后|之后|最后|接着)\\s*(?:保存|另存)|\\b(?:and|then)\\s+save\\b"),
            QRegularExpression::CaseInsensitiveOption);
        for (const auto& clause : request_clauses(request))
        {
            if (query.match(clause.trimmed()).hasMatch() && !also_save.match(clause).hasMatch())
                continue;
            if ((save_command(clause) || also_save.match(clause).hasMatch()) &&
                save.match(clause).hasMatch() && !skip.match(clause).hasMatch())
                return true;
        }
        return false;
    }

    QStringList requested_save_formats(const QString& request)
    {
        static const QRegularExpression separate(
            QStringLiteral("分别保存|各自保存|都(?:要|需)?保存|全部保存|save (?:each|both|all)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression single(
            QStringLiteral("只要|只(?:制作|保存)|仅(?:要|制作|保存)|\\b(?:only|just)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        const bool single_format = single.match(request).hasMatch();
        if (!separate.match(request).hasMatch() && !single_format)
            return {};
        bool direct = false;
        for (const auto& clause : request_clauses(request))
        {
            const auto command = command_text(clause);
            direct = direct || work_command(clause) || save_command(clause) ||
                command.startsWith(QStringLiteral("只要")) || command.startsWith("Only", Qt::CaseInsensitive);
        }
        if (!direct)
            return {};
        const std::pair<const char*, QString> formats[]{{"pptx", QStringLiteral("PPTX?|演示文稿|幻灯片")},
            {"mfg", QStringLiteral("思维导图|脑图|mindmap")}, {"docx", QStringLiteral("Word|DOCX")},
            {"xlsx", QStringLiteral("Excel|XLSX|电子表格|spreadsheet")}, {"pdf", QStringLiteral("PDF")},
            {"md", QStringLiteral("Markdown|\\.md\\b")},
            {"txt", QStringLiteral("\\bTXT\\b|纯文本|文本文件|native text|plain text")}};
        static const QRegularExpression excluded(
            QStringLiteral("不要|不用|无需|不需要|禁止|\\b(?:do not|don't|without|no need|not)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        QStringList positive_clauses;
        for (const auto& clause : request_clauses(request))
        {
            const auto match = excluded.match(clause);
            positive_clauses.append(match.hasMatch() ? clause.left(match.capturedStart()) : clause);
        }
        const QString positive_request = positive_clauses.join(' ');
        QStringList required;
        for (const auto& format : formats)
            if (QRegularExpression(format.second, QRegularExpression::CaseInsensitiveOption)
                    .match(positive_request)
                    .hasMatch())
                required.append(QString::fromLatin1(format.first));
        return required.size() > 1 || (single_format && required.size() == 1) ? required : QStringList{};
    }

    int slide_page_number(const QString& value)
    {
        bool numeric = false;
        const int arabic = value.toInt(&numeric);
        if (numeric)
            return arabic;
        const auto digit = [](QChar character)
        {
            if (character == QChar(u'两'))
                return 2;
            return static_cast<int>(QStringLiteral("一二三四五六七八九").indexOf(character)) + 1;
        };
        const int ten = value.indexOf(QChar(u'十'));
        if (ten >= 0)
            return (ten == 0 ? 1 : digit(value.front())) * 10 +
                (ten + 1 == value.size() ? 0 : digit(value.back()));
        return digit(value.front());
    }

    int requested_slide_pages(const QString& request)
    {
        const QString number = QStringLiteral(
            "(?<![0-9])(?<count>(?:[1-9][0-9]{0,2}|[一二三四五六七八九]?十[一二三四五六七八九]?|"
            "[一二两三四五六七八九]))");
        const auto options = QRegularExpression::CaseInsensitiveOption;
        QString presentation_prefix = QStringLiteral("(?:PPTX?|演示文稿|幻灯片)\\s*");
        presentation_prefix += QStringLiteral("(?:，|,|：|:)?\\s*(?:共|一共|总共|合计)?\\s*");
        const QRegularExpression patterns[]{
            QRegularExpression(
                number + QStringLiteral("\\s*(?:页|张)\\s*(?:的)?\\s*(?:PPTX?|演示文稿|幻灯片)"), options),
            QRegularExpression(presentation_prefix + number + QStringLiteral("\\s*(?:页|张)"), options)};
        int pages = 0;
        for (const auto& pattern : patterns)
        {
            auto matches = pattern.globalMatch(request);
            while (matches.hasNext())
            {
                const auto match = matches.next();
                const auto before = request.left(match.capturedStart("count")).trimmed();
                const auto after = request.mid(match.capturedEnd(0)).trimmed();
                const QRegularExpression negated(
                    QStringLiteral("不要|不用|无需|不需要|禁止|\\b(?:do not|don't|never)\\b"),
                    QRegularExpression::CaseInsensitiveOption);
                if (negated.match(before).hasMatch())
                    continue;
                if (before.endsWith(QChar(u'第')) || before.endsWith(QStringLiteral("至少")) ||
                    before.endsWith(QStringLiteral("不少于")) || before.endsWith(QStringLiteral("至多")) ||
                    before.endsWith(QStringLiteral("不超过")) || before.endsWith(QStringLiteral("大约")) ||
                    before.endsWith(QChar(u'约')) || after.startsWith(QStringLiteral("左右")) ||
                    after.startsWith(QStringLiteral("以内")) || after.startsWith(QStringLiteral("以上")) ||
                    after.startsWith(QStringLiteral("以下")))
                    continue;
                const int count = slide_page_number(match.captured("count"));
                if (count < 1 || count > 200 || (pages > 0 && pages != count))
                    return 0;
                pages = count;
            }
        }
        return pages;
    }

}

namespace mirrorfly
{
    QString office_ai_direct_request_text(const QString& request)
    {
        QString text = request;
        // Quoted text/code/block quotes are operands, never local completion requirements.
        const QRegularExpression quoted(
            QStringLiteral(
                "```[\\s\\S]*?(?:```|$)|`[^`\\n]*`|“[^”]*”|‘[^’]*’|「[^」]*」|『[^』]*』|《[^》]*》|") +
            QStringLiteral("\"[^\"]*\"|(?<![A-Za-z])'[^'\\n]*'(?![A-Za-z])"));
        auto matches = quoted.globalMatch(text);
        while (matches.hasNext())
        {
            const auto match = matches.next();
            for (int index = match.capturedStart(); index < match.capturedEnd(); ++index)
                if (text[index] != '\n')
                    text[index] = ' ';
        }
        const QRegularExpression block_quote("(?m)^\\s*>[^\\n]*");
        matches = block_quote.globalMatch(text);
        while (matches.hasNext())
        {
            const auto match = matches.next();
            text.replace(match.capturedStart(), match.capturedLength(), QString(match.capturedLength(), ' '));
        }
        return text;
    }

    void OfficeAiTaskConstraints::merge(const OfficeAiTaskConstraints& update)
    {
        if (update.save != Save::Unspecified)
            save = update.save;
        document_work = document_work || update.document_work;
        // A continuation can strengthen source protection; ambiguous language never removes it.
        preserve_original = preserve_original || update.preserve_original;
        preserve_current = preserve_current || update.preserve_current;
        if (update.file_count > 0)
        {
            file_count = update.file_count;
            if (save_formats.size() > file_count)
                save_formats.clear();
        }
        if (update.slide_pages > 0)
            slide_pages = update.slide_pages;
        if (!update.save_formats.isEmpty())
            save_formats = update.save_formats;
    }

    QJsonObject OfficeAiTaskConstraints::checkpoint() const
    {
        QString save_state = "unspecified";
        if (save == Save::Required)
            save_state = "required";
        else if (save == Save::Forbidden)
            save_state = "forbidden";
        return {{"recognition", "conservativeExplicitHumanConstraints"}, {"save", save_state},
            {"documentWork", document_work}, {"preserveOriginal", preserve_original},
            {"fileCount", file_count}, {"slidePages", slide_pages}};
    }

    OfficeAiTaskConstraints office_ai_task_constraints(const QString& source)
    {
        const QString request = office_ai_direct_request_text(source);
        OfficeAiTaskConstraints result;
        result.document_work = office_ai_requested_document_work(request);
        result.file_count = office_ai_requested_file_count(request);
        result.save_formats = requested_save_formats(request);
        const QRegularExpression negative_save(
            QStringLiteral(
                "(?:不要|不用|无需|暂不|不得|禁止|不需要|不必|不能|不许|不)\\s*(?:保存|存盘|另存)|") +
                QStringLiteral("\\b(?:do not|don't|no need to|never)\\s+save\\b"),
            QRegularExpression::CaseInsensitiveOption);
        for (const auto& clause : request_clauses(request))
        {
            const auto text = command_text(clause);
            if (requested_save(clause))
                result.save = OfficeAiTaskConstraints::Save::Required;
            else if (negative_save.match(text).hasMatch() &&
                (text.startsWith(QStringLiteral("不")) || text.startsWith(QStringLiteral("禁止")) ||
                    QRegularExpression(
                        "^(?:do not|don't|never|no need)\\b", QRegularExpression::CaseInsensitiveOption)
                        .match(text)
                        .hasMatch()))
                result.save = OfficeAiTaskConstraints::Save::Forbidden;
        }
        const QRegularExpression preserve(
            QStringLiteral(
                "保留(?:原[件稿文]|源文件)|(?:原[件稿文]|源文件)[^。！\\n]{0,12}(不变|不动|不改|不覆盖)|") +
                QStringLiteral(
                    "(?:不要|不得|不能|不许|禁止|别|不)[^。！\\n]{0,8}(?:覆盖|修改|改动|改写|动)") +
                QStringLiteral("\\s*(?:原[件稿文]|源文件)|") +
                QStringLiteral(
                    "另存|副本|preserve.{0,20}original|keep.{0,20}original|save\\s+as|\\bcopy\\b|") +
                QStringLiteral(
                    "(do not|don't|never).{0,20}(overwrite|(?:modify|change|touch).{0,12}(original|source))"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression preserve_current(
            QStringLiteral("另存|(?:保留|不要(?:覆盖|修改|改动))当前(?:文档|文件|副本)|") +
                QStringLiteral("(?:新的?|另一份|第二份)副本|save\\s+as|(?:new|another|second)\\s+copy|") +
                QStringLiteral("(?:preserve|keep).{0,12}current.{0,12}(?:file|document|copy)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression reject(
            QStringLiteral("不要|不用|无需|暂不|不得|禁止|不必|不需要|\\b(?:do not|don't|never|no need)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression query(
            QStringLiteral("^(?:请|请你)?\\s*(?:检查|查询|读取|查看|解释|如何|怎么)|") +
                QStringLiteral("^(?:please\\s+)?(?:inspect|read|check|explain|how)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        for (const auto& clause : request_clauses(request))
        {
            const auto text = command_text(clause);
            if (query.match(text).hasMatch())
                continue;
            const QRegularExpression preserve_command(
                QStringLiteral("^(?:保留|另存|原[件稿文]|源文件|不要|不得|不能|不许|禁止|别)|") +
                    QStringLiteral("^(?:preserve|keep|save|do not|don't|never)\\b"),
                QRegularExpression::CaseInsensitiveOption);
            if (!work_command(text) && !preserve_command.match(text).hasMatch())
                continue;
            const auto match = preserve.match(text);
            if (match.hasMatch() && !reject.match(text.left(match.capturedStart())).hasMatch())
                result.preserve_original = true;
            const auto current = preserve_current.match(text);
            if (current.hasMatch() && !reject.match(text.left(current.capturedStart())).hasMatch())
                result.preserve_current = true;
        }
        result.preserve_original = result.preserve_original || result.preserve_current;
        // Exact deck counts require a positive creation command, not a query, quotation or negation.
        int pages = 0;
        bool conflicting = false;
        for (const auto& clause : request_clauses(request))
        {
            const auto text = command_text(clause);
            if (!work_command(text) && !text.startsWith(QStringLiteral("改为")) &&
                !text.startsWith(QStringLiteral("调整为")))
                continue;
            const int count = requested_slide_pages(text);
            if (count > 0 && pages > 0 && count != pages)
                conflicting = true;
            if (count > 0)
                pages = count;
        }
        result.slide_pages = conflicting ? 0 : pages;
        return result;
    }

    bool office_ai_requested_document_work(const QString& source)
    {
        const auto request = office_ai_direct_request_text(source);
        static const QRegularExpression document(
            QStringLiteral("文档|表格|思维导图|脑图|幻灯片|演示文稿|Word|Excel|PPT|Markdown|") +
                QStringLiteral("\\b(?:document|sheet|spreadsheet|table|slides?|presentation|mindmap)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        for (const auto& clause : request_clauses(request))
            if (work_command(clause) && document.match(clause).hasMatch())
                return true;
        return false;
    }

    int office_ai_requested_file_count(const QString& source)
    {
        const auto request = office_ai_direct_request_text(source);
        static const QString pattern =
            QStringLiteral("(?<count>[1-9]|[一二两三四五六七八九])\\s*(?:份|个)\\s*(?:独立\\s*)?") +
            QStringLiteral("(?:文件|文档|成果)|\\b(?<english>[1-9]|one|two|three|four|five|six)\\s+") +
            QStringLiteral("(?:separate\\s+)?(?:files?|documents?)\\b");
        static const QRegularExpression count(pattern, QRegularExpression::CaseInsensitiveOption);
        // Only a clear positive delivery clause may impose an extra completion requirement.
        static const QRegularExpression boundary(
            QStringLiteral("[，,。;；!?！\n]|(?:but|instead)\\b"), QRegularExpression::CaseInsensitiveOption);
        static const QString negative_pattern = QStringLiteral("不要|不用|无需|暂不|不得|禁止|不必|") +
            QStringLiteral("\\b(?:do not|don't|without|no need)\\b");
        static const QString delivery_pattern = QStringLiteral("制作|创建|生成|新建|完成|交付|保存|输出|") +
            QStringLiteral("\\b(?:create|make|save|produce|deliver)\\b");
        static const QRegularExpression negative(negative_pattern, QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression delivery(delivery_pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match;
        auto matches = count.globalMatch(request);
        while (matches.hasNext())
        {
            const auto candidate = matches.next();
            const QString prefix = request.left(candidate.capturedStart()).trimmed();
            if (prefix.endsWith(QChar(u'第')) || (!prefix.isEmpty() && prefix.back().isDigit()))
                continue;
            const auto clauses = prefix.split(boundary);
            const QString clause = clauses.isEmpty() ? QString{} : clauses.last();
            const auto command = command_text(clause);
            const bool revise =
                command.startsWith(QStringLiteral("改为")) || command.startsWith(QStringLiteral("调整为"));
            if (negative.match(clause).hasMatch() ||
                ((!work_command(clause) && !save_command(clause)) || !delivery.match(clause).hasMatch()) &&
                    !revise)
                continue;
            match = candidate;
            break;
        }
        if (!match.hasMatch())
            return 0;
        QString value = match.captured("count");
        if (value.isEmpty())
            value = match.captured("english").toLower();
        bool numeric = false;
        const int number = value.toInt(&numeric);
        if (numeric)
            return number;
        if (value == QStringLiteral("一") || value == "one")
            return 1;
        const int chinese = QStringLiteral("二三四五六七八九").indexOf(value);
        if (chinese >= 0)
            return chinese + 2;
        if (value == QStringLiteral("两") || value == QStringLiteral("two"))
            return 2;
        const QStringList english{"three", "four", "five", "six"};
        const int index = english.indexOf(value);
        return index < 0 ? 0 : index + 3;
    }

}
