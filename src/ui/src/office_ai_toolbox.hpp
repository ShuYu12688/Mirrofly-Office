#pragma once

#include "office_ai_file_policy.hpp"
#include "office_ai_image_library.hpp"
#include "office_ai_sequence.hpp"

#include <QJsonObject>
#include <QObject>
#include <QSet>

#include <cstdint>
#include <memory>

namespace mirrorfly
{
    // Document tool session. Uses only the public Office automation contract; knows no model protocol.
    class OfficeAiToolbox final : public QObject
    {
        Q_OBJECT

    public:
        explicit OfficeAiToolbox(const QString& desktop_directory = {}, QObject* parent = nullptr);
        void reset();
        void updateTaskRequest(const QString& request);
        void focusWorkspace();
        void setRequestedSlidePages(int pages);
        void cancel();
        void beginResponse(const QString& revision, int number);
        void requireObservation();
        bool checkWorkspace() const;
        bool stagnantEdits() const;
        QString documentKey() const;
        const QSet<QString>& groups() const;
        QJsonObject query(const QString& name, const QJsonObject& arguments);
        void execute(
            const QString& name, const QJsonObject& arguments, OfficeAiSequence::Completion completion);

    signals:
        void notice(const QString& text);
        void diagnostic(const QString& event, const QJsonObject& fields);
        void actionFinished(const QString& module, const QString& action, const QJsonArray& arguments,
            const QJsonObject& result, qint64 elapsed);
        void milestone(const QString& key, const QJsonObject& value);
        void focused(const QString& module);

    private:
        QJsonObject stateFor(const QString& module, bool details = false) const;
        QJsonObject capabilitiesFor(const QString& module, const QJsonObject& catalog) const;
        QJsonObject schemaFor(const QString& module, const QString& name) const;
        QJsonObject validateEdit(
            const QString& module, const QString& name, const QJsonValue& revision) const;
        QJsonObject executeAction(const QJsonObject& arguments);
        QJsonObject prepareWorkflow(const QString& name, const QJsonObject& arguments) const;
        QJsonObject prepareSave(
            const QString& module, const QJsonObject& document, const QJsonObject& arguments) const;
        QJsonObject prepareStructured(const QString& name, const QJsonObject& arguments) const;
        void recordCompletion(QJsonObject& result, const QString& tool);
        void executePages(
            const QString& name, const QJsonObject& arguments, OfficeAiSequence::Completion completion);
        void executePagesReady(const QString& name, const QJsonObject& arguments,
            const QJsonArray& image_sizes, OfficeAiSequence::Completion completion);
        QJsonObject recordPageBatch(const QString& id, const QJsonObject& result);

        std::unique_ptr<OfficeAiSequence> sequence_;
        OfficeAiImageLibrary image_library_;
        OfficeAiFilePolicy file_policy_;
        QSet<QString> loaded_groups_;
        QString document_key_;
        QString desktop_directory_;
        QString response_revision_;
        QString chained_revision_;
        QJsonObject workflow_receipt_;
        QHash<QString, QJsonObject> page_batches_;
        QJsonObject page_theme_;
        QString deck_document_session_;
        int deck_target_pages_ = 0;
        int requested_slide_pages_ = 0;
        QString active_page_batch_;
        QByteArray last_style_edit_;
        QString last_style_generation_;
        int repeated_style_edits_ = 0;
        int repeated_edit_response_ = 0;
        int turns_ = 0;
        bool expected_document_change_ = false;
        bool observation_required_ = false;
        std::uint64_t page_preflight_epoch_ = 0;
    };
}
