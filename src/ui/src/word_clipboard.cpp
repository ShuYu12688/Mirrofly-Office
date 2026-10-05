#include "word_bridge.hpp"
#include "word_document.hpp"
#include "word_format_properties.hpp"
#include "word_lists.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTextCursor>
#include <QTextDocumentFragment>

namespace
{
    const QString mime_type = QStringLiteral("application/x-mirrorfly-word-v1");
    QTextBlockFormat transferable_paragraph_format(const QTextBlock& block)
    {
        auto format = block.blockFormat();
        // Object indexes identify lists in their owning QTextDocument, not transferable formatting.
        format.clearProperty(QTextFormat::ObjectIndex);
        format.clearProperty(mirrorfly::word_source_paragraph_property);
        return format;
    }
}

namespace mirrorfly
{
    bool WordBridge::copySelection(int start, int end, bool cut)
    {
        if (!active_ || locked() || !editor_ || start < 0 || end <= start || end >= editor_->characterCount())
            return false;
        QTextCursor cursor(editor_);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        auto text = cursor.selectedText();
        text.replace(QChar::ParagraphSeparator, '\n');
        const QTextDocumentFragment fragment(cursor);
        auto selected = create_word_document({});
        QTextCursor(selected.get()).insertFragment(fragment);
        // A fragment without its paragraph separator otherwise loses the first block's properties.
        QTextCursor(selected->begin())
            .mergeBlockFormat(transferable_paragraph_format(editor_->findBlock(start)));
        const auto extracted = extract_word_document(*selected);
        if (!extracted.success)
            return false;
        const auto package = serialize_word(extracted.document);
        if (!package.success)
            return false;
        QJsonArray parts;
        for (const auto& part : package.parts)
            parts.append(QJsonObject{
                {"path", QString::fromStdString(part.path)}, {"bytes", QString::fromStdString(part.bytes)}});
        const auto encoded = QJsonDocument(parts).toJson(QJsonDocument::Compact);
        const auto html = fragment.toHtml();
        if (encoded.size() > 1024 * 1024 || html.toUtf8().size() > 1024 * 1024)
        {
            setMessage(QStringLiteral("复制的文字与格式超过 1 MiB，请缩小选区。"));
            return false;
        }
        auto data = std::make_unique<QMimeData>();
        data->setText(text);
        data->setHtml(html);
        data->setData(mime_type, encoded);
        if (cut && !insertText(start, end, {}))
            return false;
        QGuiApplication::clipboard()->setMimeData(data.release());
        setMessage({});
        return true;
    }

    bool WordBridge::paste(int start, int end)
    {
        if (!active_ || locked() || read_only_ || !editor_ || start < 0 || end < start ||
            end >= editor_->characterCount())
            return false;
        const auto* mime = QGuiApplication::clipboard()->mimeData();
        if (!mime)
            return false;
        auto imported = create_word_document({});
        if (mime->hasFormat(mime_type))
        {
            const auto data = mime->data(mime_type);
            if (data.size() > 1024 * 1024)
                return false;
            const auto array = QJsonDocument::fromJson(data).array();
            if (array.isEmpty() || array.size() > 32)
                return false;
            std::vector<OfficePart> parts;
            for (const auto& item : array)
            {
                const auto part = item.toObject();
                if (!part.value("path").isString() || !part.value("bytes").isString())
                    return false;
                parts.push_back({part.value("path").toString().toStdString(),
                    part.value("bytes").toString().toStdString()});
            }
            const auto parsed = parse_word(parts);
            if (!parsed.success)
            {
                setMessage(QString::fromStdString(parsed.error));
                return false;
            }
            imported = create_word_document(parsed.document);
        }
        else if (mime->hasHtml())
        {
            const auto html = mime->html();
            if (!html.isValidUtf16() || html.toUtf8().size() > 1024 * 1024)
                return false;
            // WordEditorDocument denies external resources while interpreting clipboard HTML.
            imported->setHtml(html);
            const auto checked = extract_word_document(*imported);
            if (!checked.success)
            {
                setMessage(QStringLiteral("剪贴板包含无法保留的结构，可使用“仅粘贴文本”。"));
                return false;
            }
            imported = create_word_document(checked.document);
        }
        else
            return pastePlain(start, end);
        if (!rebase_word_lists(*imported, *editor_))
            return false;
        clear_word_package_identity(*imported);
        const QTextDocumentFragment fragment(imported.get());
        std::unique_ptr<QTextDocument> candidate(editor_->clone());
        QTextCursor proposed(candidate.get());
        proposed.setPosition(start);
        proposed.setPosition(end, QTextCursor::KeepAnchor);
        const auto target = editor_->findBlock(start);
        const bool replace_paragraph =
            start == target.position() && end == target.position() + target.length() - 1;
        proposed.insertFragment(fragment);
        if (replace_paragraph)
            QTextCursor(candidate->findBlock(start))
                .mergeBlockFormat(transferable_paragraph_format(imported->begin()));
        const auto checked = extract_word_document(*candidate, editor_);
        if (!checked.success)
        {
            setMessage(QString::fromStdString(checked.error));
            return false;
        }
        QTextCursor cursor(editor_);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        cursor.beginEditBlock();
        cursor.insertFragment(fragment);
        if (replace_paragraph)
            QTextCursor(editor_->findBlock(start))
                .mergeBlockFormat(transferable_paragraph_format(imported->begin()));
        cursor.endEditBlock();
        setMessage({});
        return true;
    }
}
