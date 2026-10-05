#include <mirrorfly/embedded_font.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off: t2embapi.h requires Windows scalar types to be declared first.
#include <windows.h>
#include <t2embapi.h>
// clang-format on
#endif

namespace mirrorfly
{
    struct EmbeddedFontSession::Impl
    {
#ifdef _WIN32
        std::vector<HANDLE> fonts;
#endif
    };

    EmbeddedFontSession::EmbeddedFontSession() : impl_(std::make_unique<Impl>())
    {
    }

    EmbeddedFontSession::~EmbeddedFontSession()
    {
#ifdef _WIN32
        for (const auto font : impl_->fonts)
        {
            ULONG status = 0;
            TTDeleteEmbeddedFont(font, 0, &status);
        }
#endif
    }

#ifdef _WIN32
    namespace
    {
        constexpr std::size_t maximum_embedded_font_bytes = 32 * 1024 * 1024;

        struct FontStream
        {
            std::string_view bytes;
            std::size_t offset = 0;
        };

        unsigned long WINAPIV read_font_stream(void* context, void* output, const unsigned long count)
        {
            auto& stream = *static_cast<FontStream*>(context);
            const auto remaining = stream.bytes.size() - std::min(stream.offset, stream.bytes.size());
            const auto copied = std::min<std::size_t>(remaining, count);
            if (copied > 0)
            {
                std::memcpy(output, stream.bytes.data() + stream.offset, copied);
                stream.offset += copied;
            }
            return static_cast<unsigned long>(copied);
        }

        EmbeddedFontPermission permission(ULONG value)
        {
            switch (value)
            {
            case EMBED_PREVIEWPRINT:
                return EmbeddedFontPermission::PreviewPrint;
            case EMBED_EDITABLE:
                return EmbeddedFontPermission::Editable;
            case EMBED_INSTALLABLE:
                return EmbeddedFontPermission::Installable;
            default:
                return EmbeddedFontPermission::Unavailable;
            }
        }
    }
#endif

    EmbeddedFontLoadResult EmbeddedFontSession::load(std::string_view bytes)
    {
#ifdef _WIN32
        if (bytes.empty() || bytes.size() > maximum_embedded_font_bytes)
        {
            return {};
        }
        FontStream stream{bytes};
        HANDLE font = nullptr;
        ULONG privilege = 0;
        ULONG status = 0;
        const LONG error = TTLoadEmbeddedFont(&font, TTLOAD_PRIVATE, &privilege, LICENSE_DEFAULT, &status,
            read_font_stream, &stream, nullptr, nullptr, nullptr);
        const auto granted = permission(privilege);
        if (error != E_NONE || !font || granted == EmbeddedFontPermission::Unavailable)
        {
            if (font)
            {
                TTDeleteEmbeddedFont(font, 0, &status);
            }
            return {};
        }
        impl_->fonts.push_back(font);
        return {true, granted};
#else
        static_cast<void>(bytes);
        return {};
#endif
    }
}
