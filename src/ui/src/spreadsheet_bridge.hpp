#pragma once
#include "document_view.hpp"

#include "spreadsheet_model.hpp"

#include <mirrorfly/pdf_export.hpp>
#include <mirrorfly/spreadsheet_storage.hpp>

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <deque>

namespace mirrorfly
{
    class SpreadsheetBridge final : public DocumentView
    {
        Q_OBJECT
        Q_PROPERTY(bool active READ active NOTIFY stateChanged)
        Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
        Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
        Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
        Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
        Q_PROPERTY(QString error READ error NOTIFY stateChanged)
        Q_PROPERTY(QString documentName READ documentName NOTIFY documentChanged)
        Q_PROPERTY(QString documentPath READ documentPath NOTIFY documentChanged)
        Q_PROPERTY(QUrl saveUrl READ saveUrl NOTIFY documentChanged)
        Q_PROPERTY(QStringList sheetNames READ sheetNames NOTIFY documentChanged)
        Q_PROPERTY(int currentSheet READ currentSheet NOTIFY documentChanged)
        Q_PROPERTY(QAbstractItemModel* model READ model CONSTANT)
        Q_PROPERTY(QVariantMap rangeInfo READ rangeInfo NOTIFY cellChanged)
        Q_PROPERTY(QVariantMap cellInfo READ cellInfo NOTIFY cellChanged)
        Q_PROPERTY(QString compatibilitySummary READ compatibilitySummary NOTIFY documentChanged)
        Q_PROPERTY(QVariantMap formatInfo READ formatInfo NOTIFY cellChanged)
        Q_PROPERTY(qulonglong revision READ revision NOTIFY stateChanged)
        Q_PROPERTY(QVariantMap layoutInfo READ layoutInfo NOTIFY documentChanged)
        Q_PROPERTY(int layoutRevision READ layoutRevision NOTIFY documentChanged)

    public:
        explicit SpreadsheetBridge(QObject* parent = nullptr);
        bool active() const;
        bool modified() const;
        bool busy() const;
        bool locked() const;
        bool canUndo() const;
        bool canRedo() const;
        QString error() const;
        QString documentName() const;
        QString documentPath() const;
        QUrl saveUrl() const;
        QStringList sheetNames() const;
        int currentSheet() const;
        QAbstractItemModel* model();
        QVariantMap cellInfo() const;
        QVariantMap rangeInfo() const;
        QString compatibilitySummary() const;
        QVariantMap formatInfo() const;
        qulonglong revision() const;
        int layoutRevision() const;
        QVariantMap layoutInfo() const;
        Q_INVOKABLE bool startTool(const QString& action, const QVariantMap& args = {});

        Q_INVOKABLE bool requestOpen(const QUrl& url);
        Q_INVOKABLE void requestNew();
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
        Q_INVOKABLE void clearError();
        Q_INVOKABLE void selectSheet(int index);
        Q_INVOKABLE bool addSheet(const QString& name = {});
        Q_INVOKABLE bool renameSheet(const QString& name);
        Q_INVOKABLE void selectBand(int first, int last, bool rows);
        Q_INVOKABLE void selectCell(int row, int column, bool extend = false);
        Q_INVOKABLE bool selectAddress(const QString& address);
        Q_INVOKABLE bool findCell(const QString& query, bool backwards = false);
        Q_INVOKABLE void copyCell();
        Q_INVOKABLE bool pasteCell();
        Q_INVOKABLE QString selectionText();
        Q_INVOKABLE bool pasteText(const QString& text);
        Q_INVOKABLE bool clearSelection();
        Q_INVOKABLE bool setCellValue(int row, int column, const QString& text, const QString& kind);
        Q_INVOKABLE void commitTextInput();
        Q_INVOKABLE void undo();
        Q_INVOKABLE void redo();
        Q_INVOKABLE bool formatSelection(const QVariantMap& patch);
        Q_INVOKABLE bool styleSelection(const QString& preset, const QVariantMap& palette);
        Q_INVOKABLE bool resizeSelection(bool columns, double size);
        Q_INVOKABLE double columnWidth(int column) const;
        Q_INVOKABLE double rowHeight(int row) const;
        Q_INVOKABLE bool sortSelection(bool descending, bool header = true);
        Q_INVOKABLE bool insertTemplate(const QString& name);
        Q_INVOKABLE QVariantMap editSchema() const;
        Q_INVOKABLE QVariantMap snapshot() const;
        Q_INVOKABLE QVariantMap readContent(const QVariantMap& query) const;
        PdfExportSource pdfSource() const;

