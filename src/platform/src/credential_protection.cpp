#include <mirrorfly/credential_protection.hpp>

#ifdef _WIN32
#include <windows.h>

#include <dpapi.h>
#endif

namespace
{
    mirrorfly::ProtectedCredentialResult transform(std::string_view bytes, bool protect)
    {
        if (bytes.empty() || bytes.size() > 65536)
            return {};
#ifdef _WIN32
        DATA_BLOB input{
            static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()))};
        DATA_BLOB output{};
        BOOL ok = FALSE;
        if (protect)
            ok = CryptProtectData(&input, L"Mirrorfly Office AI", nullptr, nullptr, nullptr,
                CRYPTPROTECT_UI_FORBIDDEN, &output);
        else
            ok = CryptUnprotectData(
                &input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
        if (!ok)
            return {};
        mirrorfly::ProtectedCredentialResult result{
            true, std::string(reinterpret_cast<const char*>(output.pbData), output.cbData)};
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
        return result;
#else
        (void)protect;
        return {};
#endif
    }
}

namespace mirrorfly
{
    ProtectedCredentialResult protect_current_user_credential(std::string_view bytes)
    {
        return transform(bytes, true);
    }

    ProtectedCredentialResult unprotect_current_user_credential(std::string_view bytes)
    {
        return transform(bytes, false);
    }
}
