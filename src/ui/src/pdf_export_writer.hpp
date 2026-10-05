#pragma once

#include <QVariantMap>
#include <atomic>
#include <mirrorfly/pdf_export.hpp>
#include <mirrorfly/pdf_storage.hpp>

namespace mirrorfly
{
    struct PdfExportOptions
    {
        QString scope = "all";
        QString layout = "fit";
        QString compression = "structure";
        bool landscape = false;
    };

    struct PdfExportProgress
    {
        std::atomic<bool> cancelled{false};
        std::atomic<int> completed{0};
        std::atomic<int> total{0};
    };

    PdfBytesResult render_pdf_export(const PdfExportSource& source, const PdfExportOptions& options,
        const QVariantMap& theme, PdfExportProgress& progress);
}
