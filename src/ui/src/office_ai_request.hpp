#pragma once

#include <QJsonObject>
#include <QStringList>

namespace mirrorfly
{
    // Conservative explicit constraints from direct human text, not a general intent parser.
    struct OfficeAiTaskConstraints
    {
        enum class Save
        {
            Unspecified,
            Required,
            Forbidden
        };
        Save save = Save::Unspecified;
        bool document_work = false;
        bool preserve_original = false;
        bool preserve_current = false;
        int file_count = 0;
        int slide_pages = 0;
        QStringList save_formats;

        void merge(const OfficeAiTaskConstraints& update);
        QJsonObject checkpoint() const;
    };

    OfficeAiTaskConstraints office_ai_task_constraints(const QString& request);
    QString office_ai_direct_request_text(const QString& request);
    int office_ai_requested_file_count(const QString& request);
    bool office_ai_requested_document_work(const QString& request);
}
