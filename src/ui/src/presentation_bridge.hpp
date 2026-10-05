#pragma once
#include <mirrorfly/pdf_export.hpp>

#include "presentation_guide_settings.hpp"
#include "presentation_scene.hpp"

#include <mirrorfly/presentation_storage.hpp>

#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>

namespace mirrorfly
{
    struct PreparedPresentationLoad
    {
        PresentationResult result;
        std::shared_ptr<PresentationScene> scene;
        RenderPresentationPtr document;
    };

    class PresentationBridge final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(QString editGeneration READ editGeneration NOTIFY stateChanged)
        Q_PROPERTY(QStringList chineseFontFamilies READ chineseFontFamilies CONSTANT)
        Q_PROPERTY(bool active READ active NOTIFY stateChanged)
        Q_PROPERTY(bool editable READ editable NOTIFY stateChanged)
        Q_PROPERTY(bool themeEditable READ themeEditable NOTIFY stateChanged)
        Q_PROPERTY(QVariantMap themeState READ themeState NOTIFY stateChanged)
        Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
        Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
        Q_PROPERTY(bool syncing READ syncing NOTIFY stateChanged)
        Q_PROPERTY(int pendingEdits READ pendingEdits NOTIFY stateChanged)
        Q_PROPERTY(QString loadingStage READ loadingStage NOTIFY stateChanged)
        Q_PROPERTY(qreal loadingProgress READ loadingProgress NOTIFY stateChanged)
        Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
        Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
        Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
        Q_PROPERTY(QString error READ error NOTIFY errorChanged)
        Q_PROPERTY(QString message READ message NOTIFY messageChanged)
        Q_PROPERTY(QVariant document READ document NOTIFY documentChanged)
        Q_PROPERTY(QString documentName READ documentName NOTIFY documentChanged)
        Q_PROPERTY(QString documentPath READ documentPath NOTIFY documentChanged)
        Q_PROPERTY(int slideCount READ slideCount NOTIFY documentChanged)
        Q_PROPERTY(QVariantList hiddenSlides READ hiddenSlides NOTIFY documentChanged)
        Q_PROPERTY(QVariantList slideSections READ slideSections NOTIFY documentChanged)
        Q_PROPERTY(bool sectionsEditable READ sectionsEditable NOTIFY documentChanged)
        Q_PROPERTY(int currentSlide READ currentSlide NOTIFY stateChanged)
        Q_PROPERTY(QVariantMap slideTransition READ slideTransition NOTIFY stateChanged)
        Q_PROPERTY(int selectedShape READ selectedShape NOTIFY selectionChanged)
        Q_PROPERTY(QVariantMap selection READ selection NOTIFY selectionChanged)
        Q_PROPERTY(qreal slideWidth READ slideWidth NOTIFY documentChanged)
        Q_PROPERTY(qreal slideHeight READ slideHeight NOTIFY documentChanged)
        Q_PROPERTY(qreal zoom READ zoom NOTIFY stateChanged)
        Q_PROPERTY(QVariantMap guideSettings READ guideSettings NOTIFY stateChanged)
        Q_PROPERTY(QString fontSummary READ fontSummary NOTIFY documentChanged)
        Q_PROPERTY(QUrl saveUrl READ saveUrl NOTIFY documentChanged)

    public:
        explicit PresentationBridge(QObject* parent = nullptr);
        ~PresentationBridge() override;

        QStringList chineseFontFamilies() const;
        bool active() const;
        bool editable() const;
        bool themeEditable() const;
        QVariantMap themeState() const;
        bool modified() const;
        bool busy() const;
        bool loading() const;
        bool syncing() const;
        int pendingEdits() const;
        QString loadingStage() const;
        qreal loadingProgress() const;
        bool locked() const;
        bool canUndo() const;
        bool canRedo() const;
        QString error() const;
        QString message() const;
        QVariant document() const;
        PdfExportSource pdfSource() const;
        QString documentName() const;
        QString documentPath() const;
        int slideCount() const;
        QVariantList hiddenSlides() const;
        QVariantList slideSections() const;
        bool sectionsEditable() const;
        int currentSlide() const;
        QVariantMap slideTransition() const;
        int selectedShape() const;
        QVariantMap selection() const;
        qreal slideWidth() const;
        qreal slideHeight() const;
        qreal zoom() const;
        QVariantMap guideSettings() const;
        QString fontSummary() const;
        QUrl saveUrl() const;

