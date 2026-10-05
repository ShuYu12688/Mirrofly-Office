#include <mirrorfly/pdf.hpp>

#include <utf8/checked.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace
{
    using mirrorfly::PdfAnnotation;
    using mirrorfly::PdfDocument;
    using mirrorfly::PdfEditResult;
    using mirrorfly::PdfError;
    using mirrorfly::PdfPage;

    PdfEditResult failure(PdfError error, const std::string& message)
    {
        PdfEditResult result;
        result.error = error;
        result.message = message;
        return result;
    }

    auto page_by_id(PdfDocument& document, const std::string& id)
    {
        return std::find_if(document.pages.begin(), document.pages.end(), [&id](const PdfPage& page)
        {
            return page.id == id;
        });
    }

    std::size_t annotation_count(const PdfDocument& document)
    {
        std::size_t count = 0;
        for (const auto& page : document.pages)
        {
            count += page.source_annotation_count - page.removed_annotation_ids.size() +
                page.added_annotations.size();
        }
        return count;
    }

    bool valid_rect(const PdfPage& page, const mirrorfly::PdfRect& rect)
    {
        if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
            !std::isfinite(rect.height) || rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0)
        {
            return false;
        }
        constexpr double tolerance = 0.01;
        return rect.x + rect.width <= page.width_points + tolerance &&
            rect.y + rect.height <= page.height_points + tolerance;
    }

    bool valid_stored_rect(const PdfPage& page, const mirrorfly::PdfRect& rect, bool allow_empty)
    {
        if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
            !std::isfinite(rect.height) || rect.x < 0 || rect.y < 0 || rect.width < 0 || rect.height < 0 ||
            (!allow_empty && (rect.width == 0 || rect.height == 0)))
        {
            return false;
        }
        constexpr double tolerance = 0.01;
        return rect.x + rect.width <= page.width_points + tolerance &&
            rect.y + rect.height <= page.height_points + tolerance;
    }

    bool valid_annotation_text(const std::string& text)
    {
        return text.size() <= mirrorfly::maximum_pdf_annotation_text_bytes &&
            text.find('\0') == std::string::npos && utf8::is_valid(text.begin(), text.end());
    }

    bool valid_added_kind(const PdfAnnotation& annotation)
    {
        if (annotation.kind == mirrorfly::PdfAnnotationKind::Stamp)
            return annotation.stamp_bytes && !annotation.stamp_bytes->empty() &&
                annotation.stamp_bytes->size() <= 8 * 1024 * 1024;
        return !annotation.stamp_bytes &&
            (annotation.kind == mirrorfly::PdfAnnotationKind::Text ||
                annotation.kind == mirrorfly::PdfAnnotationKind::Highlight);
    }

    PdfEditResult rotate_page(PdfDocument& document, const mirrorfly::PdfCommand& command)
    {
        auto page = page_by_id(document, command.page_id);
        if (page == document.pages.end())
        {
            return failure(PdfError::InvalidPage, "没有找到要旋转的 PDF 页面。");
        }
        const int turns = ((command.clockwise_quarter_turns % 4) + 4) % 4;
        if (turns == 0)
        {
            return {};
        }
        page->rotation = (page->rotation + turns) % 4;
        PdfEditResult result;
        result.changed = true;
        return result;
    }

    PdfEditResult delete_page(PdfDocument& document, const mirrorfly::PdfCommand& command)
    {
        auto page = page_by_id(document, command.page_id);
        if (page == document.pages.end())
        {
            return failure(PdfError::InvalidPage, "没有找到要删除的 PDF 页面。");
        }
        if (document.pages.size() == 1)
        {
            return failure(PdfError::InvalidPage, "PDF 必须至少保留一页。");
        }
        document.pages.erase(page);
        PdfEditResult result;
        result.changed = true;
        return result;
    }

    PdfEditResult move_page(PdfDocument& document, const mirrorfly::PdfCommand& command)
    {
        auto page = page_by_id(document, command.page_id);
        if (page == document.pages.end() || command.destination_index >= document.pages.size())
        {
            return failure(PdfError::InvalidPage, "PDF 页面位置无效。");
        }
        const auto source_index = static_cast<std::size_t>(page - document.pages.begin());
        if (source_index == command.destination_index)
        {
            return {};
        }
        PdfPage moved = std::move(*page);
        document.pages.erase(page);
        document.pages.insert(document.pages.begin() + static_cast<std::ptrdiff_t>(command.destination_index),
            std::move(moved));
        PdfEditResult result;
        result.changed = true;
        return result;
    }

    PdfEditResult add_annotation(PdfDocument& document, const mirrorfly::PdfCommand& command)
    {
        auto page = page_by_id(document, command.page_id);
        if (page == document.pages.end())
        {
            return failure(PdfError::InvalidPage, "没有找到要添加批注的 PDF 页面。");
        }
        if (!valid_added_kind(command.annotation))
        {
            return failure(PdfError::InvalidAnnotation, "批注或图文标注数据无效。");
        }
        if (!valid_rect(*page, command.annotation.rect) ||
            !valid_annotation_text(command.annotation.contents))
        {
            return failure(PdfError::InvalidAnnotation, "PDF 批注的位置或文字无效。");
        }
        const auto page_count = page->source_annotation_count - page->removed_annotation_ids.size() +
            page->added_annotations.size();
        if (page_count >= mirrorfly::maximum_pdf_page_annotations ||
            annotation_count(document) >= mirrorfly::maximum_pdf_annotations)
        {
            return failure(PdfError::TooLarge, "PDF 批注数量超过当前限制。");
        }

        PdfAnnotation annotation = command.annotation;
        if (document.next_annotation_id == std::numeric_limits<std::uint64_t>::max())
            return failure(PdfError::TooLarge, "批注编号已达上限。");
        annotation.id = "added-" + std::to_string(document.next_annotation_id++);
        annotation.source_index = -1;
        page->added_annotations.push_back(annotation);
        PdfEditResult result;
        result.changed = true;
        result.created_annotation_id = annotation.id;
        return result;
    }

    PdfEditResult delete_annotation(PdfDocument& document, const mirrorfly::PdfCommand& command)
    {
        auto page = page_by_id(document, command.page_id);
        if (page == document.pages.end())
        {
            return failure(PdfError::InvalidPage, "没有找到批注所在的 PDF 页面。");
        }
        auto added = std::find_if(page->added_annotations.begin(), page->added_annotations.end(),
            [&command](const PdfAnnotation& annotation)
        {
            return annotation.id == command.annotation_id;
        });
        if (added != page->added_annotations.end())
        {
            page->added_annotations.erase(added);
            PdfEditResult result;
            result.changed = true;
            return result;
        }
        const auto original = std::find_if(page->original_annotations.begin(),
            page->original_annotations.end(), [&command](const PdfAnnotation& annotation)
        {
            return annotation.id == command.annotation_id;
        });
        if (original == page->original_annotations.end())
        {
            return failure(PdfError::InvalidAnnotation, "没有找到要删除的 PDF 批注。");
        }
        if (original->kind == mirrorfly::PdfAnnotationKind::Other)
        {
            return failure(PdfError::InvalidAnnotation, "当前不会删除不支持的原始批注。");
        }
        if (!page->removed_annotation_ids.insert(original->id).second)
        {
            return {};
        }
        PdfEditResult result;
        result.changed = true;
        return result;
    }
}

