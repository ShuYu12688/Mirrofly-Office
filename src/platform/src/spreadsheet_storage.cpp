#include <mirrorfly/spreadsheet_storage.hpp>

#include "archive_storage.hpp"

namespace
{
    mirrorfly::archive_storage::Limits limits()
    {
        using namespace mirrorfly;
        return {maximum_spreadsheet_archive_bytes, maximum_spreadsheet_expanded_bytes,
            maximum_spreadsheet_part_bytes, maximum_spreadsheet_xml_bytes, maximum_spreadsheet_parts,
            maximum_spreadsheet_path_bytes};
    }

    mirrorfly::SpreadsheetError spreadsheet_error(mirrorfly::archive_storage::Error error)
    {
        using ArchiveError = mirrorfly::archive_storage::Error;
        using Error = mirrorfly::SpreadsheetError;
        switch (error)
        {
        case ArchiveError::None:
            return Error::None;
        case ArchiveError::ReadFailed:
            return Error::ReadFailed;
        case ArchiveError::TooLarge:
            return Error::TooLarge;
        case ArchiveError::InvalidArchive:
            return Error::InvalidArchive;
        case ArchiveError::EncryptedArchive:
            return Error::EncryptedArchive;
        case ArchiveError::InvalidPackage:
            return Error::InvalidPackage;
        case ArchiveError::WriteFailed:
            return Error::WriteFailed;
        case ArchiveError::ChangedOnDisk:
            return Error::ChangedOnDisk;
        }
        return Error::InvalidPackage;
    }

    mirrorfly::SpreadsheetResult load_failure(
        mirrorfly::SpreadsheetError error, const std::string& message = {})
    {
        mirrorfly::SpreadsheetResult result;
        result.error = error;
        result.message = message;
        return result;
    }

    mirrorfly::SpreadsheetSaveResult save_failure(
        mirrorfly::SpreadsheetError error, const std::string& message = {})
    {
        mirrorfly::SpreadsheetSaveResult result;
        result.error = error;
        result.message = message;
        return result;
    }
}

namespace mirrorfly
{
    SpreadsheetResult load_spreadsheet_file(const std::string& path)
    {
        if (!is_spreadsheet_path(path))
        {
            return load_failure(SpreadsheetError::UnsupportedType);
        }
        auto archive = archive_storage::read(path, limits());
        if (archive.error != archive_storage::Error::None)
        {
            return load_failure(spreadsheet_error(archive.error), archive.message);
        }
        auto result = parse_spreadsheet(std::move(archive.parts));
        if (result.error == SpreadsheetError::None)
        {
            result.path = path;
            result.revision = archive.revision;
        }
        return result;
    }

    SpreadsheetSaveResult save_spreadsheet_file(
        const std::string& path, const std::vector<OfficePart>& parts, const std::string& expected_revision)
    {
        if (!is_spreadsheet_path(path))
        {
            return save_failure(SpreadsheetError::UnsupportedType);
        }
        const auto archive = archive_storage::write(path, parts, expected_revision, limits());
        if (archive.error != archive_storage::Error::None)
        {
            return save_failure(spreadsheet_error(archive.error), archive.message);
        }
        SpreadsheetSaveResult result;
        result.path = path;
        result.revision = archive.revision;
        return result;
    }
}
