#include <mirrorfly/image_decode.hpp>

#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QSvgRenderer>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace
{
    constexpr std::size_t maximum_svg_bytes = 4 * 1024 * 1024;
    constexpr std::uint64_t maximum_source_pixels = 16 * 1024 * 1024;
    constexpr int maximum_output_side = 2048;
    constexpr int maximum_svg_elements = 10000;

    bool safe_css_references(const QString& text)
    {
        const QString normalized = text.toCaseFolded();
        if (normalized.contains(QStringLiteral("@import")))
        {
            return false;
        }
        qsizetype position = 0;
        while ((position = normalized.indexOf(QStringLiteral("url("), position)) >= 0)
        {
            const qsizetype end = normalized.indexOf(QLatin1Char(')'), position + 4);
            if (end < 0)
            {
                return false;
            }
            QString target = normalized.mid(position + 4, end - position - 4).trimmed();
            if (target.size() >= 2 &&
                ((target.front() == QLatin1Char('\'') && target.back() == QLatin1Char('\'')) ||
                    (target.front() == QLatin1Char('"') && target.back() == QLatin1Char('"'))))
            {
                target = target.mid(1, target.size() - 2).trimmed();
            }
            if (!target.startsWith(QLatin1Char('#')))
            {
                return false;
            }
            position = end + 1;
        }
        return true;
    }

    bool safe_svg(std::string_view bytes)
    {
        if (bytes.empty() || bytes.size() > maximum_svg_bytes)
        {
            return false;
        }
        const QByteArray view = QByteArray::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size()));
        QXmlStreamReader xml(view);
        bool root = false;
        int elements = 0;
        while (!xml.atEnd())
        {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference ||
                token == QXmlStreamReader::ProcessingInstruction)
            {
                return false;
            }
            if (token == QXmlStreamReader::StartElement)
            {
                const QString name = xml.name().toString();
                if (!root)
                {
                    root = true;
                    if (name != QStringLiteral("svg"))
                    {
                        return false;
                    }
                }
                if (++elements > maximum_svg_elements || name == QStringLiteral("script") ||
                    name == QStringLiteral("foreignObject") || name == QStringLiteral("iframe") ||
                    name == QStringLiteral("object"))
                {
                    return false;
                }
                for (const auto& attribute : xml.attributes())
                {
                    const QString value = attribute.value().toString();
                    if ((attribute.name() == QStringLiteral("href") &&
                            !value.trimmed().startsWith(QLatin1Char('#'))) ||
                        !safe_css_references(value))
                    {
                        return false;
                    }
                }
            }
            else if (token == QXmlStreamReader::Characters && !safe_css_references(xml.text().toString()))
            {
                return false;
            }
        }
        return root && !xml.hasError();
    }

    mirrorfly::DecodedRasterImage read_svg(std::string_view bytes, bool pixels)
    {
        if (!safe_svg(bytes))
        {
            return {};
        }
        const QByteArray view = QByteArray::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size()));
        QSvgRenderer renderer;
        renderer.setAnimationEnabled(false);
        if (!renderer.load(view) || !renderer.isValid())
        {
            return {};
        }
        QSize source_size = renderer.defaultSize();
        if (!source_size.isValid())
        {
            const QRectF view_box = renderer.viewBoxF();
            source_size = QSize(static_cast<int>(std::ceil(view_box.width())),
                static_cast<int>(std::ceil(view_box.height())));
        }
        if (!source_size.isValid() || source_size.width() <= 0 || source_size.height() <= 0 ||
            static_cast<std::uint64_t>(source_size.width()) * source_size.height() > maximum_source_pixels)
        {
            return {};
        }
        mirrorfly::DecodedRasterImage result;
        result.source_size = {source_size.width(), source_size.height()};
        if (!pixels)
        {
            return result;
        }
        const double scale = std::min(1.0,
            static_cast<double>(maximum_output_side) / std::max(source_size.width(), source_size.height()));
        const QSize output_size(std::max(1, static_cast<int>(std::lround(source_size.width() * scale))),
            std::max(1, static_cast<int>(std::lround(source_size.height() * scale))));
        QImage image(output_size, QImage::Format_RGBA8888);
        if (image.isNull())
        {
            return {};
        }
        image.fill(Qt::transparent);
        QPainter painter(&image);
        if (!painter.isActive())
        {
            return {};
        }
        renderer.render(&painter, QRectF(QPointF(), QSizeF(output_size)));
        painter.end();
        result.size = {output_size.width(), output_size.height()};
        result.rgba.resize(static_cast<std::size_t>(output_size.width()) * output_size.height() * 4);
        for (int row = 0; row < output_size.height(); ++row)
        {
            std::memcpy(result.rgba.data() + static_cast<std::size_t>(row) * output_size.width() * 4,
                image.constScanLine(row), static_cast<std::size_t>(output_size.width()) * 4);
        }
        return result;
    }
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <gdiplus.h>

