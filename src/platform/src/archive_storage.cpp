#include "archive_storage.hpp"

#include <miniz.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <system_error>
#include <thread>
#include <utility>

namespace
{
    struct ArchiveReader
    {
        mz_zip_archive archive{};

        ~ArchiveReader()
        {
            mz_zip_reader_end(&archive);
        }
    };

    struct ArchiveWriter
    {
        mz_zip_archive archive{};
        bool initialized = false;

        ~ArchiveWriter()
        {
            if (initialized)
            {
                mz_zip_writer_end(&archive);
            }
        }
    };

    struct ArchiveEntry
    {
        mz_uint index = 0;
        std::string path;
        std::size_t size = 0;
    };

    struct ArchiveOutput
    {
        QSaveFile* file = nullptr;
        QCryptographicHash hash{QCryptographicHash::Sha256};
        std::size_t limit = 0;
        std::size_t written = 0;
        bool failed = false;
        bool too_large = false;
    };

    mirrorfly::archive_storage::Result failure(
        mirrorfly::archive_storage::Error error, const std::string& message = {})
    {
        mirrorfly::archive_storage::Result result;
        result.error = error;
        result.message = message;
        return result;
    }

    void report_progress(const mirrorfly::archive_storage::ReadProgress& progress,
        mirrorfly::archive_storage::ReadStage stage, std::size_t completed, std::size_t total) noexcept
    {
        if (!progress)
        {
            return;
        }
        try
        {
            progress(stage, completed, total);
        }
        catch (...)
        {
            // Diagnostics must not change archive validity or persistence behavior.
        }
    }

    void report_save(const mirrorfly::OfficeSaveProgress& progress, mirrorfly::OfficeSaveStage stage,
        std::size_t completed, std::size_t total) noexcept
    {
        try
        {
            if (progress)
                progress(stage, completed, total);
        }
        catch (...)
        {
            // An observer must not interrupt an atomic save.
        }
    }

