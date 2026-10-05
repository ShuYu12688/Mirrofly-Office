#include <mirrorfly/word_storage.hpp>

#include "archive_storage.hpp"

namespace
{
    mirrorfly::archive_storage::Limits limits()
    {
        return {mirrorfly::maximum_word_archive_bytes, mirrorfly::maximum_word_expanded_bytes,
            mirrorfly::maximum_word_part_bytes, mirrorfly::maximum_word_xml_bytes, 4096};
    }

    mirrorfly::WordResult failure(const std::string& error)
    {
        mirrorfly::WordResult result;
        result.error = error;
        return result;
    }

    void report(const mirrorfly::WordLoadProgress& progress, mirrorfly::WordLoadStage stage,
        std::size_t completed, std::size_t total) noexcept
    {
        try
        {
            if (progress)
                progress(stage, completed, total);
        }
        catch (...)
        {
            // A progress observer cannot change whether the source package is valid.
        }
    }
}

namespace mirrorfly
{
    WordResult load_word_file(const std::string& path, const WordLoadProgress& progress)
    {
        if (!is_word_path(path))
        {
            return failure("当前 Word 模块支持 .docx 文件。");
        }
        auto archive = archive_storage::read(path, limits(),
            [&](archive_storage::ReadStage stage, std::size_t completed, std::size_t total)
        {
            auto target = WordLoadStage::Extracting;
            if (stage == archive_storage::ReadStage::Reading)
                target = WordLoadStage::Reading;
            else if (stage == archive_storage::ReadStage::Validating)
                target = WordLoadStage::Validating;
            report(progress, target, completed, total);
        });
        if (archive.error != archive_storage::Error::None)
        {
            return failure("无法读取 DOCX：" + archive.message);
        }
        report(progress, WordLoadStage::Parsing, 0, 1);
        auto result = parse_word(std::move(archive.parts));
        if (result.success)
        {
            result.path = path;
            result.revision = archive.revision;
            report(progress, WordLoadStage::Parsing, 1, 1);
        }
        return result;
    }

    WordResult save_word_file(const std::string& path, const WordDocument& document,
        const std::string& expected_revision, const OfficeSaveProgress& progress)
    {
        if (!is_word_path(path))
        {
            return failure("请使用 .docx 扩展名保存。");
        }
        auto result = serialize_word(document);
        if (!result.success)
        {
            return result;
        }
        const auto archive =
            archive_storage::write(path, result.parts, expected_revision, limits(), progress);
        if (archive.error != archive_storage::Error::None)
        {
            return failure(archive.error == archive_storage::Error::ChangedOnDisk
                    ? "文件已被其他程序修改或移走，请另存为。当前草稿已保留。"
                    : "DOCX 保存失败，当前草稿已保留：" + archive.message);
        }
        result.parts.clear();
        result.path = path;
        result.revision = archive.revision;
        return result;
    }
}
