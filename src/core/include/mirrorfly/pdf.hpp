#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace mirrorfly
{
    constexpr std::size_t maximum_pdf_bytes = 64 * 1024 * 1024;
    constexpr std::size_t maximum_pdf_pages = 250;
    constexpr std::size_t maximum_pdf_render_pixels = 8 * 1024 * 1024;
    constexpr std::size_t maximum_pdf_page_text_bytes = 64 * 1024;
    constexpr std::size_t maximum_pdf_annotation_text_bytes = 8 * 1024;
    constexpr std::size_t maximum_pdf_page_annotations = 100;
    constexpr std::size_t maximum_pdf_annotations = 1000;

    enum class PdfError
    {
        None,
        UnsupportedType,
        ReadFailed,
        TooLarge,
        InvalidDocument,
        PasswordProtected,
        ReadOnly,
        SignedDocument,
        InvalidPage,
        InvalidAnnotation,
        RenderFailed,
        WriteFailed,
        ChangedOnDisk
    };

    enum class PdfAnnotationKind
    {
        Text,
        Highlight,
        Other,
        Stamp
    };

    // PDF user-space points, with the origin at the unrotated page's bottom-left.
    struct PdfRect
    {
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
    };

    struct PdfAnnotation
    {
        std::string id;
        PdfAnnotationKind kind = PdfAnnotationKind::Text;
        PdfRect rect;
        std::string contents;
        int source_index = -1;
        // One vector PDF page, flattened into page content when saving a copy.
        std::shared_ptr<const std::vector<std::uint8_t>> stamp_bytes;
    };

    struct PdfPage
    {
        std::string id;
        std::size_t source_index = 0;
        // Unrotated crop-box size. Annotation coordinates are local to its bottom-left.
        double origin_x_points = 0;
        double origin_y_points = 0;
        double width_points = 0;
        double height_points = 0;
        int rotation = 0;
        std::string text;
        bool text_truncated = false;
        std::size_t source_annotation_count = 0;
        std::vector<PdfAnnotation> original_annotations;
        std::vector<PdfAnnotation> added_annotations;
        std::set<std::string> removed_annotation_ids;
    };

    struct PdfDocument
    {
        std::shared_ptr<const std::vector<std::uint8_t>> source_bytes;
        std::size_t source_page_count = 0;
        std::vector<PdfPage> pages;
        bool editable = false;
        std::string read_only_reason;
        std::uint64_t next_annotation_id = 1;
    };

    enum class PdfCommandKind
    {
        RotatePage,
        DeletePage,
        MovePage,
        AddAnnotation,
        DeleteAnnotation
    };

    struct PdfCommand
    {
        PdfCommandKind kind = PdfCommandKind::RotatePage;
        std::string page_id;
        std::size_t destination_index = 0;
        int clockwise_quarter_turns = 1;
        PdfAnnotation annotation;
        std::string annotation_id;
    };

    struct PdfEditResult
    {
        PdfError error = PdfError::None;
        std::string message;
        bool changed = false;
        std::string created_annotation_id;
    };

    struct PdfResult
    {
        PdfError error = PdfError::None;
        std::string message;
        PdfDocument document;
        std::string path;
        std::string revision;
    };

    bool is_pdf_path(const std::string& path);
    double pdf_page_display_width(const PdfPage& page);
    double pdf_page_display_height(const PdfPage& page);
    std::string validate_pdf(const PdfDocument& document);
    PdfEditResult apply_pdf_command(PdfDocument& document, const PdfCommand& command);
}
