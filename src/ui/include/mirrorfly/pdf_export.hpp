#pragma once
#include <memory>
#include <mirrorfly/mindmap.hpp>
#include <mirrorfly/pdf.hpp>
#include <mirrorfly/presentation.hpp>
#include <mirrorfly/spreadsheet.hpp>
#include <mirrorfly/word.hpp>
#include <string>
#include <variant>

namespace mirrorfly
{
    struct PdfTextSource
    {
        std::string text;
        bool markdown = false;
    };

    // Immutable capture of the supported editor model; no GUI objects cross the worker boundary.
    struct PdfExportSource
    {
        using Content = std::variant<std::monostate, PdfTextSource, WordDocument, SpreadsheetDocument,
            std::shared_ptr<const PresentationScene>, MindMapDocument, PdfDocument>;
        Content content;
        std::string title;
        std::string source_path;
        std::string protected_source_path;
        std::string error;
        std::size_t current = 0;
        SpreadsheetRange selection;
    };
}
