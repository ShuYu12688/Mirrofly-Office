#include <mirrorfly/pdf_storage.hpp>

#include <QBuffer>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QString>

#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>
#include <fpdf_signature.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace
{
    using mirrorfly::PdfAnnotation;
    using mirrorfly::PdfAnnotationKind;
    using mirrorfly::PdfDocument;
    using mirrorfly::PdfError;
    using mirrorfly::PdfPage;

    class PdfiumRuntime
    {
    public:
        PdfiumRuntime()
        {
            FPDF_LIBRARY_CONFIG config{};
            config.version = 2;
            FPDF_InitLibraryWithConfig(&config);
        }

        ~PdfiumRuntime()
        {
            FPDF_DestroyLibrary();
        }

        std::mutex mutex;
    };

    PdfiumRuntime& runtime()
    {
        static PdfiumRuntime instance;
        return instance;
    }

    struct DocumentCloser
    {
        void operator()(FPDF_DOCUMENT document) const
        {
            if (document)
            {
                FPDF_CloseDocument(document);
            }
        }
    };

    struct PageCloser
    {
        void operator()(FPDF_PAGE page) const
        {
            if (page)
            {
                FPDF_ClosePage(page);
            }
        }
    };

    struct TextPageCloser
    {
        void operator()(FPDF_TEXTPAGE page) const
        {
            if (page)
            {
                FPDFText_ClosePage(page);
            }
        }
    };

    struct BitmapCloser
    {
        void operator()(FPDF_BITMAP bitmap) const
        {
            if (bitmap)
            {
                FPDFBitmap_Destroy(bitmap);
            }
        }
    };

    struct AnnotationCloser
    {
        void operator()(FPDF_ANNOTATION annotation) const
        {
            if (annotation)
            {
                FPDFPage_CloseAnnot(annotation);
            }
        }
    };

    using DocumentHandle = std::unique_ptr<std::remove_pointer_t<FPDF_DOCUMENT>, DocumentCloser>;
    using PageHandle = std::unique_ptr<std::remove_pointer_t<FPDF_PAGE>, PageCloser>;
    using TextPageHandle = std::unique_ptr<std::remove_pointer_t<FPDF_TEXTPAGE>, TextPageCloser>;
    using BitmapHandle = std::unique_ptr<std::remove_pointer_t<FPDF_BITMAP>, BitmapCloser>;
    using AnnotationHandle = std::unique_ptr<std::remove_pointer_t<FPDF_ANNOTATION>, AnnotationCloser>;

    QString from_utf8(const std::string& value)
    {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }

    std::string to_utf8(const QString& value)
    {
        const QByteArray bytes = value.toUtf8();
        return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }

    std::string revision_of(const QByteArray& bytes)
    {
        return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
    }

    bool read_bytes(const QString& path, QByteArray& bytes, std::string& message)
    {
        const QFileInfo information(path);
        if (!information.exists() || !information.isFile() || information.isSymLink() ||
            information.isJunction())
        {
            message = "文件不存在、不是普通文件或是不支持的链接。";
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.isSequential())
        {
            message = "无法打开文件。";
            return false;
        }
        constexpr qint64 maximum = static_cast<qint64>(mirrorfly::maximum_pdf_bytes);
        if (file.size() > maximum)
        {
            message = "PDF 超过 64 MiB 限制。";
            return false;
        }
        bytes = file.read(maximum + 1);
        if (bytes.size() > maximum)
        {
            bytes.clear();
            message = "PDF 超过 64 MiB 限制。";
            return false;
        }
        if (file.error() != QFileDevice::NoError)
        {
            bytes.clear();
            message = "读取文件时发生错误。";
            return false;
        }
        return true;
    }

    bool writable_destination(const QString& path)
    {
        const QFileInfo information(path);
        if (information.isSymLink() || information.isJunction())
        {
            return false;
        }
        if (information.exists() && (!information.isFile() || !information.isWritable()))
        {
            return false;
        }
        const QFileInfo parent(information.absolutePath());
        return parent.exists() && parent.isDir();
    }

    bool revision_matches(const QString& path, const std::string& expected_revision)
    {
        if (expected_revision == "missing")
        {
            const QFileInfo target(path);
            return !target.exists() && !target.isSymLink() && !target.isJunction();
        }
        if (expected_revision.empty())
        {
            return true;
        }
        QByteArray current;
        std::string message;
        return read_bytes(path, current, message) && revision_of(current) == expected_revision;
    }

    DocumentHandle load_document(const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.empty() || bytes.size() > mirrorfly::maximum_pdf_bytes)
        {
            return {};
        }
        return DocumentHandle(FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr));
    }

    PdfAnnotationKind annotation_kind(FPDF_ANNOTATION annotation)
    {
        switch (FPDFAnnot_GetSubtype(annotation))
        {
        case FPDF_ANNOT_TEXT:
            return PdfAnnotationKind::Text;
        case FPDF_ANNOT_HIGHLIGHT:
            return PdfAnnotationKind::Highlight;
        default:
            return PdfAnnotationKind::Other;
        }
    }

    std::string annotation_contents(FPDF_ANNOTATION annotation)
    {
        const unsigned long bytes = FPDFAnnot_GetStringValue(annotation, "Contents", nullptr, 0);
        if (bytes < sizeof(unsigned short))
        {
            return {};
        }
        const auto maximum_utf16_bytes =
            (mirrorfly::maximum_pdf_annotation_text_bytes + 1) * sizeof(unsigned short);
        if (bytes > maximum_utf16_bytes)
        {
            return {};
        }
        std::vector<unsigned short> buffer((bytes + sizeof(unsigned short) - 1) / sizeof(unsigned short));
        const unsigned long copied = FPDFAnnot_GetStringValue(
            annotation, "Contents", buffer.data(), static_cast<unsigned long>(buffer.size() * 2));
        if (copied == 0 || copied > buffer.size() * 2)
        {
            return {};
        }
        const auto units = copied / 2 > 0 ? copied / 2 - 1 : 0;
        const QString decoded = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(buffer.data()), static_cast<qsizetype>(units));
        QByteArray encoded = decoded.toUtf8();
        if (encoded.size() > static_cast<qsizetype>(mirrorfly::maximum_pdf_annotation_text_bytes))
        {
            qsizetype size = static_cast<qsizetype>(mirrorfly::maximum_pdf_annotation_text_bytes);
            while (size > 0 && (static_cast<unsigned char>(encoded.at(size)) & 0xc0) == 0x80)
            {
                --size;
            }
            encoded.truncate(size);
        }
        return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
    }

    bool append_text(QString& destination, const QString& chunk, bool& truncated)
    {
        const auto current_size = destination.toUtf8().size();
        if (current_size >= static_cast<qsizetype>(mirrorfly::maximum_pdf_page_text_bytes))
        {
            truncated = true;
            return false;
        }
        const auto remaining = static_cast<qsizetype>(mirrorfly::maximum_pdf_page_text_bytes) - current_size;
        if (chunk.toUtf8().size() <= remaining)
        {
            destination += chunk;
            return true;
        }
        qsizetype low = 0;
        qsizetype high = chunk.size();
        while (low < high)
        {
            const qsizetype middle = low + (high - low + 1) / 2;
            if (chunk.left(middle).toUtf8().size() <= remaining)
            {
                low = middle;
            }
            else
            {
                high = middle - 1;
            }
        }
        if (low > 0 && chunk.at(low - 1).isHighSurrogate())
        {
            --low;
        }
        destination += chunk.left(low);
        truncated = true;
        return false;
    }

    std::string extract_page_text(FPDF_PAGE page, bool& truncated)
    {
        TextPageHandle text_page(FPDFText_LoadPage(page));
        if (!text_page)
        {
            return {};
        }
        const int count = FPDFText_CountChars(text_page.get());
        if (count <= 0)
        {
            return {};
        }
        QString text;
        constexpr int chunk_size = 4096;
        std::vector<unsigned short> buffer(chunk_size + 1);
        for (int offset = 0; offset < count; offset += chunk_size)
        {
            const int requested = (std::min)(chunk_size, count - offset);
            const int copied = FPDFText_GetText(text_page.get(), offset, requested, buffer.data());
            if (copied <= 1)
            {
                continue;
            }
            const QString chunk = QString::fromUtf16(
                reinterpret_cast<const char16_t*>(buffer.data()), static_cast<qsizetype>(copied - 1));
            if (!append_text(text, chunk, truncated))
            {
                break;
            }
        }
        return to_utf8(text);
    }

    bool load_annotations(FPDF_PAGE page, PdfPage& output, std::size_t& total, std::string& message)
    {
        const int count = FPDFPage_GetAnnotCount(page);
        if (count < 0 || count > static_cast<int>(mirrorfly::maximum_pdf_page_annotations) ||
            total + static_cast<std::size_t>(count) > mirrorfly::maximum_pdf_annotations)
        {
            message = "PDF 批注数量超过当前限制。";
            return false;
        }
        output.original_annotations.reserve(static_cast<std::size_t>(count));
        output.source_annotation_count = static_cast<std::size_t>(count);
        for (int index = 0; index < count; ++index)
        {
            AnnotationHandle annotation(FPDFPage_GetAnnot(page, index));
            if (!annotation)
            {
                message = "无法读取 PDF 批注。";
                return false;
            }
            FS_RECTF rectangle{};
            if (!FPDFAnnot_GetRect(annotation.get(), &rectangle))
            {
                continue;
            }
            const double x = rectangle.left - output.origin_x_points;
            const double y = rectangle.bottom - output.origin_y_points;
            const double width = rectangle.right - rectangle.left;
            const double height = rectangle.top - rectangle.bottom;
            constexpr double tolerance = 0.01;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
                x < 0 || y < 0 || width < 0 || height < 0 || x + width > output.width_points + tolerance ||
                y + height > output.height_points + tolerance)
            {
                continue;
            }
            PdfAnnotation value;
            value.id = output.id + "-annotation-" + std::to_string(index + 1);
            value.kind = annotation_kind(annotation.get());
            value.rect = {x, y, width, height};
            value.contents = annotation_contents(annotation.get());
            value.source_index = index;
            output.original_annotations.push_back(std::move(value));
        }
        total += static_cast<std::size_t>(count);
        return true;
    }

    bool add_annotation(
        FPDF_DOCUMENT document, FPDF_PAGE page, const PdfPage& model_page, const PdfAnnotation& value)
    {
        if (value.kind == PdfAnnotationKind::Stamp)
        {
            auto stamp = value.stamp_bytes ? load_document(*value.stamp_bytes) : DocumentHandle{};
            if (!stamp || FPDF_GetPageCount(stamp.get()) != 1)
                return false;
            PageHandle source(FPDF_LoadPage(stamp.get(), 0));
            if (!source)
                return false;
            const double width = FPDF_GetPageWidth(source.get());
            const double height = FPDF_GetPageHeight(source.get());
            if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
                return false;
            auto xobject = FPDF_NewXObjectFromPage(document, stamp.get(), 0);
            if (!xobject)
                return false;
            auto object = FPDF_NewFormObjectFromXObject(xobject);
            FPDF_CloseXObject(xobject);
            if (!object)
                return false;
            FPDFPageObj_Transform(object, value.rect.width / width, 0, 0, value.rect.height / height,
                model_page.origin_x_points + value.rect.x, model_page.origin_y_points + value.rect.y);
            FPDFPage_InsertObject(page, object);
            return FPDFPage_GenerateContent(page) != 0;
        }
        const int subtype = value.kind == PdfAnnotationKind::Text ? FPDF_ANNOT_TEXT : FPDF_ANNOT_HIGHLIGHT;
        AnnotationHandle annotation(FPDFPage_CreateAnnot(page, subtype));
        if (!annotation)
        {
            return false;
        }
        const FS_RECTF rectangle{static_cast<float>(model_page.origin_x_points + value.rect.x),
            static_cast<float>(model_page.origin_y_points + value.rect.y + value.rect.height),
            static_cast<float>(model_page.origin_x_points + value.rect.x + value.rect.width),
            static_cast<float>(model_page.origin_y_points + value.rect.y)};
        if (!FPDFAnnot_SetRect(annotation.get(), &rectangle))
        {
            return false;
        }
        const QString contents = from_utf8(value.contents);
        if (!FPDFAnnot_SetStringValue(
                annotation.get(), "Contents", reinterpret_cast<FPDF_WIDESTRING>(contents.utf16())))
        {
            return false;
        }
        if (value.kind == PdfAnnotationKind::Highlight)
        {
            const FS_QUADPOINTSF points{rectangle.left, rectangle.top, rectangle.right, rectangle.top,
                rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom};
            if (!FPDFAnnot_AppendAttachmentPoints(annotation.get(), &points) ||
                !FPDFAnnot_SetColor(annotation.get(), FPDFANNOT_COLORTYPE_Color, 255, 224, 92, 128))
            {
                return false;
            }
        }
        else if (!FPDFAnnot_SetColor(annotation.get(), FPDFANNOT_COLORTYPE_Color, 196, 112, 68, 255))
        {
            return false;
        }
        return true;
    }

    bool validate_model(const PdfDocument& model, std::string& message)
    {
        message = mirrorfly::validate_pdf(model);
        return message.empty();
    }

    DocumentHandle editable_copy(
        const PdfDocument& model, bool enforce_edit_permission, PdfError& error, std::string& message)
    {
        if (!validate_model(model, message))
        {
            error = PdfError::InvalidDocument;
            return {};
        }
        DocumentHandle source = load_document(*model.source_bytes);
        DocumentHandle destination(FPDF_CreateNewDocument());
        if (!source || !destination)
        {
            error = PdfError::InvalidDocument;
            message = "无法重新打开 PDF 原始数据。";
            return {};
        }
        if (FPDF_GetPageCount(source.get()) != static_cast<int>(model.source_page_count))
        {
            error = PdfError::InvalidDocument;
            message = "PDF 原始页面记录不一致。";
            return {};
        }
        if (enforce_edit_permission)
        {
            if (FPDF_GetSignatureCount(source.get()) > 0)
            {
                error = PdfError::SignedDocument;
                message = "含数字签名的 PDF 不能保存编辑副本。";
                return {};
            }
            const unsigned long permissions = FPDF_GetDocPermissions(source.get());
            constexpr unsigned long modify_pages = 1UL << 3;
            constexpr unsigned long modify_annotations = 1UL << 5;
            if (permissions != 0xffffffffUL &&
                ((permissions & modify_pages) == 0 || (permissions & modify_annotations) == 0))
            {
                error = PdfError::ReadOnly;
                message = "此 PDF 的权限不允许页面和批注编辑。";
                return {};
            }
        }
        std::vector<int> indices;
        indices.reserve(model.pages.size());
        for (const auto& page : model.pages)
        {
            if (page.source_index >= model.source_page_count ||
                page.source_index > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                error = PdfError::InvalidPage;
                message = "PDF 页面来源无效。";
                return {};
            }
            indices.push_back(static_cast<int>(page.source_index));
        }
        if (!FPDF_ImportPagesByIndex(destination.get(), source.get(), indices.data(),
                static_cast<unsigned long>(indices.size()), 0))
        {
            error = PdfError::InvalidDocument;
            message = "无法保留 PDF 原始页面。";
            return {};
        }
        for (std::size_t page_index = 0; page_index < model.pages.size(); ++page_index)
        {
            PageHandle page(FPDF_LoadPage(destination.get(), static_cast<int>(page_index)));
            if (!page)
            {
                error = PdfError::InvalidPage;
                message = "无法载入 PDF 页面。";
                return {};
            }
            const auto& model_page = model.pages[page_index];
            FPDFPage_SetRotation(page.get(), model_page.rotation);

            const int imported_annotation_count = FPDFPage_GetAnnotCount(page.get());
            if (imported_annotation_count < 0 ||
                std::any_of(model_page.original_annotations.begin(), model_page.original_annotations.end(),
                    [imported_annotation_count](const PdfAnnotation& annotation)
            {
                return annotation.source_index >= imported_annotation_count;
            }))
            {
                error = PdfError::InvalidAnnotation;
                message = "PDF 原始批注索引无效。";
                return {};
            }

            std::vector<int> removals;
            for (const auto& annotation : model_page.original_annotations)
            {
                if (model_page.removed_annotation_ids.count(annotation.id) != 0)
                {
                    removals.push_back(annotation.source_index);
                }
            }
            std::sort(removals.rbegin(), removals.rend());
            for (const int annotation_index : removals)
            {
                if (annotation_index < 0 || !FPDFPage_RemoveAnnot(page.get(), annotation_index))
                {
                    error = PdfError::InvalidAnnotation;
                    message = "无法删除 PDF 原始批注。";
                    return {};
                }
            }
            for (const auto& annotation : model_page.added_annotations)
            {
                if (!add_annotation(destination.get(), page.get(), model_page, annotation))
                {
                    error = PdfError::InvalidAnnotation;
                    message = "无法写入 PDF 批注。";
                    return {};
                }
            }
        }
        return destination;
    }

    struct MemoryWriter : FPDF_FILEWRITE
    {
        std::vector<std::uint8_t> bytes;
        bool too_large = false;
    };

    int write_block(FPDF_FILEWRITE* base, const void* data, unsigned long size)
    {
        auto* writer = static_cast<MemoryWriter*>(base);
        if (size > mirrorfly::maximum_pdf_bytes - writer->bytes.size())
        {
            writer->too_large = true;
            return 0;
        }
        const auto* first = static_cast<const std::uint8_t*>(data);
        try
        {
            writer->bytes.insert(writer->bytes.end(), first, first + size);
        }
        catch (...)
        {
            return 0;
        }
        return 1;
    }

    bool save_to_memory(FPDF_DOCUMENT document, MemoryWriter& writer)
    {
        writer.version = 1;
        writer.WriteBlock = write_block;
        return FPDF_SaveAsCopy(document, &writer, FPDF_NO_INCREMENTAL) != 0;
    }

    PdfError loading_error()
    {
        return FPDF_GetLastError() == FPDF_ERR_PASSWORD ? PdfError::PasswordProtected
                                                        : PdfError::InvalidDocument;
    }
}

