#pragma once
#include "document_view.hpp"
#include <mirrorfly/pdf_export.hpp>

#include <mirrorfly/word_storage.hpp>

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QUrl>
#include <QVariantMap>
#include <optional>

namespace mirrorfly
{
    class WordBridge final : public DocumentView
    {
        Q_OBJECT
        Q_PROPERTY(bool active READ active NOTIFY stateChanged)
        Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
        Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
        Q_PROPERTY(bool readOnly READ readOnly NOTIFY stateChanged)
        Q_PROPERTY(qreal loadingProgress READ loadingProgress NOTIFY loadingChanged)
        Q_PROPERTY(QString loadingStage READ loadingStage NOTIFY loadingChanged)
        Q_PROPERTY(int revision READ revision NOTIFY documentChanged)
        Q_PROPERTY(QString documentName READ documentName NOTIFY documentChanged)
        Q_PROPERTY(QString documentPath READ documentPath NOTIFY documentChanged)
        Q_PROPERTY(QString message READ message NOTIFY messageChanged)
        Q_PROPERTY(QUrl saveUrl READ saveUrl NOTIFY documentChanged)
        Q_PROPERTY(QVariantMap statistics READ statistics NOTIFY editorChanged)
        Q_PROPERTY(QStringList chineseFonts READ chineseFonts CONSTANT)
        Q_PROPERTY(bool formatReady READ formatReady NOTIFY editorChanged)
        Q_PROPERTY(QVariantList paragraphDecorations READ paragraphDecorations NOTIFY editorChanged)

    public:
        explicit WordBridge(QObject* parent = nullptr);
        ~WordBridge() override;
        bool active() const;
        bool modified() const;
        bool locked() const;
        bool readOnly() const;
        qreal loadingProgress() const;
        QString loadingStage() const;
        int revision() const;
        QString documentName() const;
        QString documentPath() const;
        QString message() const;
        QUrl saveUrl() const;
        QVariantMap statistics() const;
        QStringList chineseFonts() const;
        bool formatReady() const;
        QVariantList paragraphDecorations() const;

        Q_INVOKABLE bool requestOpen(const QUrl& url);
        Q_INVOKABLE void requestNew();
        Q_INVOKABLE void requestHome();
        void requestHandoff(const QUrl& destination);
        void finishHandoff(bool success);
        Q_INVOKABLE bool requestWindowClose();
        Q_INVOKABLE void resolveUnsaved(const QString& decision);
        Q_INVOKABLE void requestEditableCopy();
        Q_INVOKABLE bool createEditableCopyTo(const QUrl& destination);
        Q_INVOKABLE void save();
        Q_INVOKABLE void saveAs();
        Q_INVOKABLE bool saveTo(const QUrl& destination);
        Q_INVOKABLE void selectSaveFile(const QUrl& url);
        Q_INVOKABLE void cancelSaveDialog();
        Q_INVOKABLE void clearMessage();
        Q_INVOKABLE void loadEditor(QQuickTextDocument* wrapper);
        Q_INVOKABLE QVariantMap snapshot() const;
        Q_INVOKABLE QVariantMap readContent(const QVariantMap& query) const;
        Q_INVOKABLE QVariantMap editSchema() const;
        PdfExportSource pdfSource() const;
        Q_INVOKABLE QVariantMap inspect(int position) const;
        Q_INVOKABLE bool format(int start, int end, const QString& action, const QVariant& value);
        Q_INVOKABLE void undo();
        Q_INVOKABLE void redo();
        Q_INVOKABLE QVariantMap find(const QString& query, int from, bool backward);
        Q_INVOKABLE bool insertText(int start, int end, const QString& text);
        Q_INVOKABLE bool replace(int start, int end, const QString& expected, const QString& replacement);
        Q_INVOKABLE bool paste(int start, int end);
        Q_INVOKABLE bool pastePlain(int start, int end);
        Q_INVOKABLE int insertParagraph(int start, int end);
        Q_INVOKABLE bool insertTemplate(int position, const QString& kind);
        Q_INVOKABLE bool copyFormat(int position);
        Q_INVOKABLE bool pasteFormat(int start, int end);
        Q_INVOKABLE bool copySelection(int start, int end, bool cut);
        Q_INVOKABLE bool replaceAll(const QString& query, const QString& replacement);

    signals:
        void stateChanged();
        void documentChanged();
        void messageChanged();
        void editorChanged();
        void confirmUnsavedRequested();
        void saveDialogRequested();
        void windowCloseAllowed();
        void fileRecorded(const QString& path);
        void documentActivated();
        void openCompleted(bool success);
        void loadStarted();
        void loadReady();
        void copyCompleted(bool success);
        void loadingChanged();
        void handoffRequested(const QUrl& destination);

    private:
        struct PreparedEditor
        {
            std::unique_ptr<QTextDocument> document;
            ~PreparedEditor();
        };

        struct OperationResult
        {
            WordResult file;
            std::shared_ptr<PreparedEditor> editor;
        };

        enum class Action
        {
            None,
            Open,
            New,
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
        void requestAction(Action action, const QString& destination = {});
        void performPendingAction();
        void beginLoad(const QString& path);
        void beginSave(const QString& path, bool new_file = false);
        void completeOperation();
        void setMessage(const QString& message);
        void openSaveDialog();
        void cancelPending();
        void guardEditorBudget();
        void setLoadingProgress(const QString& stage, qreal progress);

        QFutureWatcher<OperationResult> worker_;
        std::shared_ptr<PreparedEditor> prepared_editor_;
        QPointer<QTextDocument> editor_;
        WordDocument document_;
        QString path_;
        QString source_path_;
        QString pending_path_;
        QString message_;
        std::string disk_revision_;
        Action pending_action_ = Action::None;
        Operation operation_ = Operation::None;
        int revision_ = 0;
        int load_generation_ = 0;
        qreal loading_progress_ = 0;
        QString loading_stage_;
        bool awaiting_editor_ = false;
        bool active_ = false;
        bool read_only_ = false;
        bool modified_ = false;
        bool confirmation_open_ = false;
        bool save_dialog_open_ = false;
        bool copy_pending_ = false;
        bool guarding_editor_ = false;
        int last_change_position_ = 0;
        int last_change_removed_ = 0;
        int last_change_added_ = 0;
        int accepted_block_count_ = 0;
        std::optional<WordParagraph> format_sample_;
    };
}