        Q_INVOKABLE bool requestOpen(const QUrl& url);
        Q_INVOKABLE void finishLoadingFrame();
        Q_INVOKABLE QVariantMap templatePreviews(const QVariantMap& options) const;
        Q_INVOKABLE QVariantMap templateDescriptions(const QVariantMap& options) const;
        Q_INVOKABLE void requestNew();
        Q_INVOKABLE void createEditableCopy();
        Q_INVOKABLE bool createEditableCopyTo(const QUrl& destination);
        Q_INVOKABLE void showHome();
        Q_INVOKABLE void clear();
        Q_INVOKABLE void setSlide(int index);
        Q_INVOKABLE void nextSlide();
        Q_INVOKABLE void previousSlide();
        Q_INVOKABLE void selectShape(int index);
        Q_INVOKABLE bool applyEdit(const QString& action, const QVariantMap& options = {});
        Q_INVOKABLE QVariantMap snapshot() const;
        Q_INVOKABLE QVariantMap readContent(const QVariantMap& query) const;
        Q_INVOKABLE QVariantMap findText(const QString& query, bool case_sensitive, int offset) const;
        Q_INVOKABLE QVariantMap semanticTree(int page, int offset) const;
        QString editGeneration() const;
        Q_INVOKABLE QVariantMap semanticPage(int page, int offset, int limit) const;
        Q_INVOKABLE QVariantMap paragraphInfo(int index) const;
        Q_INVOKABLE QString speakerNotes(int page) const;
        Q_INVOKABLE QVariantMap editSchema() const;
        Q_INVOKABLE bool selectObject(const QString& id);
        Q_INVOKABLE void addImage(const QUrl& url);
        Q_INVOKABLE void replaceImage(const QUrl& url);
        Q_INVOKABLE void undo();
        Q_INVOKABLE void redo();
        Q_INVOKABLE void setZoom(qreal zoom);
        Q_INVOKABLE bool setGuideSettings(const QVariantMap& patch);
        Q_INVOKABLE void save();
        Q_INVOKABLE void saveAs();
        Q_INVOKABLE bool saveTo(const QUrl& destination);
        Q_INVOKABLE void selectSaveFile(const QUrl& url);
        Q_INVOKABLE void cancelSaveDialog();
        Q_INVOKABLE void resolveUnsaved(const QString& decision);
        Q_INVOKABLE void requestHandoff(const QUrl& destination);
        Q_INVOKABLE void finishHandoff(bool accepted);
        Q_INVOKABLE bool requestWindowClose();
        Q_INVOKABLE void clearError();
        Q_INVOKABLE void clearMessage();

    signals:
        void stateChanged();
        void documentChanged();
        void selectionChanged();
        void errorChanged();
        void messageChanged();
        void openCompleted(bool success);
        void fileRecorded(const QString& path);
        void confirmUnsavedRequested();
        void saveDialogRequested();
        void windowCloseAllowed();
        void handoffRequested(const QUrl& destination);
        void documentActivated();
        void loadStarted();
        void copyCompleted(bool success);