namespace mirrorfly
{
    PdfResult load_pdf_file(const std::string& path)
    {
        PdfResult result;
        if (!is_pdf_path(path))
        {
            result.error = PdfError::UnsupportedType;
            result.message = "当前 PDF 模块只支持 .pdf 文件。";
            return result;
        }
        const QString absolute_path = QDir::cleanPath(QFileInfo(from_utf8(path)).absoluteFilePath());
        result.path = to_utf8(absolute_path);
        QByteArray bytes;
        if (!read_bytes(absolute_path, bytes, result.message))
        {
            result.error = bytes.size() > static_cast<qsizetype>(maximum_pdf_bytes) ? PdfError::TooLarge
                                                                                    : PdfError::ReadFailed;
            if (result.message.find("64 MiB") != std::string::npos)
            {
                result.error = PdfError::TooLarge;
            }
            return result;
        }

        std::lock_guard lock(runtime().mutex);
        auto source = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
        DocumentHandle document = load_document(*source);
        if (!document)
        {
            result.error = loading_error();
            result.message = result.error == PdfError::PasswordProtected
                ? "此 PDF 需要密码，当前版本不接收或保存密码。"
                : "PDF 文件损坏或格式不受支持。";
            return result;
        }
        const int page_count = FPDF_GetPageCount(document.get());
        if (page_count <= 0 || page_count > static_cast<int>(maximum_pdf_pages))
        {
            if (page_count > static_cast<int>(maximum_pdf_pages))
            {
                result.error = PdfError::TooLarge;
                result.message = "PDF 超过 250 页限制。";
            }
            else
            {
                result.error = PdfError::InvalidDocument;
                result.message = "PDF 没有可读取的页面。";
            }
            return result;
        }

        PdfDocument model;
        model.source_bytes = source;
        model.source_page_count = static_cast<std::size_t>(page_count);
        model.pages.reserve(static_cast<std::size_t>(page_count));
        std::size_t total_annotations = 0;
        for (int index = 0; index < page_count; ++index)
        {
            PageHandle page(FPDF_LoadPage(document.get(), index));
            if (!page)
            {
                result.error = PdfError::InvalidPage;
                result.message = "无法读取 PDF 页面。";
                return result;
            }
            const int rotation = FPDFPage_GetRotation(page.get());
            FS_RECTF bounds{};
            if (!FPDF_GetPageBoundingBox(page.get(), &bounds) || !std::isfinite(bounds.left) ||
                !std::isfinite(bounds.bottom) || !std::isfinite(bounds.right) || !std::isfinite(bounds.top) ||
                bounds.right <= bounds.left || bounds.top <= bounds.bottom)
            {
                result.error = PdfError::InvalidPage;
                result.message = "PDF 页面尺寸无效。";
                return result;
            }
            PdfPage model_page;
            model_page.id = "page-" + std::to_string(index + 1);
            model_page.source_index = static_cast<std::size_t>(index);
            model_page.origin_x_points = bounds.left;
            model_page.origin_y_points = bounds.bottom;
            model_page.width_points = bounds.right - bounds.left;
            model_page.height_points = bounds.top - bounds.bottom;
            model_page.rotation = rotation;
            model_page.text = extract_page_text(page.get(), model_page.text_truncated);
            if (!load_annotations(page.get(), model_page, total_annotations, result.message))
            {
                result.error = PdfError::TooLarge;
                return result;
            }
            model.pages.push_back(std::move(model_page));
        }

        const bool signed_document = FPDF_GetSignatureCount(document.get()) > 0;
        const unsigned long permissions = FPDF_GetDocPermissions(document.get());
        constexpr unsigned long modify_pages = 1UL << 3;
        constexpr unsigned long modify_annotations = 1UL << 5;
        const bool has_edit_permissions = permissions == 0xffffffffUL ||
            ((permissions & modify_pages) != 0 && (permissions & modify_annotations) != 0);
        model.editable = !signed_document && has_edit_permissions;
        if (signed_document)
        {
            model.read_only_reason = "含数字签名的 PDF 仅供阅读，避免使签名失效。";
        }
        else if (!has_edit_permissions)
        {
            model.read_only_reason = "此 PDF 的权限不允许页面和批注编辑。";
        }
        result.message = validate_pdf(model);
        if (!result.message.empty())
        {
            result.error = PdfError::InvalidDocument;
            return result;
        }
        result.document = std::move(model);
        result.revision = revision_of(bytes);
        return result;
    }

