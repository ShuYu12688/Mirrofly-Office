#include "word_bridge.hpp"
#include "word_document.hpp"
#include "word_format_properties.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>

namespace mirrorfly
{
    bool WordBridge::formatReady() const
    {
        return format_sample_.has_value();
    }

    QVariantList WordBridge::paragraphDecorations() const
    {
        return editor_ ? word_paragraph_decorations(*editor_) : QVariantList{};
    }

    bool WordBridge::copyFormat(int position)
    {
        if (!active_ || locked() || !editor_ || position < 0 || position >= editor_->characterCount())
            return false;
        const auto block = editor_->findBlock(position);
        QTextCursor source(editor_);
        source.setPosition(position);
        QTextCharFormat character;
        character.merge(source.charFormat());
        character.setObjectType(QTextFormat::NoObject);
        auto selected_document = create_word_document({});
        selected_document->setDefaultFont(editor_->defaultFont());
        QTextCursor cursor(selected_document.get());
        cursor.setBlockFormat(block.blockFormat());
        cursor.insertText(QStringLiteral(" "), character);
        clear_word_package_identity(*selected_document);
        const auto document = extract_word_document(*selected_document);
        if (!document.success || document.document.paragraphs.empty())
            return false;
        auto sample = document.document.paragraphs.front();
        WordRun selected = sample.runs.empty() ? WordRun{} : sample.runs.front();
        selected.text.clear();
        selected.ruby.clear();
        selected.source_id = 0;
        selected.image_id = 0;
        sample.source_id = 0;
        sample.origin_id = 0;
        sample.runs = {selected};
        format_sample_ = std::move(sample);
        emit editorChanged();
        return true;
    }

    bool WordBridge::pasteFormat(int start, int end)
    {
        return active_ && !locked() && !read_only_ && editor_ && format_sample_ &&
            paint_word_format(*editor_, start, end, *format_sample_);
    }

    bool WordBridge::replaceAll(const QString& query, const QString& replacement)
    {
        if (!active_ || locked() || read_only_ || !editor_ || query.isEmpty() || !query.isValidUtf16() ||
            !replacement.isValidUtf16() || query.contains('\n'))
            return false;
        std::vector<QTextCursor> matches;
        QTextCursor found(editor_);
        while (!(found = editor_->find(query, found, QTextDocument::FindCaseSensitively)).isNull())
            matches.push_back(found);
        if (matches.empty())
        {
            setMessage(QStringLiteral("未找到匹配内容。"));
            return false;
        }
        const qint64 bytes = editor_->toPlainText().toUtf8().size() +
            static_cast<qint64>(matches.size()) * (replacement.toUtf8().size() - query.toUtf8().size());
        if (bytes > static_cast<qint64>(maximum_word_text_bytes))
            return false;
        std::unique_ptr<QTextDocument> candidate(editor_->clone());
        for (auto item = matches.rbegin(); item != matches.rend(); ++item)
        {
            QTextCursor cursor(candidate.get());
            cursor.setPosition(item->selectionStart());
            cursor.setPosition(item->selectionEnd(), QTextCursor::KeepAnchor);
            cursor.insertText(replacement);
        }
        if (!extract_word_document(*candidate, editor_).success)
            return false;
        QTextCursor transaction(editor_);
        transaction.beginEditBlock();
        for (auto item = matches.rbegin(); item != matches.rend(); ++item)
            item->insertText(replacement);
        transaction.endEditBlock();
        setMessage(QStringLiteral("已替换 %1 处。可撤销。 ").arg(matches.size()));
        return true;
    }
}
