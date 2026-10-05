#pragma once

#include <string>
#include <string_view>

namespace mirrorfly
{
    struct ProtectedCredentialResult
    {
        bool ok = false;
        std::string bytes;
    };

    ProtectedCredentialResult protect_current_user_credential(std::string_view bytes);
    ProtectedCredentialResult unprotect_current_user_credential(std::string_view bytes);
}
