#include "mirrorfly/text_storage.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <string>

namespace
{

    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }

        return condition;
    }

    std::string path_in(const QString& directory, const QString& name)
    {
        return QDir(directory).filePath(name).toUtf8().toStdString();
    }

    bool write_fixture(const std::string& path, const QByteArray& bytes)
    {
        QFile file(QString::fromUtf8(path.c_str()));
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

    QByteArray read_fixture(const std::string& path)
    {
        QFile file(QString::fromUtf8(path.c_str()));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }

    bool test_round_trip(const QString& directory)
    {
        using namespace mirrorfly;
        const std::string path = path_in(directory, QString::fromUtf8(u8"你好🦋.TeXt"));
        const std::string content = u8"中文与 emoji 🦋\n下一行\n";
        const TextFormat format{TextEncoding::Utf16BigEndian, LineEnding::CrLf, false};
        const auto saved = save_text_file(path, content, format, "");
        const auto loaded = load_text_file(path);
        bool passed = check(saved.error == TextFileError::None && loaded.error == TextFileError::None &&
                loaded.text == content && loaded.format.encoding == format.encoding &&
                loaded.format.line_ending == format.line_ending && saved.revision == loaded.revision,
            "UTF-16 Chinese and emoji documents save and load through UTF-8 paths");
        passed = check(loaded.revision ==
                             QCryptographicHash::hash(read_fixture(path), QCryptographicHash::Sha256)
                                 .toHex()
                                 .toStdString() &&
                         loaded.revision.size() == 64 &&
                         QFileInfo(QString::fromUtf8(loaded.path.c_str())).isAbsolute(),
                     "results carry an absolute UTF-8 path and the original-byte SHA-256 revision") &&
            passed;
        const auto updated = save_text_file(path, u8"已修改\n", loaded.format, loaded.revision);
        passed = check(updated.error == TextFileError::None && updated.revision != loaded.revision &&
                         load_text_file(path).text == u8"已修改\n",
                     "saving the current revision replaces the file and returns the new revision") &&
            passed;

        const std::string empty_path = path_in(directory, QStringLiteral("empty.txt"));
        passed = check(save_text_file(empty_path, "", {}, "").error == TextFileError::None &&
                         load_text_file(empty_path).text.empty() && read_fixture(empty_path).isEmpty(),
                     "empty plain text is a valid file") &&
            passed;

        const std::string mixed_path = path_in(directory, QStringLiteral("mixed.text"));
        passed =
            check(write_fixture(mixed_path, "a\r\nb\nc\r\n"), "create mixed line-ending fixture") && passed;
        const auto mixed = load_text_file(mixed_path);
        const auto unified = save_text_file(mixed_path, mixed.text, mixed.format, mixed.revision);
        passed = check(mixed.format.mixed_line_endings && unified.error == TextFileError::None &&
                         !unified.format.mixed_line_endings && read_fixture(mixed_path) == "a\r\nb\r\nc\r\n",
                     "saving mixed line endings preserves the dominant style and clears its mixed flag") &&
            passed;
        return passed;
    }

    bool test_conflicts(const QString& directory)
    {
        using namespace mirrorfly;
        const std::string path = path_in(directory, QStringLiteral("conflict.txt"));
        bool passed = check(write_fixture(path, "original"), "create conflict fixture");
        const auto original = load_text_file(path);
        passed = check(write_fixture(path, "external"), "simulate external modification") && passed;
        const auto conflict = save_text_file(path, "local edit", original.format, original.revision);
        passed = check(conflict.error == TextFileError::ChangedOnDisk && read_fixture(path) == "external",
                     "a changed revision preserves the externally modified file") &&
            passed;
        passed = check(QFile::remove(QString::fromUtf8(path.c_str())), "remove temporary conflict fixture") &&
            passed;
        passed = check(save_text_file(path, "local edit", original.format, original.revision).error ==
                             TextFileError::ChangedOnDisk &&
                         !QFileInfo::exists(QString::fromUtf8(path.c_str())),
                     "a deleted original is not silently recreated by a stale save") &&
            passed;
        return passed;
    }

    bool test_failures_preserve_files(const QString& directory)
    {
        using namespace mirrorfly;
        const std::string path = path_in(directory, QStringLiteral("protected.txt"));
        bool passed = check(write_fixture(path, "keep me"), "create preservation fixture");
        const auto original = load_text_file(path);
        passed = check(save_text_file(path, std::string("\xC0\xAF", 2), {}, original.revision).error ==
                             TextFileError::InvalidEncoding &&
                         read_fixture(path) == "keep me",
                     "invalid Unicode fails before modifying the original") &&
            passed;
        passed =
            check(
                save_text_file(path, std::string(maximum_text_bytes + 1, 'x'), {}, original.revision).error ==
                        TextFileError::TooLarge &&
                    read_fixture(path) == "keep me",
                "oversized edits leave the original file intact") &&
            passed;

        const std::string missing = path_in(directory, QStringLiteral("missing/failure.text"));
        passed = check(load_text_file(missing).error == TextFileError::ReadFailed &&
                         save_text_file(missing, "new", {}, "").error == TextFileError::WriteFailed,
                     "missing files and missing output directories produce explicit failures") &&
            passed;
        const std::string folder = path_in(directory, QStringLiteral("directory.text"));
        passed =
            check(QDir().mkpath(QString::fromUtf8(folder.c_str())), "create directory fixture") && passed;
        passed = check(load_text_file(folder).error == TextFileError::ReadFailed &&
                         save_text_file(folder, "new", {}, "").error == TextFileError::WriteFailed &&
                         QFileInfo(QString::fromUtf8(folder.c_str())).isDir(),
                     "directories cannot be loaded or replaced as text files") &&
            passed;
        passed = check(load_text_file(path_in(directory, QStringLiteral("unsupported.docx"))).error ==
                         TextFileError::UnsupportedType,
                     "unsupported extensions are rejected") &&
            passed;

        const auto old_permissions = QFile::permissions(QString::fromUtf8(path.c_str()));
        const auto read_only = old_permissions &
            ~(QFileDevice::WriteOwner | QFileDevice::WriteUser | QFileDevice::WriteGroup |
                QFileDevice::WriteOther);

        if (QFile::setPermissions(QString::fromUtf8(path.c_str()), read_only) &&
            !QFileInfo(QString::fromUtf8(path.c_str())).isWritable())
        {
            passed = check(save_text_file(path, "must not replace", {}, original.revision).error ==
                                 TextFileError::WriteFailed &&
                             read_fixture(path) == "keep me",
                         "read-only file rejection preserves its original bytes") &&
                passed;
        }
        else
        {
            std::cout << "SKIP: filesystem does not enforce read-only permissions for this account.\n";
        }

        QFile::setPermissions(QString::fromUtf8(path.c_str()), old_permissions);
        return passed;
    }

    bool test_input_bounds_and_links(const QString& directory)
    {
        using namespace mirrorfly;
        const std::string large = path_in(directory, QStringLiteral("large.text"));
        bool passed =
            check(write_fixture(large, QByteArray(static_cast<qsizetype>(maximum_text_bytes + 1), 'a')),
                "create oversized input fixture");
        passed = check(load_text_file(large).error == TextFileError::TooLarge,
                     "oversized disk input is rejected before decoding") &&
            passed;
        const std::string binary = path_in(directory, QStringLiteral("binary.txt"));
        passed = check(write_fixture(binary, QByteArray("a\0b", 3)) &&
                         load_text_file(binary).error == TextFileError::BinaryContent,
                     "binary input has a distinct failure") &&
            passed;
        const std::string malformed = path_in(directory, QStringLiteral("malformed.text"));
        passed = check(write_fixture(malformed, QByteArray("\xFF\xFE\x00\xD8", 4)) &&
                         load_text_file(malformed).error == TextFileError::InvalidEncoding,
                     "malformed UTF-16 input has a distinct failure") &&
            passed;

        const std::string target = path_in(directory, QStringLiteral("link-target.txt"));
        const std::string link = path_in(directory, QStringLiteral("symbolic-link.txt"));
        passed =
            check(write_fixture(target, "target bytes"), "create symbolic link target fixture") && passed;
        std::error_code error;
        std::filesystem::create_symlink(
            std::filesystem::u8path(target), std::filesystem::u8path(link), error);

        if (!error)
        {
            passed =
                check(load_text_file(link).error == TextFileError::ReadFailed &&
                        save_text_file(link, "replacement", {}, "").error == TextFileError::WriteFailed &&
                        read_fixture(target) == "target bytes",
                    "symbolic links are rejected without changing their targets") &&
                passed;
        }
        else
        {
            std::cout << "SKIP: symbolic link creation unavailable: " << error.message() << '\n';
        }

        return passed;
    }

}

int run_text_storage_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;

    if (!check(directory.isValid(), "create an isolated temporary directory"))
    {
        return 1;
    }

    bool passed = test_round_trip(directory.path());
    passed = test_conflicts(directory.path()) && passed;
    passed = test_failures_preserve_files(directory.path()) && passed;
    passed = test_input_bounds_and_links(directory.path()) && passed;

    if (passed)
    {
        std::cout << "Text storage tests passed.\n";
    }

    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_text_storage_tests(argc, argv);
}