namespace mirrorfly
{
    bool is_pdf_path(const std::string& path)
    {
        const auto separator = path.find_last_of("/\\");
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos || (separator != std::string::npos && dot < separator))
        {
            return false;
        }
        std::string extension = path.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return extension == ".pdf";
    }

    double pdf_page_display_width(const PdfPage& page)
    {
        return page.rotation % 2 == 0 ? page.width_points : page.height_points;
    }

    double pdf_page_display_height(const PdfPage& page)
    {
        return page.rotation % 2 == 0 ? page.height_points : page.width_points;
    }

    std::string validate_pdf(const PdfDocument& document)
    {
        if (!document.source_bytes || document.source_bytes->empty() ||
            document.source_bytes->size() > maximum_pdf_bytes || document.source_page_count == 0 ||
            document.source_page_count > maximum_pdf_pages || document.pages.empty() ||
            document.pages.size() > document.source_page_count)
        {
            return "PDF 文档状态无效。";
        }
        std::set<std::string> page_ids;
        std::set<std::size_t> source_indices;
        std::set<std::string> annotation_ids;
        std::size_t total_annotations = 0;
        std::size_t stamp_bytes = 0;
        for (const auto& page : document.pages)
        {
            if (page.id.empty() || !page_ids.insert(page.id).second ||
                page.source_index >= document.source_page_count ||
                !source_indices.insert(page.source_index).second || !std::isfinite(page.width_points) ||
                !std::isfinite(page.height_points) || !std::isfinite(page.origin_x_points) ||
                !std::isfinite(page.origin_y_points) || page.width_points <= 0 || page.height_points <= 0 ||
                page.rotation < 0 || page.rotation > 3 || page.text.size() > maximum_pdf_page_text_bytes ||
                page.text.find('\0') != std::string::npos ||
                !utf8::is_valid(page.text.begin(), page.text.end()) ||
                page.source_annotation_count > maximum_pdf_page_annotations ||
                page.original_annotations.size() > page.source_annotation_count ||
                page.removed_annotation_ids.size() > page.source_annotation_count)
            {
                return "PDF 页面状态无效。";
            }
            std::set<std::string> original_ids;
            std::set<int> original_indices;
            std::size_t active_annotations = page.source_annotation_count -
                page.removed_annotation_ids.size() + page.added_annotations.size();
            for (const auto& annotation : page.original_annotations)
            {
                if (annotation.id.empty() || annotation.source_index < 0 ||
                    static_cast<std::size_t>(annotation.source_index) >= page.source_annotation_count ||
                    !annotation_ids.insert(annotation.id).second ||
                    !original_indices.insert(annotation.source_index).second ||
                    !valid_stored_rect(page, annotation.rect, true) ||
                    !valid_annotation_text(annotation.contents))
                {
                    return "PDF 原始批注状态无效。";
                }
                original_ids.insert(annotation.id);
            }
            for (const auto& id : page.removed_annotation_ids)
            {
                if (original_ids.count(id) == 0)
                {
                    return "PDF 批注删除记录无效。";
                }
            }
            for (const auto& annotation : page.added_annotations)
            {
                if (annotation.id.empty() || annotation.source_index != -1 || !valid_added_kind(annotation) ||
                    !annotation_ids.insert(annotation.id).second ||
                    !valid_stored_rect(page, annotation.rect, false) ||
                    !valid_annotation_text(annotation.contents))
                {
                    return "PDF 新批注状态无效。";
                }
                if (annotation.stamp_bytes)
                    stamp_bytes += annotation.stamp_bytes->size();
                if (stamp_bytes > 24 * 1024 * 1024)
                    return "图文标注合计超过 24 MiB。";
            }
            if (active_annotations > maximum_pdf_page_annotations ||
                total_annotations > maximum_pdf_annotations - active_annotations)
            {
                return "PDF 批注数量超过当前限制。";
            }
            total_annotations += active_annotations;
        }
        return {};
    }

    PdfEditResult apply_pdf_command(PdfDocument& document, const PdfCommand& command)
    {
        if (!document.editable)
        {
            return failure(PdfError::ReadOnly,
                document.read_only_reason.empty() ? "此 PDF 只能阅读。" : document.read_only_reason);
        }
        const std::string validation = validate_pdf(document);
        if (!validation.empty())
        {
            return failure(PdfError::InvalidDocument, validation);
        }

        PdfDocument candidate = document;
        PdfEditResult result;
        switch (command.kind)
        {
        case PdfCommandKind::RotatePage:
            result = rotate_page(candidate, command);
            break;
        case PdfCommandKind::DeletePage:
            result = delete_page(candidate, command);
            break;
        case PdfCommandKind::MovePage:
            result = move_page(candidate, command);
            break;
        case PdfCommandKind::AddAnnotation:
            result = add_annotation(candidate, command);
            break;
        case PdfCommandKind::DeleteAnnotation:
            result = delete_annotation(candidate, command);
            break;
        }
        if (result.error == PdfError::None && result.changed)
        {
            const std::string candidate_validation = validate_pdf(candidate);
            if (!candidate_validation.empty())
            {
                return failure(PdfError::InvalidDocument, candidate_validation);
            }
            document = std::move(candidate);
        }
        return result;
    }
}
