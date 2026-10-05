#pragma once

#include <mirrorfly/office_package.hpp>
#include <mirrorfly/office_progress.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace mirrorfly::archive_storage
{
    enum class ReadStage
    {
        Reading,
        Validating,
        Extracting
    };

    using ReadProgress = std::function<void(ReadStage stage, std::size_t completed, std::size_t total)>;

    struct Limits
    {
        std::size_t archive_bytes = 0;
        std::size_t expanded_bytes = 0;
        std::size_t part_bytes = 0;
        std::size_t xml_bytes = 0;
        std::size_t parts = 0;
        std::size_t path_bytes = 1024;
    };

    enum class Error
    {
        None,
        ReadFailed,
        TooLarge,
        InvalidArchive,
        EncryptedArchive,
        InvalidPackage,
        WriteFailed,
        ChangedOnDisk
    };

    struct Result
    {
        Error error = Error::None;
        std::string message;
        std::vector<OfficePart> parts;
        std::string source_bytes;
        std::string revision;
    };

    Result read(const std::string& path, const Limits& limits, const ReadProgress& progress = {});
    Result write(const std::string& path, const std::vector<OfficePart>& parts,
        const std::string& expected_revision, const Limits& limits, const OfficeSaveProgress& progress = {});
    Result write_bytes(const std::string& path, const std::string& bytes,
        const std::string& expected_revision, const Limits& limits);
}