    PdfRenderResult render_pdf_page(
        const PdfDocument& document, const std::string& page_id, int width, int height)
    {
        PdfRenderResult result;
        if (width <= 0 || height <= 0 ||
            static_cast<std::uint64_t>(width) * height > maximum_pdf_render_pixels)
        {
            result.error = PdfError::TooLarge;
            result.message = "PDF 渲染尺寸超过 800 万像素限制。";
            return result;
        }
        const auto model_page =
            std::find_if(document.pages.begin(), document.pages.end(), [&page_id](const PdfPage& page)
        {
            return page.id == page_id;
        });
        if (model_page == document.pages.end())
        {
            result.error = PdfError::InvalidPage;
            result.message = "没有找到要渲染的 PDF 页面。";
            return result;
        }

        const std::string validation = validate_pdf(document);
        if (!validation.empty())
        {
            result.error = PdfError::InvalidDocument;
            result.message = validation;
            return result;
        }
        PdfDocument preview;
        preview.source_bytes = document.source_bytes;
        preview.source_page_count = document.source_page_count;
        preview.editable = document.editable;
        preview.read_only_reason = document.read_only_reason;
        preview.next_annotation_id = document.next_annotation_id;
        preview.pages.push_back(*model_page);
        std::lock_guard lock(runtime().mutex);
        PdfError error = PdfError::None;
        // Import only the visible page; saving still retains the complete ordered document.
        DocumentHandle copy = editable_copy(preview, false, error, result.message);
        if (!copy)
        {
            result.error = error;
            return result;
        }
        PageHandle page(FPDF_LoadPage(copy.get(), 0));
        if (!page)
        {
            result.error = PdfError::RenderFailed;
            result.message = "无法载入要渲染的 PDF 页面。";
            return result;
        }
        std::vector<std::uint8_t> bgra(static_cast<std::size_t>(width) * height * 4);
        BitmapHandle bitmap(FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRA, bgra.data(), width * 4));
        if (!bitmap || !FPDFBitmap_FillRect(bitmap.get(), 0, 0, width, height, 0xffffffff))
        {
            result.error = PdfError::RenderFailed;
            result.message = "无法分配 PDF 渲染缓冲区。";
            return result;
        }
        FPDF_RenderPageBitmap(bitmap.get(), page.get(), 0, 0, width, height, 0, FPDF_ANNOT | FPDF_LCD_TEXT);
        result.rgba.resize(bgra.size());
        for (std::size_t index = 0; index < bgra.size(); index += 4)
        {
            result.rgba[index] = bgra[index + 2];
            result.rgba[index + 1] = bgra[index + 1];
            result.rgba[index + 2] = bgra[index];
            result.rgba[index + 3] = bgra[index + 3];
        }
        result.width = width;
        result.height = height;
        return result;
    }

    PdfSaveResult inspect_pdf_destination(const std::string& path)
    {
        PdfSaveResult result;
        if (!is_pdf_path(path))
        {
            result.error = PdfError::UnsupportedType;
            result.message = "请使用 .pdf 扩展名。";
            return result;
        }
        const QString absolute = QDir::cleanPath(QFileInfo(from_utf8(path)).absoluteFilePath());
        result.path = to_utf8(absolute);
        if (!writable_destination(absolute))
        {
            result.error = PdfError::WriteFailed;
            result.message = "此导出位置不可写。";
            return result;
        }
        if (!QFileInfo::exists(absolute))
        {
            result.revision = "missing";
            return result;
        }
        QByteArray bytes;
        if (!read_bytes(absolute, bytes, result.message))
        {
            result.error = PdfError::ReadFailed;
            return result;
        }
        result.revision = revision_of(bytes);
        return result;
    }

    PdfBytesResult serialize_pdf_document(const PdfDocument& document)
    {
        PdfBytesResult result;
        if (!document.editable)
        {
            result.error = PdfError::ReadOnly;
            result.message = "此 PDF 不允许编辑或压缩导出。";
            return result;
        }
        std::lock_guard lock(runtime().mutex);
        PdfError error = PdfError::None;
        DocumentHandle copy = editable_copy(document, true, error, result.message);
        if (!copy)
        {
            result.error = error;
            return result;
        }
        MemoryWriter writer;
        if (!save_to_memory(copy.get(), writer))
        {
            result.error = writer.too_large ? PdfError::TooLarge : PdfError::WriteFailed;
            result.message = "PDF 序列化失败或超过 64 MiB。";
            return result;
        }
        result.bytes = std::move(writer.bytes);
        return result;
    }

    PdfBytesResult compress_pdf_document(
        const PdfDocument& document, int dpi, int quality, const std::function<bool(int, int)>& progress)
    {
        if (!document.editable || dpi < 72 || dpi > 200 || quality < 40 || quality > 95)
            return {PdfError::ReadOnly, "此文档或压缩参数不允许执行。", {}};
        std::lock_guard lock(runtime().mutex);
        PdfBytesResult result;
        PdfError error = PdfError::None;
        auto source = editable_copy(document, true, error, result.message);
        if (!source)
        {
            result.error = error;
            return result;
        }
        DocumentHandle destination(FPDF_CreateNewDocument());
        if (!destination)
            return {PdfError::WriteFailed, "无法创建压缩副本。", {}};
        std::size_t image_bytes = 0;
        const int count = static_cast<int>(document.pages.size());
        for (int index = 0; index < count; ++index)
        {
            if (progress && !progress(index, count))
                return {PdfError::WriteFailed, "压缩已取消。", {}};
            const auto& model = document.pages[index];
            const double width = pdf_page_display_width(model);
            const double height = pdf_page_display_height(model);
            const double scale = (std::min)({dpi / 72.0, 8192.0 / width, 8192.0 / height,
                std::sqrt(static_cast<double>(maximum_pdf_render_pixels) / (width * height))});
            const int pixels_x = (std::max)(1, static_cast<int>(std::floor(width * scale)));
            const int pixels_y = (std::max)(1, static_cast<int>(std::floor(height * scale)));
            QImage image(pixels_x, pixels_y, QImage::Format_ARGB32);
            if (image.isNull())
                return {PdfError::RenderFailed, "压缩页面内存不足。", {}};
            image.fill(Qt::white);
            PageHandle page(FPDF_LoadPage(source.get(), index));
            BitmapHandle bitmap(
                FPDFBitmap_CreateEx(pixels_x, pixels_y, FPDFBitmap_BGRA, image.bits(), image.bytesPerLine()));
            if (!page || !bitmap)
                return {PdfError::RenderFailed, "无法渲染压缩页面。", {}};
            FPDF_RenderPageBitmap(bitmap.get(), page.get(), 0, 0, pixels_x, pixels_y, 0, FPDF_ANNOT);
            QByteArray jpeg;
            QBuffer buffer(&jpeg);
            if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG", quality))
                return {PdfError::WriteFailed, "JPEG 压缩编码失败。", {}};
            image_bytes += static_cast<std::size_t>(jpeg.size());
            if (image_bytes > maximum_pdf_bytes)
                return {PdfError::TooLarge, "压缩结果仍超过 64 MiB，请使用较低质量。", {}};
            FPDF_FILEACCESS access{};
            access.m_FileLen = static_cast<unsigned long>(jpeg.size());
            access.m_Param = &jpeg;
            access.m_GetBlock = [](void* context, unsigned long position, unsigned char* data,
                                    unsigned long size) -> int
            {
                const auto& bytes = *static_cast<QByteArray*>(context);
                if (static_cast<std::uint64_t>(position) + size > static_cast<std::uint64_t>(bytes.size()))
                    return 0;
                std::copy_n(reinterpret_cast<const unsigned char*>(bytes.constData()) + position, size, data);
                return 1;
            };
            PageHandle target(FPDFPage_New(destination.get(), index, width, height));
            auto object = FPDFPageObj_NewImageObj(destination.get());
            if (!target || !object)
            {
                if (object)
                    FPDFPageObj_Destroy(object);
                return {PdfError::WriteFailed, "无法创建压缩页。", {}};
            }
            if (!FPDFImageObj_LoadJpegFileInline(nullptr, 0, object, &access) ||
                !FPDFImageObj_SetMatrix(object, width, 0, 0, height, 0, 0))
            {
                FPDFPageObj_Destroy(object);
                return {PdfError::WriteFailed, "无法写入压缩图片。", {}};
            }
            FPDFPage_InsertObject(target.get(), object);
            if (!FPDFPage_GenerateContent(target.get()))
                return {PdfError::WriteFailed, "无法生成压缩页面内容。", {}};
        }
        if (progress && !progress(count, count))
            return {PdfError::WriteFailed, "压缩已取消。", {}};
        MemoryWriter writer;
        if (!save_to_memory(destination.get(), writer))
            return {PdfError::WriteFailed, "压缩 PDF 写入失败或超过 64 MiB。", {}};
        result.bytes = std::move(writer.bytes);
        return result;
    }

    PdfSaveResult save_pdf_bytes(
        const std::string& path, const std::vector<std::uint8_t>& bytes, const std::string& expected_revision)
    {
        PdfSaveResult result;
        if (!is_pdf_path(path) || bytes.empty() || bytes.size() > maximum_pdf_bytes)
        {
            result.error = PdfError::InvalidDocument;
            result.message = "导出的 PDF 路径或大小无效。";
            return result;
        }
        {
            std::lock_guard lock(runtime().mutex);
            auto check = load_document(bytes);
            if (!check || FPDF_GetPageCount(check.get()) <= 0 ||
                FPDF_GetPageCount(check.get()) > static_cast<int>(maximum_pdf_pages))
            {
                result.error = PdfError::InvalidDocument;
                result.message = "导出的 PDF 无法验证或超过 250 页。";
                return result;
            }
        }
        const QString absolute_path = QDir::cleanPath(QFileInfo(from_utf8(path)).absoluteFilePath());
        result.path = to_utf8(absolute_path);
        if (!writable_destination(absolute_path))
        {
            result.error = PdfError::WriteFailed;
            result.message = "PDF 保存位置不可写，当前修改已保留。";
            return result;
        }
        if (!revision_matches(absolute_path, expected_revision))
        {
            result.error = PdfError::ChangedOnDisk;
            result.message = "文件已被其他程序修改或移走，请另存为。当前修改已保留。";
            return result;
        }
        QSaveFile destination(absolute_path);
        destination.setDirectWriteFallback(false);
        if (!destination.open(QIODevice::WriteOnly) ||
            destination.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()))
        {
            destination.cancelWriting();
            result.error = PdfError::WriteFailed;
            result.message = "PDF 保存失败，当前修改已保留。";
            return result;
        }
        if (!writable_destination(absolute_path) || !revision_matches(absolute_path, expected_revision))
        {
            destination.cancelWriting();
            result.error = PdfError::ChangedOnDisk;
            result.message = "文件已被其他程序修改或移走，请另存为。当前修改已保留。";
            return result;
        }
        if (!destination.commit())
        {
            result.error = PdfError::WriteFailed;
            result.message = "PDF 保存失败，当前修改已保留。";
            return result;
        }
        const QByteArray saved(
            reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
        result.revision = revision_of(saved);
        return result;
    }

    PdfSaveResult save_pdf_file(
        const std::string& path, const PdfDocument& document, const std::string& expected_revision)
    {
        if (!is_pdf_path(path))
            return {PdfError::UnsupportedType, "请使用 .pdf 扩展名。", {}, {}};
        auto serialized = serialize_pdf_document(document);
        if (serialized.error != PdfError::None)
            return {serialized.error, serialized.message, {}, {}};
        return save_pdf_bytes(path, serialized.bytes, expected_revision);
    }
}