    signals:
        void stateChanged();
        void documentChanged();
        void cellChanged();
        void documentActivated();
        void openCompleted(bool success);
        void fileRecorded(const QString& path);
        void confirmUnsavedRequested();
        void saveDialogRequested();
        void windowCloseAllowed();
        void handoffRequested(const QUrl& destination);

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
            Handoff
        };
        struct CellChange
        {
            SpreadsheetAddress address;
            SpreadsheetValue before;
            SpreadsheetValue after;
        };
        struct PendingCut
        {
            QByteArray bytes;
            std::size_t sheet = 0;
            SpreadsheetRange range;
            std::uint64_t generation = 0;
        };
        struct HistoryEntry
        {
            std::size_t sheet = 0;
            SpreadsheetAddress address;
            SpreadsheetAddress last;
            std::vector<CellChange> changes;
            std::vector<SpreadsheetFormatCommand> formats_before;
            std::vector<SpreadsheetFormatCommand> formats_after;
            std::vector<SpreadsheetDimensionCommand> dimensions_before;
            std::vector<SpreadsheetDimensionCommand> dimensions_after;
            std::optional<SpreadsheetFeatures> features_before;
            std::optional<SpreadsheetFeatures> features_after;
            std::optional<SpreadsheetSheetCommand> structure_before;
            std::optional<SpreadsheetSheetCommand> structure_after;
            std::shared_ptr<const SpreadsheetDocument> document_before;
            std::shared_ptr<const SpreadsheetDocument> document_after;
            std::size_t document_bytes = 0;
            std::size_t previous_sheet = 0;
            std::uint64_t before_generation = 0;
            std::uint64_t after_generation = 0;
        };

        void requestAction(Action action, const QUrl& destination = {});
        void performPendingAction();
        void beginSave(const QString& path, bool new_file = false);
        void completeOperation();
        void resetDocument();
        void setError(const QString& error);
        bool applyTableTool(const QString& action, const QVariantMap& args);
        bool applyAxisTool(const QString& action, const QVariantMap& args);
        bool applySheetTool(const QString& action, const QVariantMap& args);
        bool commitDocument(SpreadsheetDocument proposed, std::size_t sheet);
        bool commitFeatures(const SpreadsheetFeatures& features);
        bool copyCells(bool cut);
        bool pasteCells(const QString& mode);
        bool commitCells(const std::vector<SpreadsheetEditCommand>& commands);
        bool commitStructure(const SpreadsheetSheetCommand& command, const SpreadsheetSheetCommand& inverse);
        bool commitChanges(const std::vector<SpreadsheetEditCommand>& cells,
            const std::vector<SpreadsheetFormatCommand>& formats,
            const std::vector<SpreadsheetDimensionCommand>& dimensions);
        static std::size_t historyBytes(const HistoryEntry& entry);
        SpreadsheetRange selectedRange() const;
        void trimHistory();
        void restoreHistory(bool redo);

        SpreadsheetFormat format_sample_;
        std::optional<PendingCut> pending_cut_;
        SpreadsheetModel model_;
        SpreadsheetDocument document_;
        QFutureWatcher<SpreadsheetResult> worker_;
        std::deque<HistoryEntry> undo_;
        std::deque<HistoryEntry> redo_;
        QString path_;
        QString error_;
        std::string disk_revision_;
        QUrl pending_url_;
        Action pending_action_ = Action::None;
        Operation operation_ = Operation::None;
        std::uint64_t generation_ = 0;
        std::uint64_t next_generation_ = 1;
        std::uint64_t saved_generation_ = 0;
        int sheet_ = 0;
        SpreadsheetAddress cell_;
        SpreadsheetAddress anchor_;
        bool active_ = false;
        bool confirmation_open_ = false;
        bool save_dialog_open_ = false;
        int layout_revision_ = 0;
        std::uint64_t revision_ = 1;
    };
}