namespace
{
    using Microsoft::WRL::ComPtr;
    constexpr std::size_t maximum_input_bytes = 24 * 1024 * 1024;
    constexpr std::size_t maximum_metafile_bytes = 16 * 1024 * 1024;
    constexpr std::uint32_t maximum_metafile_record_bytes = 8 * 1024 * 1024;
    constexpr std::uint32_t maximum_metafile_records = 100000;
    constexpr std::uint16_t maximum_metafile_handles = 4096;

    std::uint16_t little_u16(std::string_view bytes, std::size_t offset)
    {
        return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset])) |
            static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset + 1]) << 8);
    }

    std::int16_t little_i16(std::string_view bytes, std::size_t offset)
    {
        return static_cast<std::int16_t>(little_u16(bytes, offset));
    }

    std::uint32_t little_u32(std::string_view bytes, std::size_t offset)
    {
        return static_cast<std::uint32_t>(little_u16(bytes, offset)) |
            (static_cast<std::uint32_t>(little_u16(bytes, offset + 2)) << 16);
    }

    std::int32_t little_i32(std::string_view bytes, std::size_t offset)
    {
        return static_cast<std::int32_t>(little_u32(bytes, offset));
    }

    bool bounded_dimensions(const mirrorfly::ImageDimensions& size)
    {
        return size.width > 0 && size.height > 0 &&
            static_cast<std::uint64_t>(size.width) * size.height <= maximum_source_pixels;
    }

    int physical_pixels(std::int64_t units, std::int64_t units_per_inch)
    {
        if (units <= 0 || units_per_inch <= 0)
        {
            return 0;
        }
        const double pixels = static_cast<double>(units) * 96.0 / static_cast<double>(units_per_inch);
        if (!std::isfinite(pixels) || pixels > static_cast<double>(INT_MAX))
        {
            return 0;
        }
        return std::max(1, static_cast<int>(std::lround(pixels)));
    }

    bool bounded_large_dib(std::string_view record)
    {
        // Only uncompressed, self-contained RGB bitmaps may exceed the normal record budget.
        if (record.size() < 120 || little_u32(record, 0) != EMR_STRETCHDIBITS ||
            little_u32(record, 64) != DIB_RGB_COLORS)
            return false;
        const auto info_offset = little_u32(record, 48);
        const auto info_size = little_u32(record, 52);
        const auto bits_offset = little_u32(record, 56);
        const auto bits_size = little_u32(record, 60);
        if (info_offset < 80 || info_size < 40 || info_offset > record.size() ||
            info_size > record.size() - info_offset || bits_offset < info_offset + info_size ||
            bits_offset > record.size() || bits_size > record.size() - bits_offset)
            return false;
        const auto info = record.substr(info_offset, info_size);
        const auto width = little_i32(info, 4);
        const auto height = std::abs(static_cast<std::int64_t>(little_i32(info, 8)));
        const auto depth = little_u16(info, 14);
        if (little_u32(info, 0) != 40 || width <= 0 || height <= 0 ||
            static_cast<std::uint64_t>(width) * height > maximum_source_pixels || little_u16(info, 12) != 1 ||
            (depth != 24 && depth != 32) || little_u32(info, 16) != BI_RGB || little_u32(info, 32) != 0)
            return false;
        const auto stride = (static_cast<std::uint64_t>(width) * depth + 31) / 32 * 4;
        return stride * height == bits_size;
    }

    bool validate_emf(std::string_view bytes, mirrorfly::ImageDimensions& dimensions)
    {
        if (bytes.size() < 88 || bytes.size() > maximum_metafile_bytes ||
            little_u32(bytes, 0) != EMR_HEADER || little_u32(bytes, 40) != ENHMETA_SIGNATURE ||
            little_u32(bytes, 48) != bytes.size())
        {
            return false;
        }
        const std::uint32_t header_size = little_u32(bytes, 4);
        const std::uint32_t declared_records = little_u32(bytes, 52);
        if (header_size < 88 || header_size > bytes.size() || header_size % 4 != 0 || declared_records < 2 ||
            declared_records > maximum_metafile_records || little_u16(bytes, 56) > maximum_metafile_handles)
        {
            return false;
        }
        const std::int64_t frame_width =
            std::abs(static_cast<std::int64_t>(little_i32(bytes, 32)) - little_i32(bytes, 24));
        const std::int64_t frame_height =
            std::abs(static_cast<std::int64_t>(little_i32(bytes, 36)) - little_i32(bytes, 28));
        dimensions = {physical_pixels(frame_width, 2540), physical_pixels(frame_height, 2540)};
        if (!bounded_dimensions(dimensions))
        {
            const std::int64_t bounds_width =
                std::abs(static_cast<std::int64_t>(little_i32(bytes, 16)) - little_i32(bytes, 8)) + 1;
            const std::int64_t bounds_height =
                std::abs(static_cast<std::int64_t>(little_i32(bytes, 20)) - little_i32(bytes, 12)) + 1;
            if (bounds_width > INT_MAX || bounds_height > INT_MAX)
            {
                return false;
            }
            dimensions = {static_cast<int>(bounds_width), static_cast<int>(bounds_height)};
            if (!bounded_dimensions(dimensions))
            {
                return false;
            }
        }

        std::size_t offset = 0;
        std::uint32_t records = 0;
        bool eof = false;
        while (offset < bytes.size() && records < maximum_metafile_records)
        {
            if (bytes.size() - offset < 8)
            {
                return false;
            }
            const std::uint32_t type = little_u32(bytes, offset);
            const std::uint32_t record_size = little_u32(bytes, offset + 4);
            if (type < EMR_HEADER || type > EMR_CREATECOLORSPACEW || record_size < 8 ||
                record_size % 4 != 0 || record_size > bytes.size() - offset ||
                (records == 0 && record_size != header_size) || type == EMR_GLSRECORD ||
                type == EMR_GLSBOUNDEDRECORD)
            {
                return false;
            }
            if (record_size > maximum_metafile_record_bytes &&
                !bounded_large_dib(bytes.substr(offset, record_size)))
                return false;
            ++records;
            offset += record_size;
            if (type == EMR_EOF)
            {
                eof = offset == bytes.size();
                break;
            }
        }
        return eof && records == declared_records;
    }

    bool validate_wmf(std::string_view bytes, mirrorfly::ImageDimensions& dimensions)
    {
        if (bytes.size() < 18 || bytes.size() > maximum_metafile_bytes)
        {
            return false;
        }
        std::size_t header_offset = 0;
        if (bytes.size() >= 40 && little_u32(bytes, 0) == 0x9AC6CDD7)
        {
            std::uint16_t checksum = 0;
            for (std::size_t offset = 0; offset < 20; offset += 2)
            {
                checksum ^= little_u16(bytes, offset);
            }
            const std::int64_t width =
                std::abs(static_cast<std::int64_t>(little_i16(bytes, 10)) - little_i16(bytes, 6));
            const std::int64_t height =
                std::abs(static_cast<std::int64_t>(little_i16(bytes, 12)) - little_i16(bytes, 8));
            const std::uint16_t inch = little_u16(bytes, 14);
            if (checksum != little_u16(bytes, 20) || !inch)
            {
                return false;
            }
            dimensions = {physical_pixels(width, inch), physical_pixels(height, inch)};
            if (!bounded_dimensions(dimensions))
            {
                return false;
            }
            header_offset = 22;
        }
        if (bytes.size() - header_offset < 18)
        {
            return false;
        }
        const std::uint16_t type = little_u16(bytes, header_offset);
        const std::uint16_t header_words = little_u16(bytes, header_offset + 2);
        const std::uint16_t version = little_u16(bytes, header_offset + 4);
        const std::uint32_t total_words = little_u32(bytes, header_offset + 6);
        const std::uint16_t objects = little_u16(bytes, header_offset + 10);
        const std::uint32_t declared_largest_record = little_u32(bytes, header_offset + 12);
        if ((type != 1 && type != 2) || header_words != 9 || (version != 0x0100 && version != 0x0300) ||
            total_words * 2ULL != bytes.size() - header_offset || objects > maximum_metafile_handles ||
            declared_largest_record < 3 || declared_largest_record * 2ULL > maximum_metafile_record_bytes ||
            little_u16(bytes, header_offset + 16) != 0)
        {
            return false;
        }
        std::size_t offset = header_offset + 18;
        std::uint32_t records = 0;
        std::uint32_t largest_record = 0;
        bool eof = false;
        while (offset < bytes.size() && records < maximum_metafile_records)
        {
            if (bytes.size() - offset < 6)
            {
                return false;
            }
            const std::uint32_t record_words = little_u32(bytes, offset);
            const std::uint16_t function = little_u16(bytes, offset + 4);
            const std::uint64_t record_bytes = static_cast<std::uint64_t>(record_words) * 2;
            if (record_words < 3 || record_bytes > maximum_metafile_record_bytes ||
                record_bytes > bytes.size() - offset)
            {
                return false;
            }
            if (function == META_ESCAPE &&
                (record_bytes < 10 || little_u16(bytes, offset + 6) != MFCOMMENT ||
                    (static_cast<std::uint32_t>(little_u16(bytes, offset + 8)) + 1) / 2 * 2 !=
                        record_bytes - 10))
                return false;
            largest_record = std::max(largest_record, record_words);
            ++records;
            offset += static_cast<std::size_t>(record_bytes);
            if (function == 0)
            {
                eof = record_words == 3 && offset == bytes.size();
                break;
            }
        }
        return eof && records <= maximum_metafile_records && declared_largest_record >= largest_record;
    }

    std::string strip_wmf_comments(std::string_view bytes)
    {
        // Validated metadata comments are not forwarded to the native metafile interpreter.
        const std::size_t header = little_u32(bytes, 0) == 0x9AC6CDD7 ? 22 : 0;
        std::string result(bytes.substr(0, header + 18));
        std::uint32_t largest = 3;
        for (std::size_t offset = header + 18; offset < bytes.size();)
        {
            const auto words = little_u32(bytes, offset);
            if (little_u16(bytes, offset + 4) != META_ESCAPE)
            {
                result.append(bytes.substr(offset, words * 2ULL));
                largest = std::max(largest, words);
            }
            offset += words * 2ULL;
        }
        const auto store = [&](std::size_t offset, std::uint32_t value)
        {
            for (unsigned int byte = 0; byte < 4; ++byte)
                result[offset + byte] = static_cast<char>((value >> (byte * 8)) & 0xff);
        };
        store(header + 6, static_cast<std::uint32_t>((result.size() - header) / 2));
        store(header + 12, largest);
        return result;
    }

    struct ComScope
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~ComScope()
        {
            if (SUCCEEDED(result))
                CoUninitialize();
        }
    };

    struct GdiplusScope
    {
        ULONG_PTR token = 0;
        Gdiplus::Status status = Gdiplus::GenericError;

        GdiplusScope()
        {
            Gdiplus::GdiplusStartupInput input;
            status = Gdiplus::GdiplusStartup(&token, &input, nullptr);
        }

        ~GdiplusScope()
        {
            if (status == Gdiplus::Ok)
            {
                Gdiplus::GdiplusShutdown(token);
            }
        }
    };

    mirrorfly::DecodedRasterImage read_metafile(
        std::string_view bytes, mirrorfly::MetafileFormat format, bool pixels)
    {
        mirrorfly::ImageDimensions declared_size;
        const bool valid = format == mirrorfly::MetafileFormat::Emf ? validate_emf(bytes, declared_size)
                                                                    : validate_wmf(bytes, declared_size);
        if (!valid)
        {
            return {};
        }
        const auto sanitized =
            format == mirrorfly::MetafileFormat::Wmf ? strip_wmf_comments(bytes) : std::string{};
        if (!sanitized.empty())
            bytes = sanitized;
        ComScope com;
        if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
        {
            return {};
        }
        static GdiplusScope gdiplus;
        if (gdiplus.status != Gdiplus::Ok)
        {
            return {};
        }
        ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
        {
            return {};
        }
        ULONG written = 0;
        LARGE_INTEGER start{};
        if (FAILED(stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written)) ||
            written != bytes.size() || FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)))
        {
            return {};
        }
        Gdiplus::Metafile metafile(stream.Get());
        const UINT width = metafile.GetWidth();
        const UINT height = metafile.GetHeight();
        if (metafile.GetLastStatus() != Gdiplus::Ok || !width || !height || width > INT_MAX ||
            height > INT_MAX || static_cast<std::uint64_t>(width) * height > maximum_source_pixels)
        {
            return {};
        }
        mirrorfly::DecodedRasterImage result;
        result.source_size = {static_cast<int>(width), static_cast<int>(height)};
        if (!pixels)
        {
            return result;
        }
        const double scale =
            std::min(1.0, static_cast<double>(maximum_output_side) / std::max(width, height));
        const int output_width = std::max(1, static_cast<int>(std::lround(width * scale)));
        const int output_height = std::max(1, static_cast<int>(std::lround(height * scale)));
        QImage image(output_width, output_height, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
        {
            return {};
        }
        Gdiplus::Bitmap target(
            output_width, output_height, image.bytesPerLine(), PixelFormat32bppPARGB, image.bits());
        if (target.GetLastStatus() != Gdiplus::Ok)
        {
            return {};
        }
        Gdiplus::Graphics graphics(&target);
        if (graphics.GetLastStatus() != Gdiplus::Ok ||
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy) != Gdiplus::Ok ||
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0)) != Gdiplus::Ok ||
            graphics.DrawImage(&metafile, Gdiplus::Rect(0, 0, output_width, output_height)) != Gdiplus::Ok)
        {
            return {};
        }
        const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.isNull())
        {
            return {};
        }
        result.size = {output_width, output_height};
        result.rgba.resize(static_cast<std::size_t>(output_width) * output_height * 4);
        for (int row = 0; row < output_height; ++row)
        {
            std::memcpy(result.rgba.data() + static_cast<std::size_t>(row) * output_width * 4,
                rgba.constScanLine(row), static_cast<std::size_t>(output_width) * 4);
        }
        return result;
    }

    WICBitmapTransformOptions orientation(IWICBitmapFrameDecode* frame)
    {
        ComPtr<IWICMetadataQueryReader> metadata;
        PROPVARIANT value{};
        unsigned int tag = 1;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata)) &&
            SUCCEEDED(metadata->GetMetadataByName(L"/ifd/{ushort=274}", &value)))
        {
            if (value.vt == VT_UI2)
                tag = value.uiVal;
            else if (value.vt == VT_UI4)
                tag = value.ulVal;
        }
        PropVariantClear(&value);
        switch (tag)
        {
        case 2:
            return WICBitmapTransformFlipHorizontal;
        case 3:
            return WICBitmapTransformRotate180;
        case 4:
            return WICBitmapTransformFlipVertical;
        case 5:
            return static_cast<WICBitmapTransformOptions>(
                WICBitmapTransformRotate90 | WICBitmapTransformFlipVertical);
        case 6:
            return WICBitmapTransformRotate90;
        case 7:
            return static_cast<WICBitmapTransformOptions>(
                WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        case 8:
            return WICBitmapTransformRotate270;
        default:
            return WICBitmapTransformRotate0;
        }
    }

    mirrorfly::DecodedRasterImage read_tiff(std::string_view bytes, bool pixels)
    {
        if (bytes.size() < 8 || bytes.size() > maximum_input_bytes ||
            (bytes.substr(0, 4) != std::string_view("II\x2a\0", 4) &&
                bytes.substr(0, 4) != std::string_view("MM\0\x2a", 4)))
            return {};
        ComScope com;
        if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
            return {};
        std::string owned(bytes);
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(
                reinterpret_cast<BYTE*>(owned.data()), static_cast<DWORD>(owned.size()))) ||
            FAILED(CoCreateInstance(
                CLSID_WICTiffDecoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder))) ||
            FAILED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) ||
            FAILED(decoder->GetFrame(0, &frame)))
            return {};
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
            static_cast<std::uint64_t>(width) * height > maximum_source_pixels)
            return {};
        ComPtr<IWICBitmapSource> source = frame;
        ComPtr<IWICBitmapFlipRotator> rotated;
        const auto transform = orientation(frame.Get());
        if (transform != WICBitmapTransformRotate0)
        {
            if (FAILED(factory->CreateBitmapFlipRotator(&rotated)) ||
                FAILED(rotated->Initialize(source.Get(), transform)) ||
                FAILED(rotated->GetSize(&width, &height)))
                return {};
            source = rotated;
        }
        mirrorfly::DecodedRasterImage result;
        result.source_size = {static_cast<int>(width), static_cast<int>(height)};
        if (!pixels)
            return result;
        const double scale = std::min(1.0, 2048.0 / std::max(width, height));
        const UINT output_width = std::max(1U, static_cast<UINT>(std::lround(width * scale)));
        const UINT output_height = std::max(1U, static_cast<UINT>(std::lround(height * scale)));
        ComPtr<IWICBitmapScaler> scaled;
        if (output_width != width || output_height != height)
        {
            if (FAILED(factory->CreateBitmapScaler(&scaled)) ||
                FAILED(scaled->Initialize(
                    source.Get(), output_width, output_height, WICBitmapInterpolationModeFant)))
                return {};
            source = scaled;
        }
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                nullptr, 0, WICBitmapPaletteTypeCustom)))
            return {};
        result.rgba.resize(static_cast<std::size_t>(output_width) * output_height * 4);
        if (FAILED(converter->CopyPixels(
                nullptr, output_width * 4, static_cast<UINT>(result.rgba.size()), result.rgba.data())))
            return {};
        result.size = {static_cast<int>(output_width), static_cast<int>(output_height)};
        return result;
    }

    mirrorfly::DecodedRasterImage read_wdp(std::string_view bytes, bool pixels, int requested_side)
    {
        if (bytes.size() < 8 || bytes.size() > maximum_input_bytes ||
            (bytes.substr(0, 4) != std::string_view("II\xbc\x01", 4) &&
                bytes.substr(0, 4) != std::string_view("II\xbc\x00", 4)))
        {
            return {};
        }
        ComScope com;
        if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
        {
            return {};
        }
        std::string owned(bytes);
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
            FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(
                reinterpret_cast<BYTE*>(owned.data()), static_cast<DWORD>(owned.size()))) ||
            FAILED(CoCreateInstance(
                CLSID_WICWmpDecoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder))) ||
            FAILED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) ||
            FAILED(decoder->GetFrame(0, &frame)))
        {
            return {};
        }
        UINT width = 0;
        UINT height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
            static_cast<std::uint64_t>(width) * height > maximum_source_pixels)
        {
            return {};
        }
        mirrorfly::DecodedRasterImage result;
        result.source_size = {static_cast<int>(width), static_cast<int>(height)};
        if (!pixels)
        {
            return result;
        }
        const int maximum_side = std::clamp(requested_side, 256, maximum_output_side);
        const double scale = std::min(1.0, static_cast<double>(maximum_side) / std::max(width, height));
        const UINT output_width = std::max(1U, static_cast<UINT>(std::lround(width * scale)));
        const UINT output_height = std::max(1U, static_cast<UINT>(std::lround(height * scale)));
        ComPtr<IWICBitmapSource> source = frame;
        ComPtr<IWICBitmapScaler> scaled;
        if (output_width != width || output_height != height)
        {
            if (FAILED(factory->CreateBitmapScaler(&scaled)) ||
                FAILED(scaled->Initialize(
                    source.Get(), output_width, output_height, WICBitmapInterpolationModeFant)))
            {
                return {};
            }
            source = scaled;
        }
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                nullptr, 0, WICBitmapPaletteTypeCustom)))
        {
            return {};
        }
        result.rgba.resize(static_cast<std::size_t>(output_width) * output_height * 4);
        if (FAILED(converter->CopyPixels(
                nullptr, output_width * 4, static_cast<UINT>(result.rgba.size()), result.rgba.data())))
        {
            return {};
        }
        result.size = {static_cast<int>(output_width), static_cast<int>(output_height)};
        return result;
    }
}
#endif

