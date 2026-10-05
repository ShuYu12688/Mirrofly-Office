#include "document_read.hpp"
#include "text_bridge.hpp"
#include "text_search.hpp"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <mirrorfly/markdown.hpp>

namespace mirrorfly
{
    QString TextEditorBridge::content() const
    {
        return text_;
    }

    int TextEditorBridge::revision() const
    {
        return revision_;
    }

    QString TextEditorBridge::documentName() const
    {
        if (!path_.isEmpty())
        {
            return QFileInfo(path_).fileName();
        }
        return markdown_ ? QStringLiteral("未命名.md") : QStringLiteral("未命名.txt");
    }

    QString TextEditorBridge::documentPath() const
    {
        return path_;
    }

    QString TextEditorBridge::formatLabel() const
    {
        QString encoding;
        switch (format_.encoding)
        {
        case TextEncoding::Utf8:
            encoding = QStringLiteral("UTF-8");
            break;
        case TextEncoding::Utf8Bom:
            encoding = QStringLiteral("UTF-8 BOM");
            break;
        case TextEncoding::Utf16LittleEndian:
            encoding = QStringLiteral("UTF-16 LE");
            break;
        case TextEncoding::Utf16BigEndian:
            encoding = QStringLiteral("UTF-16 BE");
            break;
        }
        const QString ending = format_.line_ending == LineEnding::CrLf
            ? QStringLiteral("CRLF")
            : (format_.line_ending == LineEnding::Cr ? QStringLiteral("CR") : QStringLiteral("LF"));
        return encoding + QStringLiteral(" · ") + ending +
            (format_.mixed_line_endings ? QStringLiteral("（混合换行，保存时统一）") : QString{});
    }

    QUrl TextEditorBridge::saveUrl() const
    {
        if (!path_.isEmpty())
        {
            return QUrl::fromLocalFile(path_);
        }
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        return QUrl::fromLocalFile(QDir(directory).filePath(documentName()));
    }

    QString TextEditorBridge::message() const
    {
        return message_;
    }

    void TextEditorBridge::setMessage(const QString& message)
    {
        message_ = message;
        emit messageChanged();
    }

    void TextEditorBridge::clearMessage()
    {
        setMessage({});
    }

    bool TextEditorBridge::replaceContent(const QString& text)
    {
        if (!active_ || locked() || !text.isValidUtf16())
            return false;
        const auto encoded = encode_text(text.toUtf8().toStdString(), format_);
        if (encoded.error != TextError::None)
            return false;
        const auto normalized = decode_text(encoded.bytes);
        if (normalized.error != TextError::None)
            return false;
        setEditingError({});
        updateText(QString::fromStdString(normalized.text));
        ++revision_;
        emit documentChanged();
        return true;
    }

