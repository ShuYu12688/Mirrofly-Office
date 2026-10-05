#include "text_bridge.hpp"
#include "document_path.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtConcurrentRun>

namespace
{
    QString error_message(mirrorfly::TextFileError error)
    {
        using mirrorfly::TextFileError;
        switch (error)
        {
        case TextFileError::UnsupportedType:
            return QStringLiteral("当前可以编辑 .txt、.text、.md 和 .markdown 文件。");
        case TextFileError::TooLarge:
            return QStringLiteral("本版支持最多 2 MiB 的纯文本，编码转换后的内容也需在此范围内。");
        case TextFileError::InvalidEncoding:
            return QStringLiteral("文件编码无法识别。请使用 UTF-8，或带 BOM 的 UTF-16 文件。");
        case TextFileError::BinaryContent:
            return QStringLiteral("文件包含二进制内容，不能作为纯文本编辑。");
        case TextFileError::ChangedOnDisk:
            return QStringLiteral("文件已被其他程序修改或移走。请另存为，保留当前编辑内容。");
        case TextFileError::WriteFailed:
            return QStringLiteral("未能保存文件，原文件未被本次保存替换。请检查权限或另存到其他位置。");
        case TextFileError::ReadFailed:
            return QStringLiteral("文件无法读取，请确认文件存在、权限正常且不是符号链接。");
        case TextFileError::None:
            return {};
        }
        return QStringLiteral("文件操作未完成。");
    }

}

namespace mirrorfly
{
    TextEditorBridge::TextEditorBridge(QObject* parent) : DocumentView(parent)
    {
        connect(
            &worker_, &QFutureWatcher<TextFileResult>::finished, this, &TextEditorBridge::completeOperation);
    }

    bool TextEditorBridge::active() const
    {
        return active_;
    }

    bool TextEditorBridge::modified() const
    {
        return modified_;
    }

    bool TextEditorBridge::busy() const
    {
        return busy_;
    }

    bool TextEditorBridge::locked() const
    {
        return busy_ || confirmation_open_ || save_dialog_open_;
    }

    bool TextEditorBridge::markdown() const
    {
        return markdown_;
    }

    void TextEditorBridge::requestOpen(const QUrl& url)
    {
        if (url.isLocalFile())
        {
            requestAction(Action::Open, url.toLocalFile());
        }
    }

    void TextEditorBridge::requestNew()
    {
        requestAction(Action::New);
    }

    void TextEditorBridge::requestNewMarkdown()
    {
        requestAction(Action::NewMarkdown);
    }

    void TextEditorBridge::requestHome()
    {
        requestAction(Action::Home);
    }

    void TextEditorBridge::requestHandoff(const QUrl& destination)
    {
        if (destination.isValid() && !destination.isEmpty())
        {
            requestAction(Action::Handoff, destination.toString(QUrl::FullyEncoded));
        }
    }

    void TextEditorBridge::finishHandoff(bool accepted)
    {
        if (operation_ != Operation::Handoff)
        {
            return;
        }
        operation_ = Operation::None;
        busy_ = false;
        if (accepted)
        {
            pending_action_ = Action::Home;
            performPendingAction();
        }
        else
        {
            emit stateChanged();
        }
    }

    bool TextEditorBridge::requestWindowClose()
    {
        if (locked())
        {
            return false;
        }
        if (!modified_)
        {
            return true;
        }
        requestAction(Action::Quit);
        return false;
    }

    void TextEditorBridge::requestAction(Action action, const QString& path)
    {
        if (locked())
        {
            return;
        }
        pending_action_ = action;
        pending_path_ = path;
        if (modified_)
        {
            confirmation_open_ = true;
            emit stateChanged();
            emit confirmUnsavedRequested();
            return;
        }
        performPendingAction();
    }

    void TextEditorBridge::performPendingAction()
    {
        const auto action = pending_action_;
        const auto path = pending_path_;
        pending_action_ = Action::None;
        pending_path_.clear();
        switch (action)
        {
        case Action::Open:
            beginLoad(path);
            return;
        case Action::New:
        case Action::NewMarkdown:
            active_ = true;
            markdown_ = action == Action::NewMarkdown;
            modified_ = false;
            editing_error_.clear();
            text_.clear();
            saved_text_.clear();
            path_.clear();
            disk_revision_.clear();
            format_ = TextFormat{};
            ++revision_;
            clearMessage();
            emit contentChanged();
            emit documentChanged();
            emit stateChanged();
            emit documentActivated();
            return;
        case Action::Home:
            active_ = false;
            markdown_ = false;
            modified_ = false;
            editing_error_.clear();
            text_.clear();
            saved_text_.clear();
            path_.clear();
            disk_revision_.clear();
            ++revision_;
            clearMessage();
            emit contentChanged();
            emit documentChanged();
            emit stateChanged();
            return;
        case Action::Handoff:
            busy_ = true;
            operation_ = Operation::Handoff;
            emit stateChanged();
            emit handoffRequested(QUrl(path));
            return;
        case Action::Quit:
            emit windowCloseAllowed();
            return;
        case Action::None:
            return;
        }
    }