namespace mirrorfly
{
    ImageDimensions inspect_tiff_image(std::string_view bytes)
    {
#ifdef _WIN32
        return read_tiff(bytes, false).source_size;
#else
        static_cast<void>(bytes);
        return {};
#endif
    }

    DecodedRasterImage decode_tiff_image(std::string_view bytes)
    {
#ifdef _WIN32
        return read_tiff(bytes, true);
#else
        static_cast<void>(bytes);
        return {};
#endif
    }

    ImageDimensions inspect_wdp_image(std::string_view bytes)
    {
#ifdef _WIN32
        return read_wdp(bytes, false, maximum_output_side).source_size;
#else
        static_cast<void>(bytes);
        return {};
#endif
    }

    DecodedRasterImage decode_wdp_image(std::string_view bytes, int maximum_side)
    {
#ifdef _WIN32
        return read_wdp(bytes, true, maximum_side);
#else
        static_cast<void>(bytes);
        static_cast<void>(maximum_side);
        return {};
#endif
    }

    ImageDimensions inspect_svg_image(std::string_view bytes)
    {
        return read_svg(bytes, false).source_size;
    }

    DecodedRasterImage decode_svg_image(std::string_view bytes)
    {
        return read_svg(bytes, true);
    }

    ImageDimensions inspect_metafile_image(std::string_view bytes, MetafileFormat format)
    {
#ifdef _WIN32
        return read_metafile(bytes, format, false).source_size;
#else
        static_cast<void>(bytes);
        static_cast<void>(format);
        return {};
#endif
    }

    DecodedRasterImage decode_metafile_image(std::string_view bytes, MetafileFormat format)
    {
#ifdef _WIN32
        return read_metafile(bytes, format, true);
#else
        static_cast<void>(bytes);
        static_cast<void>(format);
        return {};
#endif
    }
}
