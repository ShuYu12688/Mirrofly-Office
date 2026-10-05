#include "word_lists.hpp"
#include "word_format_properties.hpp"
#include <QJsonValue>
#include <QTextBlock>
#include <QTextDocument>
#include <algorithm>
#include <cmath>
#include <limits>

namespace mirrorfly
{
    namespace
    {
        const QMap<QString, QTextListFormat::Style> styles{{"disc", QTextListFormat::ListDisc},
            {"circle", QTextListFormat::ListCircle}, {"square", QTextListFormat::ListSquare},
            {"decimal", QTextListFormat::ListDecimal}, {"lowerLetter", QTextListFormat::ListLowerAlpha},
            {"upperLetter", QTextListFormat::ListUpperAlpha}, {"lowerRoman", QTextListFormat::ListLowerRoman},
            {"upperRoman", QTextListFormat::ListUpperRoman}};
        int next_instance(const QTextDocument& document)
        {
            int id = 0;
            for (auto block = document.begin(); block.isValid(); block = block.next())
                if (const auto* list = block.textList())
                    id = std::max(id, list->format().property(word_list_instance_property).toInt());
            return id < std::numeric_limits<int>::max() ? id + 1 : 0;
        }
        std::vector<QTextBlock> selected_blocks(const QTextDocument& document, const QTextCursor& cursor)
        {
            std::vector<QTextBlock> blocks;
            const auto last = std::max(cursor.selectionStart(), cursor.selectionEnd() - 1);
            for (auto block = document.findBlock(cursor.selectionStart());
                block.isValid() && block.position() <= last; block = block.next())
                blocks.push_back(block);
            return blocks;
        }
    }

    QTextListFormat word_list_format(const WordParagraph& paragraph)
    {
        QTextListFormat result;
        const auto marker = QString::fromStdString(paragraph.list_marker);
        const auto fallback =
            paragraph.list == WordListKind::Bullet ? QTextListFormat::ListDisc : QTextListFormat::ListDecimal;
        result.setStyle(styles.value(marker, fallback));
        result.setIndent(paragraph.list_level + 1);
        result.setStart(paragraph.list_start);
        result.setProperty(word_list_instance_property, paragraph.list_instance);
        result.setProperty(word_list_marker_property, marker);
        result.setProperty(word_list_text_property, QString::fromStdString(paragraph.list_text));
        const auto text = QString::fromStdString(paragraph.list_text);
        const auto token = QStringLiteral("%") + QString::number(paragraph.list_level + 1);
        const auto position = text.indexOf(token);
        if (paragraph.list != WordListKind::Bullet && position >= 0 && text.count('%') == 1)
        {
            result.setNumberPrefix(text.left(position));
            result.setNumberSuffix(text.mid(position + token.size()));
        }
        return result;
    }

    void extract_word_list(const QTextBlock& block, WordParagraph& paragraph)
    {
        paragraph.list = word_list_kind(block.textList());
        paragraph.list_level = 0;
        paragraph.list_instance = 0;
        paragraph.list_start = 1;
        paragraph.list_marker.clear();
        paragraph.list_text.clear();
        if (const auto* list = block.textList())
        {
            const auto format = list->format();
            paragraph.list_level = std::clamp(format.indent() - 1, 0, 2);
            paragraph.list_start = format.start();
            paragraph.list_instance = format.property(word_list_instance_property).toInt();
            paragraph.list_marker = format.hasProperty(word_list_marker_property)
                ? format.property(word_list_marker_property).toString().toStdString()
                : styles.key(format.style(), "decimal").toStdString();
            paragraph.list_text = format.property(word_list_text_property).toString().toStdString();
        }
    }

    QVariantMap inspect_word_list(const QTextBlock& block)
    {
        WordParagraph paragraph;
        extract_word_list(block, paragraph);
        const auto* list = block.textList();
        const auto marker = paragraph.list_marker.empty()
            ? (paragraph.list == WordListKind::Bullet ? "disc" : "decimal")
            : paragraph.list_marker;
        const int number = list ? list->itemNumber(block) + list->format().start() : 0;
        const auto text = QString::fromStdString(paragraph.list_text);
        const auto token = QStringLiteral("%") + QString::number(paragraph.list_level + 1);
        const bool roman = marker == "lowerRoman" || marker == "upperRoman";
        const bool letters = marker == "lowerLetter" || marker == "upperLetter";
        const bool template_supported = text.isEmpty() || paragraph.list == WordListKind::Bullet ||
            (text.count('%') == 1 && text.contains(token));
        const bool supported = !list ||
            (styles.contains(QString::fromStdString(marker)) && template_supported &&
                (!(roman || letters) || number >= 1) && (!roman || number <= 4999));
        return {{"marker", list ? QString::fromStdString(marker) : QString{}},
            {"start", paragraph.list_start}, {"number", number},
            {"label", list ? list->itemText(block) : QString{}}, {"template", text},
            {"displaySupported", supported}};
    }

    void WordListBuilder::append(QTextCursor& cursor, const WordParagraph& paragraph)
    {
        if (auto* existing = cursor.block().textList())
            existing->remove(cursor.block());
        if (paragraph.list == WordListKind::None)
        {
            current_ = nullptr;
            return;
        }
        const auto format = word_list_format(paragraph);
        if (paragraph.list_instance > 0)
        {
            // Parent advancement starts the next child sequence; a plain paragraph does not restart it.
            auto descendant = instances_.lower_bound({paragraph.list_instance, paragraph.list_level + 1});
            while (descendant != instances_.end() && descendant->first.first == paragraph.list_instance)
                descendant = instances_.erase(descendant);
            const auto found = instances_.find({paragraph.list_instance, paragraph.list_level});
            current_ = found == instances_.end() ? nullptr : found->second;
        }
        if (!current_ || current_->format().style() != format.style() ||
            current_->format().indent() != format.indent() || current_->format().start() != format.start() ||
            current_->format().property(word_list_instance_property) !=
                format.property(word_list_instance_property) ||
            current_->format().property(word_list_text_property) != format.property(word_list_text_property))
            current_ = cursor.createList(format);
        else
            current_->add(cursor.block());
        if (paragraph.list_instance > 0)
            instances_[{paragraph.list_instance, paragraph.list_level}] = current_;
    }

