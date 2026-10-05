#pragma once

#include <memory>
#include <string_view>

namespace mirrorfly
{
    enum class EmbeddedFontPermission
    {
        Unavailable,
        PreviewPrint,
        Editable,
        Installable
    };

    struct EmbeddedFontLoadResult
    {
        bool loaded = false;
        EmbeddedFontPermission permission = EmbeddedFontPermission::Unavailable;
    };

    class EmbeddedFontSession
    {
    public:
        EmbeddedFontSession();
        ~EmbeddedFontSession();
        EmbeddedFontSession(const EmbeddedFontSession&) = delete;
        EmbeddedFontSession& operator=(const EmbeddedFontSession&) = delete;

        EmbeddedFontLoadResult load(std::string_view bytes);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
