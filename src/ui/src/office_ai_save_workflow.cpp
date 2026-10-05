#include "document_path.hpp"
#include "office_ai_toolbox.hpp"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
    QJsonObject error(const QString& code, const QString& hint)
    {
        return {{"ok", false}, {"error", code}, {"hint", hint}};
    }

    QJsonObject step(const QString& module, const QString& action, const QJsonArray& args = {})
    {
        return {{"module", module}, {"action", action}, {"args", args}};
    }
}

namespace mirrorfly
{
    QJsonObject OfficeAiToolbox::prepareSave(
        const QString& module, const QJsonObject& document, const QJsonObject& arguments) const
    {
        const QJsonObject extensions{{"text", document.value("markdown").toBool() ? "md" : "txt"},
            {"word", "docx"}, {"sheets", "xlsx"}, {"slides", "pptx"}, {"mindmap", "mfg"}, {"pdf", "pdf"}};
        const QString extension = extensions.value(module).toString();
        if (extension.isEmpty() || !document.value("active").toBool() ||
            document.value("readOnly").toBool() ||
            (module == "slides" && !document.value("editable").toBool()))
            return error("editable_document_required",
                module == "word" || module == "slides"
                    ? "This document is read-only. Use office_editable_copy first; edit the new copy."
                    : "Select an editable document; preserve imported originals.");
        if (arguments.contains("current") && !arguments.value("current").isBool())
            return error("invalid_save_mode", "current must be a boolean.");
        if (arguments.value("current").toBool())
        {
            if (arguments.contains("title") || arguments.contains("destination"))
                return error("invalid_save_mode", "Choose current=true, title or destination, not several.");
            return {{"ok", true}, {"steps", QJsonArray{step(module, "save")}},
                {"receipt", QJsonObject{{"operation", "save_current"}}}};
        }
        QString title = arguments.value("title").toString().trimmed();
        const QUrl title_url(title);
        const bool absolute_title = QFileInfo(title).isAbsolute() || title_url.isLocalFile();
        if (arguments.contains("destination") || absolute_title)
        {
            if (arguments.contains("destination") &&
                (!arguments.value("destination").isString() || arguments.contains("title")))
                return error("invalid_save_mode", "Use a string destination or title, never both.");
            const QString value =
                arguments.contains("destination") ? arguments.value("destination").toString() : title;
            const QUrl url(value);
            QString path = url.isLocalFile() ? url.toLocalFile() : value;
            if (absolute_title && QFileInfo(path).suffix().isEmpty())
                path += '.' + extension;
            const QFileInfo file(path);
            const bool current =
                file.isFile() && same_document_path(document.value("documentPath").toString(), path);
            if (!file.isAbsolute() || file.suffix().compare(extension, Qt::CaseInsensitive) ||
                (!current && !new_document_destination(QUrl::fromLocalFile(path), {extension})))
                return error("invalid_save_destination",
                    "Use a complete absolute path with this document's extension in an existing folder. "
                    "Only the current file may already exist; other existing files are protected.");
            return {{"ok", true},
                {"steps",
                    QJsonArray{
                        step(module, "saveTo", {QUrl::fromLocalFile(file.absoluteFilePath()).toString()})}},
                {"receipt", QJsonObject{{"operation", "save"}, {"path", file.absoluteFilePath()}}}};
        }
        if (title.endsWith('.' + extension, Qt::CaseInsensitive))
            title.chop(extension.size() + 1);
        title.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
        title = title.left(80);
        while (title.endsWith('.') || title.endsWith(' '))
            title.chop(1);
        if (title.isEmpty() || title == "未命名" || title == "Untitled")
            return error(
                "descriptive_title_required", "Choose a concise filename based on the document topic.");
        if (QRegularExpression(
                "^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption)
                .match(title)
                .hasMatch())
            title.prepend('_');
        QString folder = desktop_directory_;
        if (folder.isEmpty())
            folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        const QDir desktop(folder);
        if (!desktop.exists() || desktop.path().isEmpty())
            return error(
                "desktop_unavailable", "Use the current module saveTo with an explicit existing folder.");
        QString destination = desktop.filePath(title + '.' + extension);
        int suffix = 2;
        while (QFileInfo::exists(destination))
            destination = desktop.filePath(title + QStringLiteral(" (%1).").arg(suffix++) + extension);
        return {{"ok", true},
            {"steps", QJsonArray{step(module, "saveTo", {QUrl::fromLocalFile(destination).toString()})}},
            {"receipt", QJsonObject{{"operation", "save"}, {"path", destination}}}};
    }
}
