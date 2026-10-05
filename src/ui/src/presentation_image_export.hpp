#pragma once

#include "presentation_scene.hpp"

#include <QVariantMap>

#include <atomic>

namespace mirrorfly
{
    struct PresentationImageExportOptions
    {
        QString format = "png";
        QString scope = "all";
        int long_edge = 1920;
    };

    struct PresentationImageExportProgress
    {
        std::atomic<bool> cancelled{false};
        std::atomic<int> completed{0};
        std::atomic<int> total{0};
    };

    struct PresentationImageExportResult
    {
        bool success = false;
        QString message;
        QString path;
        int pages = 0;
        qint64 bytes = 0;
    };

    PresentationImageExportResult render_presentation_images(const RenderPresentationPtr& document,
        int current_slide, const QVariantMap& theme, const QString& parent_path, const QString& document_name,
        const PresentationImageExportOptions& options, PresentationImageExportProgress& progress);
}