    std::string ascii_lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
        {
            return static_cast<char>(character >= 'A' && character <= 'Z' ? character + 32 : character);
        });
        return text;
    }

    bool valid_part_path(const std::string& path, std::size_t maximum)
    {
        if (path.empty() || path.size() > maximum || path.front() == '/' ||
            path.find('\\') != std::string::npos || path.find(':') != std::string::npos ||
            path.find('?') != std::string::npos || path.find('#') != std::string::npos)
        {
            return false;
        }
        std::size_t start = 0;
        while (start < path.size())
        {
            const auto end = path.find('/', start);
            const auto component = path.substr(start, end - start);
            if (component.empty() || component == "." || component == "..")
            {
                return false;
            }
            for (const unsigned char character : component)
            {
                if (character < 32 || character == 127)
                {
                    return false;
                }
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        return true;
    }

    bool xml_part(const std::string& path)
    {
        const auto lower = ascii_lower(path);
        return (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".xml") == 0) ||
            (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".rels") == 0);
    }

    std::string revision(const char* bytes, std::size_t size)
    {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayView(bytes, static_cast<qsizetype>(size)));
        const auto digest = hash.result().toHex();
        return std::string(digest.constData(), static_cast<std::size_t>(digest.size()));
    }

    std::string revision(const QByteArray& bytes)
    {
        return revision(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }

    std::optional<std::string> file_revision(const QString& path, std::size_t limit)
    {
        const QFileInfo information(path);
        if (!information.isFile() || information.isSymLink() || information.isJunction() ||
            information.size() > static_cast<qint64>(limit))
        {
            return std::nullopt;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.isSequential())
        {
            return std::nullopt;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QByteArray buffer(1024 * 1024, Qt::Uninitialized);
        qint64 total = 0;
        while (!file.atEnd())
        {
            const qint64 read = file.read(buffer.data(), buffer.size());
            if (read <= 0)
            {
                return std::nullopt;
            }
            total += read;
            if (total > static_cast<qint64>(limit))
            {
                return std::nullopt;
            }
            hash.addData(QByteArrayView(buffer.constData(), read));
        }
        if (file.error() != QFileDevice::NoError || total != information.size())
        {
            return std::nullopt;
        }
        const auto digest = hash.result().toHex();
        return std::string(digest.constData(), static_cast<std::size_t>(digest.size()));
    }

    bool unchanged(const QString& path, const std::string& expected_revision, std::size_t limit)
    {
        if (expected_revision == "missing")
        {
            const QFileInfo target(path);
            return !target.exists() && !target.isSymLink() && !target.isJunction();
        }
        const auto current = file_revision(path, limit);
        return current && *current == expected_revision;
    }

    mirrorfly::archive_storage::Result commit(const std::string& path, const QByteArray& bytes,
        const std::string& expected_revision, const mirrorfly::archive_storage::Limits& limits)
    {
        using namespace mirrorfly::archive_storage;
        if (bytes.isEmpty() || bytes.size() > static_cast<qint64>(limits.archive_bytes))
        {
            return failure(Error::TooLarge, "写入内容超过压缩包限制。");
        }
        const QString filename = QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
        const QFileInfo target(filename);
        if ((target.exists() && (!target.isFile() || target.isSymLink() || target.isJunction())) ||
            !QFileInfo(target.absolutePath()).isDir())
        {
            return failure(Error::WriteFailed, "保存位置无效。");
        }
        if (!expected_revision.empty() && !unchanged(filename, expected_revision, limits.archive_bytes))
        {
            return failure(Error::ChangedOnDisk, "文件已被外部修改。");
        }
        QSaveFile file(filename);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        {
            file.cancelWriting();
            return failure(Error::WriteFailed, "无法写入临时文件。");
        }
        if (!expected_revision.empty() && !unchanged(filename, expected_revision, limits.archive_bytes))
        {
            file.cancelWriting();
            return failure(Error::ChangedOnDisk, "文件已被外部修改。");
        }
        if (!file.commit())
        {
            return failure(Error::WriteFailed, "无法提交原子保存。");
        }
        Result result;
        result.revision = revision(bytes);
        return result;
    }

    size_t write_archive_bytes(void* opaque, mz_uint64 offset, const void* bytes, size_t size)
    {
        auto& output = *static_cast<ArchiveOutput*>(opaque);
        if (output.failed || offset != output.written ||
            size > output.limit - std::min(output.written, output.limit))
        {
            output.failed = true;
            output.too_large = true;
            return 0;
        }
        const auto written = output.file->write(static_cast<const char*>(bytes), static_cast<qint64>(size));
        if (written != static_cast<qint64>(size))
        {
            output.failed = true;
            return 0;
        }
        output.hash.addData(QByteArrayView(static_cast<const char*>(bytes), static_cast<qsizetype>(size)));
        output.written += size;
        return size;
    }

    mirrorfly::archive_storage::Result write_archive(const std::string& path,
        const std::vector<mirrorfly::OfficePart>& parts, const std::string& expected_revision,
        const mirrorfly::archive_storage::Limits& limits, const mirrorfly::OfficeSaveProgress& progress)
    {
        using namespace mirrorfly::archive_storage;
        const QString filename = QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
        const QFileInfo target(filename);
        if ((target.exists() && (!target.isFile() || target.isSymLink() || target.isJunction())) ||
            !QFileInfo(target.absolutePath()).isDir())
        {
            return failure(Error::WriteFailed, "保存位置无效。");
        }
        if (!expected_revision.empty() && !unchanged(filename, expected_revision, limits.archive_bytes))
        {
            return failure(Error::ChangedOnDisk, "文件已被外部修改。");
        }
        QSaveFile file(filename);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly))
        {
            return failure(Error::WriteFailed, "无法写入临时文件。");
        }
        ArchiveOutput output;
        output.file = &file;
        output.limit = limits.archive_bytes;
        ArchiveWriter writer;
        writer.archive.m_pWrite = write_archive_bytes;
        writer.archive.m_pIO_opaque = &output;
        if (!mz_zip_writer_init(&writer.archive, 0))
        {
            file.cancelWriting();
            return failure(Error::WriteFailed, "无法创建 ZIP 压缩包。");
        }
        writer.initialized = true;
        std::size_t completed = 0;
        report_save(progress, mirrorfly::OfficeSaveStage::Writing, 0, parts.size());
        for (const auto& part : parts)
        {
            if (!mz_zip_writer_add_mem(
                    &writer.archive, part.path.c_str(), part.bytes.data(), part.bytes.size(), MZ_BEST_SPEED))
            {
                file.cancelWriting();
                return failure(output.too_large ? Error::TooLarge : Error::WriteFailed,
                    output.too_large ? "写入内容超过压缩包限制。" : "无法生成 ZIP 压缩包。");
            }
            report_save(progress, mirrorfly::OfficeSaveStage::Writing, ++completed, parts.size());
        }
        if (!mz_zip_writer_finalize_archive(&writer.archive) || output.failed || output.written == 0)
        {
            file.cancelWriting();
            return failure(output.too_large ? Error::TooLarge : Error::WriteFailed,
                output.too_large ? "写入内容超过压缩包限制。" : "无法完成 ZIP 压缩包。");
        }
        if (!mz_zip_writer_end(&writer.archive))
        {
            file.cancelWriting();
            return failure(Error::WriteFailed, "无法关闭 ZIP 压缩包。");
        }
        writer.initialized = false;
        if (!expected_revision.empty() && !unchanged(filename, expected_revision, limits.archive_bytes))
        {
            file.cancelWriting();
            return failure(Error::ChangedOnDisk, "文件已被外部修改。");
        }
        const auto digest = output.hash.result().toHex();
        report_save(progress, mirrorfly::OfficeSaveStage::Committing, 0, 1);
        if (!file.commit())
        {
            return failure(Error::WriteFailed, "无法提交原子保存。");
        }
        Result result;
        result.revision.assign(digest.constData(), static_cast<std::size_t>(digest.size()));
        report_save(progress, mirrorfly::OfficeSaveStage::Committing, 1, 1);
        return result;
    }

    mirrorfly::archive_storage::Result validate_parts(
        const std::vector<mirrorfly::OfficePart>& parts, const mirrorfly::archive_storage::Limits& limits)
    {
        using namespace mirrorfly::archive_storage;
        if (parts.empty())
        {
            return failure(Error::InvalidPackage, "保存包没有内容。");
        }
        if (parts.size() > limits.parts)
        {
            return failure(Error::TooLarge, "保存包条目数量超过限制。");
        }
        std::set<std::string> names;
        std::size_t expanded = 0;
        for (const auto& part : parts)
        {
            const auto part_limit = xml_part(part.path) ? limits.xml_bytes : limits.part_bytes;
            if (part.bytes.size() > part_limit ||
                part.bytes.size() > limits.expanded_bytes - std::min(expanded, limits.expanded_bytes))
            {
                return failure(Error::TooLarge, "保存包内容超过大小限制。");
            }
            if (!valid_part_path(part.path, limits.path_bytes) ||
                !names.insert(ascii_lower(part.path)).second)
            {
                return failure(Error::InvalidPackage, "保存包包含重复或越界路径。");
            }
            expanded += part.bytes.size();
        }
        return {};
    }
}

