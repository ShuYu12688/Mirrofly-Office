#pragma once
#include "document_view.hpp"

#include <mirrorfly/pdf_export.hpp>
#include <mirrorfly/text_storage.hpp>

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace mirrorfly
{
    class TextEditorBridge final : public DocumentView
    {
        Q_OBJECT
        Q_PROPERTY(bool active READ active NOTIFY stateChanged)
        Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
        Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
        Q_PROPERTY(bool markdown READ markdown NOTIFY documentChanged)
        Q_PROPERTY(QString content READ content NOTIFY contentChanged)
        Q_PROPERTY(int revision READ revision NOTIFY documentChanged)
        Q_PROPERTY(QString documentName READ documentName NOTIFY documentChanged)
        Q_PROPERTY(QString documentPath READ documentPath NOTIFY documentChanged)
        Q_PROPERTY(QString formatLabel READ formatLabel NOTIFY documentChanged)
        Q_PROPERTY(QUrl saveUrl READ saveUrl NOTIFY documentChanged)
        Q_PROPERTY(QString message READ message NOTIFY messageChanged)

    public:
        explicit TextEditorBridge(QObject* parent = nullptr);

        bool active() const;
        bool modified() const;
        bool busy() const;
        bool locked() const;
        bool markdown() const;
        QString content() const;
        int revision() const;
        QString documentName() const;
        QString documentPath() const;
        QString formatLabel() const;
        QUrl saveUrl() const;
        QString message() const;

        Q_INVOKABLE void updateText(const QString& text);
        Q_INVOKABLE bool replaceContent(const QString& text);
        Q_INVOKABLE bool formatMarkdown(
            int start, int end, const QString& action, const QVariantMap& options);
        Q_INVOKABLE QVariantMap readContent(const QVariantMap& query) const;
        PdfExportSource pdfSource() const;
        Q_INVOKABLE void setEditingError(const QString& error);
        Q_INVOKABLE void requestOpen(const QUrl& url);
        Q_INVOKABLE void requestNew();
        Q_INVOKABLE void requestNewMarkdown();
        Q_INVOKABLE void requestHome();
        void requestHandoff(const QUrl& destination);
        void finishHandoff(bool accepted);
        Q_INVOKABLE bool requestWindowClose();
        Q_INVOKABLE void save();
        Q_INVOKABLE void saveAs();
        Q_INVOKABLE bool saveTo(const QUrl& destination);
        Q_INVOKABLE void selectSaveFile(const QUrl& url);
        Q_INVOKABLE void cancelSaveDialog();
        Q_INVOKABLE void resolveUnsaved(const QString& decision);
        Q_INVOKABLE void clearMessage();

    signals:
        void stateChanged();
        void documentChanged();
        void contentChanged();
        void messageChanged();
        void confirmUnsavedRequested();
        void saveDialogRequested();
        void windowCloseAllowed();
        void fileRecorded(const QString& path);
        void documentActivated();
        void openCompleted(bool success);
        void handoffRequested(const QUrl& destination);

    private:
        enum class Action
        {
            None,
            Open,
            New,
            NewMarkdown,
            Home,
            Handoff,
            Quit
        };

        enum class Operation
        {
            None,
            Load,
            Save,
            Handoff
        };

        void requestAction(Action action, const QString& path = {});
        void performPendingAction();
        void beginLoad(const QString& path);
        void beginSave(const QString& path, bool new_file = false);
        void completeOperation();
        void setMessage(const QString& message);
        void openSaveDialog();
        bool validateEditingState();

        QFutureWatcher<TextFileResult> worker_;
        QString text_;
        QString saved_text_;
        QString saving_text_;
        QString path_;
        QString pending_path_;
        QString message_;
        QString editing_error_;
        std::string disk_revision_;
        TextFormat format_;
        Action pending_action_ = Action::None;
        Operation operation_ = Operation::None;
        int revision_ = 0;
        bool active_ = false;
        bool markdown_ = false;
        bool modified_ = false;
        bool busy_ = false;
        bool confirmation_open_ = false;
        bool save_dialog_open_ = false;
    };
}