    void TextEditorBridge::resolveUnsaved(const QString& decision)
    {
        if (!confirmation_open_)
        {
            return;
        }
        confirmation_open_ = false;
        emit stateChanged();
        if (decision == QStringLiteral("save"))
        {
            save();
        }
        else if (decision == QStringLiteral("discard"))
        {
            performPendingAction();
        }
        else
        {
            pending_action_ = Action::None;
            pending_path_.clear();
        }
    }

    void TextEditorBridge::beginLoad(const QString& path)
    {
        busy_ = true;
        operation_ = Operation::Load;
        clearMessage();
        emit stateChanged();
        const auto utf8_path = path.toUtf8().toStdString();
        worker_.setFuture(QtConcurrent::run([utf8_path]()
        {
            return load_text_file(utf8_path);
        }));
    }

    void TextEditorBridge::save()
    {
        if (!active_ || locked() || !validateEditingState())
        {
            return;
        }
        if (path_.isEmpty())
        {
            openSaveDialog();
        }
        else
        {
            beginSave(path_);
        }
    }

    bool TextEditorBridge::saveTo(const QUrl& destination)
    {
        if (!active_ || locked() || !validateEditingState() ||
            !new_document_destination(
                destination, markdown_ ? QStringList{"md", "markdown"} : QStringList{"txt", "text"}))
        {
            setMessage(QStringLiteral("请等待编辑完成，并选择已有目录下尚不存在的目标文件；不能覆盖原件。"));
            return false;
        }
        beginSave(QFileInfo(destination.toLocalFile()).absoluteFilePath(), true);
        return busy_;
    }

    void TextEditorBridge::saveAs()
    {
        if (active_ && !locked() && validateEditingState())
        {
            openSaveDialog();
        }
    }

    void TextEditorBridge::openSaveDialog()
    {
        save_dialog_open_ = true;
        emit stateChanged();
        emit saveDialogRequested();
    }

    void TextEditorBridge::selectSaveFile(const QUrl& url)
    {
        if (!save_dialog_open_)
        {
            return;
        }
        save_dialog_open_ = false;
        emit stateChanged();
        if (!url.isLocalFile() || url.toLocalFile().isEmpty())
        {
            cancelSaveDialog();
            return;
        }
        beginSave(url.toLocalFile());
    }

    void TextEditorBridge::cancelSaveDialog()
    {
        save_dialog_open_ = false;
        pending_action_ = Action::None;
        pending_path_.clear();
        emit stateChanged();
    }

    bool TextEditorBridge::validateEditingState()
    {
        if (!editing_error_.isEmpty())
        {
            pending_action_ = Action::None;
            pending_path_.clear();
            setMessage(editing_error_);
            return false;
        }
        return true;
    }

    void TextEditorBridge::beginSave(const QString& path, bool new_file)
    {
        if (!validateEditingState())
        {
            return;
        }
        if (!text_.isValidUtf16())
        {
            pending_action_ = Action::None;
            pending_path_.clear();
            setMessage(error_message(TextFileError::InvalidEncoding));
            return;
        }
        busy_ = true;
        operation_ = Operation::Save;
        saving_text_ = text_;
        clearMessage();
        emit stateChanged();
        const auto utf8_path = path.toUtf8().toStdString();
        const auto text = saving_text_.toUtf8().toStdString();
        const auto format = format_;
        auto expected_revision = same_document_path(path_, path) ? disk_revision_ : std::string{};
        if (new_file)
            expected_revision = "missing";
        worker_.setFuture(QtConcurrent::run([utf8_path, text, format, expected_revision]()
        {
            return save_text_file(utf8_path, text, format, expected_revision);
        }));
    }

    void TextEditorBridge::completeOperation()
    {
        TextFileResult result;
        const auto operation = operation_;
        try
        {
            result = worker_.result();
        }
        catch (...)
        {
            result.error =
                operation == Operation::Save ? TextFileError::WriteFailed : TextFileError::ReadFailed;
        }
        busy_ = false;
        operation_ = Operation::None;
        if (result.error != TextFileError::None)
        {
            pending_action_ = Action::None;
            pending_path_.clear();
            saving_text_.clear();
            setMessage(error_message(result.error));
            emit stateChanged();
            if (operation == Operation::Load)
            {
                emit openCompleted(false);
            }
            return;
        }
        path_ = QString::fromUtf8(result.path.data(), static_cast<qsizetype>(result.path.size()));
        markdown_ = is_markdown_path(result.path);
        disk_revision_ = result.revision;
        format_ = result.format;
        if (operation == Operation::Load)
        {
            editing_error_.clear();
            text_ = QString::fromUtf8(result.text.data(), static_cast<qsizetype>(result.text.size()));
            saved_text_ = text_;
            active_ = true;
            ++revision_;
            emit contentChanged();
        }
        else
        {
            saved_text_ = saving_text_;
            saving_text_.clear();
            format_.mixed_line_endings = false;
            setMessage(QStringLiteral("已保存。"));
        }
        modified_ = text_ != saved_text_;
        emit documentChanged();
        emit stateChanged();
        emit fileRecorded(path_);
        if (operation == Operation::Load)
        {
            emit documentActivated();
            emit openCompleted(true);
        }
        if (operation == Operation::Save)
        {
            performPendingAction();
        }
    }
}