namespace mirrorfly::archive_storage
{
    Result read(const std::string& path, const Limits& limits, const ReadProgress& progress)
    {
        try
        {
            const QString filename = QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
            const QFileInfo information(filename);
            if (!information.isFile() || information.isSymLink() || information.isJunction())
            {
                return failure(Error::ReadFailed, "文件不存在或不是普通文件。");
            }
            if (information.size() <= 0 || information.size() > static_cast<qint64>(limits.archive_bytes))
            {
                return failure(Error::TooLarge, "压缩包超过大小限制。");
            }
            QFile file(filename);
            if (!file.open(QIODevice::ReadOnly) || file.isSequential())
            {
                return failure(Error::ReadFailed, "无法读取文件。");
            }
            const auto file_size = static_cast<std::size_t>(information.size());
            uchar* mapping = file.map(0, information.size());
            QByteArray fallback;
            if (!mapping)
            {
                file.seek(0);
                fallback = file.read(information.size() + 1);
                if (fallback.size() != information.size() || file.error() != QFileDevice::NoError)
                {
                    return failure(Error::ReadFailed, "读取文件失败。");
                }
            }
            const char* bytes = mapping ? reinterpret_cast<const char*>(mapping) : fallback.constData();
            report_progress(progress, ReadStage::Reading, file_size, file_size);
            struct Unmap
            {
                QFile* file = nullptr;
                uchar* mapping = nullptr;
                ~Unmap()
                {
                    if (file && mapping)
                    {
                        file->unmap(mapping);
                    }
                }
            } unmap{&file, mapping};
            if (file_size >= 8 &&
                std::memcmp(bytes, QByteArray::fromHex("D0CF11E0A1B11AE1").constData(), 8) == 0)
            {
                return failure(Error::EncryptedArchive, "文件是加密或旧式复合文档。");
            }
            ArchiveReader reader;
            if (!mz_zip_reader_init_mem(&reader.archive, bytes, file_size, 0))
            {
                return failure(Error::InvalidArchive, "ZIP 压缩包无效。");
            }
            const auto count = mz_zip_reader_get_num_files(&reader.archive);
            if (count > limits.parts)
            {
                return failure(Error::TooLarge, "压缩包条目数量超过限制。");
            }
            std::vector<ArchiveEntry> entries;
            entries.reserve(count);
            std::set<std::string> names;
            std::size_t expanded = 0;
            report_progress(progress, ReadStage::Validating, 0, count);
            const auto validation_step = std::max<mz_uint>(1, count / 100);
            for (mz_uint index = 0; index < count; ++index)
            {
                mz_zip_archive_file_stat entry_information{};
                if (!mz_zip_reader_file_stat(&reader.archive, index, &entry_information))
                {
                    return failure(Error::InvalidArchive, "无法读取 ZIP 条目信息。");
                }
                if (entry_information.m_is_encrypted)
                {
                    return failure(Error::EncryptedArchive, "压缩包包含加密条目。");
                }
                if (!entry_information.m_is_supported)
                {
                    return failure(Error::InvalidArchive, "压缩包使用不支持的压缩方式。");
                }
                const auto filename_size = mz_zip_reader_get_filename(&reader.archive, index, nullptr, 0);
                if (filename_size < 2 || filename_size > limits.path_bytes + 1)
                {
                    return failure(Error::InvalidPackage, "压缩包条目路径无效。");
                }
                std::string name(filename_size, '\0');
                if (mz_zip_reader_get_filename(&reader.archive, index, name.data(), filename_size) !=
                        filename_size ||
                    std::strlen(name.c_str()) != filename_size - 1)
                {
                    return failure(Error::InvalidPackage, "压缩包条目名称包含 NUL。");
                }
                name.pop_back();
                const bool directory =
                    entry_information.m_is_directory != 0 || (!name.empty() && name.back() == '/');
                if (directory)
                {
                    // Office parts are files; directory markers are never extracted or resolved.
                    continue;
                }
                if (!valid_part_path(name, limits.path_bytes) || !names.insert(ascii_lower(name)).second)
                {
                    return failure(Error::InvalidPackage, "压缩包包含重复或越界路径。");
                }
                const auto part_limit = xml_part(name) ? limits.xml_bytes : limits.part_bytes;
                if (entry_information.m_uncomp_size > part_limit ||
                    entry_information.m_uncomp_size >
                        limits.expanded_bytes - std::min(expanded, limits.expanded_bytes))
                {
                    return failure(Error::TooLarge, "压缩包声明的解压内容超过限制。");
                }
                expanded += static_cast<std::size_t>(entry_information.m_uncomp_size);
                entries.push_back(
                    {index, std::move(name), static_cast<std::size_t>(entry_information.m_uncomp_size)});
                if ((index + 1) % validation_step == 0 || index + 1 == count)
                {
                    report_progress(progress, ReadStage::Validating, index + 1, count);
                }
            }
            report_progress(progress, ReadStage::Validating, count, count);
            Result source;
            source.revision = revision(bytes, file_size);
            if (file_size <= 16 * 1024 * 1024)
            {
                source.source_bytes.assign(bytes, file_size);
            }
            source.parts.reserve(entries.size());
            report_progress(progress, ReadStage::Extracting, 0, entries.size());
            for (const auto& entry : entries)
            {
                source.parts.push_back({entry.path, std::string(entry.size, '\0')});
            }
            const bool parallel = file_size >= 32 * 1024 * 1024 && expanded >= 64 * 1024 * 1024 &&
                entries.size() >= 4 && std::thread::hardware_concurrency() > 1;
            if (!parallel)
            {
                const auto extraction_step = std::max<std::size_t>(1, entries.size() / 100);
                for (std::size_t index = 0; index < entries.size(); ++index)
                {
                    auto& part = source.parts[index];
                    if (!mz_zip_reader_extract_to_mem(
                            &reader.archive, entries[index].index, part.bytes.data(), part.bytes.size(), 0))
                    {
                        return failure(Error::InvalidArchive, "ZIP 条目 CRC 或压缩数据无效。");
                    }
                    if ((index + 1) % extraction_step == 0 || index + 1 == entries.size())
                    {
                        report_progress(progress, ReadStage::Extracting, index + 1, entries.size());
                    }
                }
                return source;
            }
            const auto available_workers = static_cast<unsigned>(
                std::min<std::size_t>(entries.size(), std::numeric_limits<unsigned>::max()));
            const unsigned worker_count =
                std::min(4U, std::min(std::thread::hardware_concurrency(), available_workers));
            std::atomic_size_t next{0};
            std::atomic_size_t completed{0};
            std::atomic_bool extraction_failed{false};
            const auto extraction_step = std::max<std::size_t>(1, entries.size() / 100);
            const auto extract = [&]()
            {
                ArchiveReader local;
                if (!mz_zip_reader_init_mem(&local.archive, bytes, file_size, 0))
                {
                    extraction_failed.store(true, std::memory_order_relaxed);
                    return;
                }
                while (!extraction_failed.load(std::memory_order_relaxed))
                {
                    const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= entries.size())
                    {
                        break;
                    }
                    auto& part = source.parts[index];
                    if (!mz_zip_reader_extract_to_mem(
                            &local.archive, entries[index].index, part.bytes.data(), part.bytes.size(), 0))
                    {
                        extraction_failed.store(true, std::memory_order_relaxed);
                        break;
                    }
                    const auto current = completed.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (current % extraction_step == 0 || current == entries.size())
                    {
                        report_progress(progress, ReadStage::Extracting, current, entries.size());
                    }
                }
            };
            std::vector<std::thread> workers;
            workers.reserve(worker_count - 1);
            for (unsigned index = 1; index < worker_count; ++index)
            {
                try
                {
                    workers.emplace_back(extract);
                }
                catch (const std::system_error&)
                {
                    // Resource pressure may reduce parallelism, but it must not abort a valid load.
                    break;
                }
            }
            extract();
            for (auto& worker : workers)
            {
                worker.join();
            }
            if (extraction_failed.load(std::memory_order_relaxed))
            {
                return failure(Error::InvalidArchive, "ZIP 条目 CRC 或压缩数据无效。");
            }
            return source;
        }
        catch (const std::bad_alloc&)
        {
            return failure(Error::TooLarge, "压缩包超过内存预算。");
        }
    }

    Result write(const std::string& path, const std::vector<OfficePart>& parts,
        const std::string& expected_revision, const Limits& limits, const OfficeSaveProgress& progress)
    {
        try
        {
            const auto validation = validate_parts(parts, limits);
            if (validation.error != Error::None)
            {
                return validation;
            }
            return write_archive(path, parts, expected_revision, limits, progress);
        }
        catch (const std::bad_alloc&)
        {
            return failure(Error::TooLarge, "保存内容超过内存预算。");
        }
    }

    Result write_bytes(const std::string& path, const std::string& bytes,
        const std::string& expected_revision, const Limits& limits)
    {
        try
        {
            return commit(path, QByteArray::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size())),
                expected_revision, limits);
        }
        catch (const std::bad_alloc&)
        {
            return failure(Error::TooLarge, "保存内容超过内存预算。");
        }
    }
}
