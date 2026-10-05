#pragma once
#include "document_view.hpp"
#include <mirrorfly/pdf_export.hpp>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <deque>
#include <mirrorfly/mindmap_storage.hpp>
#include <mirrorfly/pdf_storage.hpp>
#include <variant>

namespace mirrorfly
{
    // Shared session lifecycle; domain commands stay in the independent core modules.
    class CanvasBridge final : public DocumentView
    {
        Q_OBJECT
        Q_PROPERTY(bool active READ active NOTIFY stateChanged)
        Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
        Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
        Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
        Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
        Q_PROPERTY(QString kind READ kind CONSTANT)
        Q_PROPERTY(QString documentName READ documentName NOTIFY stateChanged)
        Q_PROPERTY(QString message READ message NOTIFY stateChanged)
        Q_PROPERTY(QUrl saveUrl READ saveUrl NOTIFY stateChanged)
        Q_PROPERTY(QString documentPath READ documentPath NOTIFY stateChanged)
        Q_PROPERTY(QVariantMap viewData READ viewData NOTIFY viewChanged)
        Q_PROPERTY(QString editGeneration READ editGeneration NOTIFY stateChanged)
        Q_PROPERTY(QString selectedId READ selectedId NOTIFY viewChanged)
        Q_PROPERTY(int currentPage READ currentPage NOTIFY viewChanged)
        Q_PROPERTY(int pageCount READ pageCount NOTIFY viewChanged)
        Q_PROPERTY(QImage image READ image NOTIFY imageChanged)
        Q_PROPERTY(bool previewReady READ previewReady NOTIFY imageChanged)
        Q_PROPERTY(QString connectionMode READ connectionMode NOTIFY connectionChanged)
        Q_PROPERTY(QString connectionFrom READ connectionFrom NOTIFY connectionChanged)
    public:
        explicit CanvasBridge(bool pdf, QObject* parent = nullptr);
        ~CanvasBridge() override;
        bool active() const;
        bool locked() const;
        bool modified() const;
        bool canUndo() const;
        bool canRedo() const;
        QString kind() const;
        QString documentName() const;
        QString message() const;
        QUrl saveUrl() const;
        QString documentPath() const;
        QVariantMap viewData() const;
        QString editGeneration() const;
        QString selectedId() const;
        int currentPage() const;
        int pageCount() const;
        QImage image() const;
        bool previewReady() const;
        QString connectionMode() const;
        QString connectionFrom() const;
        Q_INVOKABLE bool beginConnection(const QString& mode, const QString& from);
        Q_INVOKABLE bool connectNode(const QString& target);
        Q_INVOKABLE bool requestOpen(const QUrl& url);
        Q_INVOKABLE void requestNew();
        Q_INVOKABLE void requestHome();
        void requestHandoff(const QUrl& destination);
        void finishHandoff(bool success);
        Q_INVOKABLE bool requestWindowClose();
        Q_INVOKABLE void resolveUnsaved(const QString& decision);
        Q_INVOKABLE void save();
        Q_INVOKABLE void saveAs();
        Q_INVOKABLE bool saveTo(const QUrl& destination);
        Q_INVOKABLE void selectSaveFile(const QUrl& url);
        Q_INVOKABLE void cancelSaveDialog();
        Q_INVOKABLE void clearMessage();
        Q_INVOKABLE void undo();
        Q_INVOKABLE void redo();
        Q_INVOKABLE bool execute(const QString& action, const QVariantMap& arguments);
        Q_INVOKABLE QVariantMap snapshot() const;
        Q_INVOKABLE QVariantMap readContent(const QVariantMap& query) const;
        Q_INVOKABLE QVariantMap editSchema() const;
        PdfExportSource pdfSource() const;
        Q_INVOKABLE void selectPage(int index);
        Q_INVOKABLE void selectNode(const QString& id);
        Q_INVOKABLE void setRenderSize(int width, int height);
        Q_INVOKABLE QString outline() const;
        Q_INVOKABLE void copyOutline();
    signals:
        void stateChanged();
        void connectionChanged();
        void viewChanged();
        void imageChanged();
        void documentActivated();
        void openCompleted(bool success);
        void fileRecorded(const QString& path);
        void confirmUnsavedRequested();
        void saveDialogRequested();
        void openDialogRequested();
        void windowCloseAllowed();
        void handoffRequested(const QUrl& destination);

    private:
        using Document = std::variant<PdfDocument, MindMapDocument>;
        struct Result
        {
            bool success = false;
            QString message;
            QString path;
            std::string revision;
            Document document;
        };
        struct History
        {
            Document document;
            std::uint64_t generation = 0;
            std::size_t bytes = 0;
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
        void requestAction(Action action, const QUrl& url = {});
        void performPending();
        void completeOperation();
        void beginSave(const QString& path, bool new_file = false);
        void refreshView();
        void reset();
        void error(const QString& message);
        void render();
        void restoreHistory(bool redo);
        static std::size_t documentBytes(const Document& document);
        static void trim(std::deque<History>& history);
        bool pdf_;
        bool active_ = false;
        bool confirming_ = false;
        bool save_dialog_ = false;
        Operation operation_ = Operation::None;
        Action pending_ = Action::None;
        QUrl pending_url_;
        QString path_;
        QString source_path_;
        QString message_;
        std::string disk_revision_;
        Document document_;
        std::deque<History> undo_, redo_;
        std::uint64_t generation_ = 0, next_generation_ = 1, saved_generation_ = 0, revision_ = 1;
        int page_ = 0;
        QString node_;
        QString connection_mode_ = "off";
        QString connection_from_;
        QVariantMap view_;
        QImage image_;
        QFutureWatcher<Result> worker_;
        QFutureWatcher<PdfRenderResult> renderer_;
        QTimer render_timer_;
        std::uint64_t render_token_ = 0, rendering_token_ = 0;
        int render_width_ = 1000, render_height_ = 1200;
    };
}
