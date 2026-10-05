#pragma once

#include <mirrorfly/pdf.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mirrorfly
{
    struct PdfRenderResult
    {
        PdfError error = PdfError::None;
        std::string message;
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgba;
    };

    struct PdfSaveResult
    {
        PdfError error = PdfError::None;
        std::string message;
        std::string path;
        std::string revision;
    };
    struct PdfBytesResult
    {
        PdfError error = PdfError::None;
        std::string message;
        std::vector<std::uint8_t> bytes;
    };

    PdfResult load_pdf_file(const std::string& path);
    PdfSaveResult inspect_pdf_destination(const std::string& path);
    PdfSaveResult save_pdf_bytes(const std::string& path, const std::vector<std::uint8_t>& bytes,
        const std::string& expected_revision);
    PdfBytesResult serialize_pdf_document(const PdfDocument& document);
    // Lossy page rasterization. Returning false from progress cancels before publication.
    PdfBytesResult compress_pdf_document(const PdfDocument& document, int dpi, int quality,
        const std::function<bool(int, int)>& progress = {});
    // Renders the current page order, rotation and annotation overlays into tightly packed RGBA8 pixels.
    PdfRenderResult render_pdf_page(
        const PdfDocument& document, const std::string& page_id, int width, int height);
    // expected_revision="missing" requires a new destination.
    PdfSaveResult save_pdf_file(
        const std::string& path, const PdfDocument& document, const std::string& expected_revision);
}