    bool TextEditorBridge::formatMarkdown(
        int start, int end, const QString& action, const QVariantMap& options)
    {
        if (!active_ || !markdown_ || locked() || start < 0 || end < 0 || start > text_.size() ||
            end > text_.size())
            return false;
        const auto boundary = [this](int position)
        {
            return position == 0 || position == text_.size() || !text_[position - 1].isHighSurrogate() ||
                !text_[position].isLowSurrogate();
        };
        if (!boundary(start) || !boundary(end))
            return false;
        MarkdownOptions settings;
        if (options.contains("expectedText"))
        {
            const auto expected = options.value("expectedText");
            if (expected.metaType().id() != QMetaType::QString || !expected.toString().isValidUtf16())
                return false;
            settings.expected_text_set = true;
            settings.expected_text = expected.toString().toUtf8().toStdString();
        }
        settings.heading_level = options.value("headingLevel", 2).toInt();
        if (action == "heading")
        {
            const auto value = options.value("headingLevel", 2);
            bool valid = false;
            settings.heading_level = value.toInt(&valid);
            if (!valid || value.metaType().id() == QMetaType::Bool ||
                value.metaType().id() == QMetaType::QString || value.toDouble() != settings.heading_level ||
                settings.heading_level < 1 || settings.heading_level > 6)
                return false;
        }
        settings.table_rows = options.value("rows", 3).toInt();
        settings.table_columns = options.value("columns", 3).toInt();
        if (action == "quoteSet")
        {
            const auto value = options.value("quoteLevel");
            bool valid_level = false;
            settings.quote_level = value.toInt(&valid_level);
            if (!valid_level || value.metaType().id() == QMetaType::Bool ||
                value.metaType().id() == QMetaType::QString || value.toDouble() != settings.quote_level)
                return false;
        }
        if (action == "tableAlign")
        {
            bool valid_column = false;
            settings.table_column = options.value("column").toInt(&valid_column);
            settings.table_alignment = options.value("alignment").toString().toStdString();
            const auto value = options.value("column");
            if (!options.contains("column") || !valid_column || value.metaType().id() == QMetaType::Bool ||
                value.metaType().id() == QMetaType::QString || value.toDouble() != settings.table_column)
                return false;
        }
        settings.language = options.value("language").toString().toStdString();
        if (action == "link" || action == "image")
        {
            for (const auto& name : {QStringLiteral("url"), QStringLiteral("title"), QStringLiteral("alt")})
                if (options.contains(name) &&
                    (options.value(name).metaType().id() != QMetaType::QString ||
                        !options.value(name).toString().isValidUtf16()))
                    return false;
            settings.url = options.value("url", "https://example.com").toString().toStdString();
            settings.title = options.value("title").toString().toStdString();
            settings.image_alt_set = options.contains("alt");
            settings.image_alt = options.value("alt").toString().toStdString();
            if (action == "image")
                settings.url = options.value("url").toString().toStdString();
        }
        if (action == "taskSet")
        {
            const auto checked = options.value("checked");
            if (checked.metaType().id() != QMetaType::Bool)
                return false;
            settings.task_checked = checked.toBool();
        }
        const auto source = text_.toUtf8().toStdString();
        const auto change = make_markdown_edit(source, text_.left(start).toUtf8().size(),
            text_.left(end).toUtf8().size(), action.toStdString(), settings);
        if (!change.valid)
            return false;
        const auto updated = source.substr(0, change.start) + change.replacement + source.substr(change.end);
        if (updated == source)
            return true;
        return replaceContent(QString::fromUtf8(updated.data(), static_cast<qsizetype>(updated.size())));
    }

    QVariantMap TextEditorBridge::readContent(const QVariantMap& query) const
    {
        if (query.value("view") == "find")
            return active_ ? read_text_matches(text_, query) : read_error("no_document");
        const DocumentReadQuery read(query);
        if (!read.valid)
            return read_error("invalid_read_query");
        if (!active_)
            return read_error("no_document");
        if (read.view == "overview")
            return {{"ok", true}, {"characters", text_.size()}, {"markdown", markdown_},
                {"views", QStringList{"overview", "text", "find"}}, {"unit", "UTF-16"}};
        if (read.view != "text" && read.view != "content")
            return read_error("unsupported_view");
        return read_text(text_, read.offset, query.contains("limit") ? read.limit : 1600);
    }

    PdfExportSource TextEditorBridge::pdfSource() const
    {
        PdfExportSource result;
        if (!active_ || locked() || !editing_error_.isEmpty())
            result.error = "文本编辑器尚未准备好导出。";
        else
            result.content = PdfTextSource{text_.toUtf8().toStdString(), markdown_};
        result.title = documentName().toStdString();
        result.source_path = path_.toStdString();
        return result;
    }

    void TextEditorBridge::updateText(const QString& text)
    {
        if (!active_ || locked() || text_ == text)
        {
            return;
        }
        text_ = text;
        emit contentChanged();
        const bool modified = !editing_error_.isEmpty() || text_ != saved_text_;
        if (modified_ != modified)
        {
            modified_ = modified;
            emit stateChanged();
        }
    }

    void TextEditorBridge::setEditingError(const QString& error)
    {
        if (!active_ || locked() || editing_error_ == error)
        {
            return;
        }
        editing_error_ = error;
        modified_ = !editing_error_.isEmpty() || text_ != saved_text_;
        setMessage(error);
        emit stateChanged();
    }

}