    private:
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
            Handoff,
            ImageLoad
        };

        struct HistoryEntry
        {
            std::shared_ptr<PresentationScene> scene;
            std::uint64_t generation = 0;
            std::size_t bytes = 0;
            std::vector<std::uint64_t> thumbnail_revisions;
        };

        struct TemplatePreview
        {
            std::array<std::string, 6> palette;
            RenderPresentationPtr document;
        };

        struct PendingEdit
        {
            PresentationEditCommand command;
            std::uint64_t generation = 0;
            std::deque<HistoryEntry> undo_before;
            std::size_t undo_bytes_before = 0;
            std::deque<HistoryEntry> redo_before;
            std::size_t redo_bytes_before = 0;
            int slide_before = 0;
            std::uint64_t selection_before = 0;
            std::string selection_source_part;
            std::string selection_source_id;
        };

        struct EditCommitResult
        {
            PresentationEditResult result;
            std::shared_ptr<PresentationScene> scene;
            std::uint64_t generation = 0;
        };

        void requestAction(Action action, const QUrl& destination = {});
        void performPendingAction();
        void beginLoad(const QString& path);
        void beginSave(const QString& path, bool conversion, bool new_file = false);
        void completeOperation();
        void completeLoad();
        void completeImageLoad();
        void loadImage(const QUrl& url, bool replacement);
        void failOpen(const QString& message);
        void setMessage(const QString& message);
        void setError(const QString& error);
        void openSaveDialog(bool conversion);
        void activateScene(std::shared_ptr<PresentationScene> scene, RenderPresentationPtr document);
        void refreshDocument(bool reuse_analysis = false, bool reuse_thumbnails = false);
        void updateLoadingProgress(const PresentationLoadProgress& progress);
        void setLoadingProgress(const QString& stage, qreal progress);
        void resetDocument();
        void pushUndo(const std::shared_ptr<PresentationScene>& scene, std::uint64_t generation,
            const PresentationScene* retained = nullptr);
        void pushRedo(const std::shared_ptr<PresentationScene>& scene, std::uint64_t generation,
            const PresentationScene* retained = nullptr);
        void trimHistory(std::deque<HistoryEntry>& history, std::size_t& bytes);
        bool commitEdit(const PresentationEditCommand& command);
        void startNextEditCommit();
        void completeEditCommit();
        void updateCommittedHistory(
            std::uint64_t generation, const std::shared_ptr<PresentationScene>& scene);
        void rollbackPendingEdits(const PendingEdit& failed, const PresentationEditResult& result);

        struct SaveResult
        {
            PresentationResult file;
            std::shared_ptr<PresentationScene> scene;
        };
        QFutureWatcher<SaveResult> worker_;
        QFutureWatcher<PreparedPresentationLoad> load_worker_;
        QFutureWatcher<PresentationImageFileResult> image_worker_;
        QThreadPool edit_pool_;
        QFutureWatcher<EditCommitResult> edit_worker_;
        std::deque<PendingEdit> pending_edits_;
        std::shared_ptr<PresentationScene> scene_;
        std::shared_ptr<PresentationScene> committed_scene_;
        std::shared_ptr<PresentationScene> saving_scene_;
        RenderPresentationPtr document_;
        PresentationRenderEnvironmentPtr render_environment_;
        mutable std::map<QString, TemplatePreview> template_previews_;
        std::deque<HistoryEntry> undo_;
        std::deque<HistoryEntry> redo_;
        QString path_;
        QString pending_path_;
        QUrl pending_url_;
        QString message_;
        QString error_;
        QString loading_stage_;
        QString imported_source_path_;
        std::string disk_revision_;
        Action pending_action_ = Action::None;
        Operation operation_ = Operation::None;
        std::uint64_t generation_ = 0;
        std::uint64_t next_generation_ = 1;
        std::uint64_t saved_generation_ = 0;
        std::uint64_t committed_generation_ = 0;
        std::size_t undo_bytes_ = 0;
        std::size_t redo_bytes_ = 0;
        int current_slide_ = 0;
        int selected_shape_ = -1;
        qreal zoom_ = 0;
        PresentationGuideSettings guide_settings_;
        qreal loading_progress_ = 0;
        bool active_ = false;
        bool busy_ = false;
        bool confirmation_open_ = false;
        bool save_dialog_open_ = false;
        bool save_dialog_conversion_ = false;
        bool saving_conversion_ = false;
        bool replacing_image_ = false;
    };
}