    bool rebase_word_lists(QTextDocument& imported, const QTextDocument& destination)
    {
        int next = next_instance(destination);
        std::map<QTextList*, int> lists;
        std::map<int, int> instances;
        for (auto block = imported.begin(); block.isValid(); block = block.next())
        {
            auto* list = block.textList();
            if (!list || lists.count(list))
                continue;
            auto format = list->format();
            const int old = format.property(word_list_instance_property).toInt();
            const auto found = instances.find(old);
            int id = old > 0 && found != instances.end() ? found->second : next;
            if (id <= 0 || id == std::numeric_limits<int>::max())
                return false;
            if (id == next)
                ++next;
            lists[list] = id;
            if (old > 0)
                instances[old] = id;
            format.setProperty(word_list_instance_property, id);
            list->setFormat(format);
        }
        return true;
    }

    bool format_word_list_variant(
        QTextDocument& document, QTextCursor& cursor, const QString& action, const QVariant& value)
    {
        const auto blocks = selected_blocks(document, cursor);
        if (blocks.empty())
            return false;
        WordParagraph paragraph;
        extract_word_list(blocks.front(), paragraph);
        if (action == "listMarker")
        {
            if (value.metaType().id() != QMetaType::QString || !styles.contains(value.toString()))
                return false;
            paragraph.list_marker = value.toString().toStdString();
            paragraph.list = QStringList{"disc", "circle", "square"}.contains(value.toString())
                ? WordListKind::Bullet
                : WordListKind::Numbered;
            paragraph.list_text.clear();
        }
        else if (action == "listStart")
        {
            const auto number = QJsonValue::fromVariant(value);
            if (!number.isDouble() || !std::isfinite(number.toDouble()) || number.toDouble() < 0 ||
                number.toDouble() > 1000000 || std::floor(number.toDouble()) != number.toDouble())
                return false;
            for (const auto& block : blocks)
                if (word_list_kind(block.textList()) != WordListKind::Numbered ||
                    block.textList() != blocks.front().textList())
                    return false;
            paragraph.list_start = static_cast<int>(number.toDouble());
        }
        else
            return false;
        const bool roman = paragraph.list_marker == "lowerRoman" || paragraph.list_marker == "upperRoman";
        const bool alphabet =
            paragraph.list_marker == "lowerLetter" || paragraph.list_marker == "upperLetter";
        if (((roman || alphabet) && paragraph.list_start < 1) ||
            (roman && paragraph.list_start + static_cast<int>(blocks.size()) - 1 > 4999))
            return false;
        paragraph.list_instance = next_instance(document);
        if (!paragraph.list_instance)
            return false;
        cursor.beginEditBlock();
        for (const auto& block : blocks)
            if (auto* list = block.textList())
                list->remove(block);
        QTextCursor first(blocks.front());
        auto* list = first.createList(word_list_format(paragraph));
        for (std::size_t index = 1; index < blocks.size(); ++index)
            list->add(blocks[index]);
        cursor.endEditBlock();
        return true;
    }
    QTextListFormat word_list_format(WordListKind kind, int level)
    {
        WordParagraph paragraph;
        paragraph.list = kind;
        paragraph.list_level = level;
        return word_list_format(paragraph);
    }

    mirrorfly::WordListKind word_list_kind(const QTextList* list)
    {
        if (!list)
        {
            return mirrorfly::WordListKind::None;
        }
        const auto style = list->format().style();
        return style == QTextListFormat::ListDisc || style == QTextListFormat::ListCircle ||
                style == QTextListFormat::ListSquare
            ? mirrorfly::WordListKind::Bullet
            : mirrorfly::WordListKind::Numbered;
    }

    bool format_word_list(QTextDocument& document, QTextCursor& cursor, int kind_value, int level)
    {
        if (kind_value < 0 || kind_value > 2 || level < 0 || level > 2)
        {
            return false;
        }
        const int first_position = cursor.selectionStart();
        const int last_position = std::max(cursor.selectionStart(), cursor.selectionEnd() - 1);
        std::vector<QTextBlock> blocks;
        for (auto block = document.findBlock(first_position); block.isValid(); block = block.next())
        {
            blocks.push_back(block);
            if (block.position() + block.length() - 1 >= last_position)
            {
                break;
            }
        }
        const auto kind = kind_value == 1 ? WordListKind::Bullet : WordListKind::Numbered;
        auto format = word_list_format(kind, level);
        if (!blocks.empty() && word_list_kind(blocks.front().textList()) == kind)
        {
            format = blocks.front().textList()->format();
            format.setIndent(level + 1);
        }
        cursor.beginEditBlock();
        for (const auto& block : blocks)
        {
            if (auto* current = block.textList())
            {
                current->remove(block);
            }
        }
        if (kind_value != 0 && !blocks.empty())
        {
            QTextCursor first(blocks.front());
            auto* list = first.createList(format);
            for (std::size_t index = 1; index < blocks.size(); ++index)
            {
                list->add(blocks[index]);
            }
        }
        cursor.endEditBlock();
        return true;
    }
}
