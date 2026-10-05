#include "mirrorfly/platform.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <iostream>

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

    bool write_file(const QString& path, const QByteArray& data)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }

    bool test_inspection(const QString& directory)
    {
        const QString document_path = QDir(directory).filePath(QString::fromUtf8(u8"项目计划.DOCX"));

        if (!check(write_file(document_path, QByteArray(1536, 'a')), "create inspection fixture"))
        {
            return false;
        }

        const auto file = mirrorfly::inspect_local_file(document_path.toUtf8().toStdString());
        bool passed = check(file.has_value(), "inspect an existing regular file");

        if (file)
        {
            passed = check(file->name == u8"项目计划.DOCX", "inspection preserves UTF-8 filename") && passed;
            passed = check(QFileInfo(QString::fromUtf8(file->path.c_str())).isAbsolute(),
                         "inspection returns absolute path") &&
                passed;
            passed = check(file->kind == mirrorfly::DocumentKind::Writer && file->size_text == "2 KB",
                         "inspection classifies extension and rounds kilobytes") &&
                passed;
            passed = check(file->modified.size() == 16 && file->modified[4] == '-' &&
                             file->modified[10] == ' ' && file->modified[13] == ':',
                         "inspection returns stable date format") &&
                passed;
        }

        const QString large_path = QDir(directory).filePath(QStringLiteral("large.xlsx"));
        passed = check(write_file(large_path, QByteArray(1572864, 'x')), "create megabyte fixture") && passed;
        const auto large = mirrorfly::inspect_local_file(large_path.toUtf8().toStdString());
        passed = check(large && large->size_text == "1.5 MB", "inspection formats megabytes") && passed;
        const QString empty_path = QDir(directory).filePath(QStringLiteral("empty.txt"));
        passed = check(write_file(empty_path, {}), "create empty fixture") && passed;
        const auto empty = mirrorfly::inspect_local_file(empty_path.toUtf8().toStdString());
        passed = check(empty && empty->size_text == "0 KB", "inspection supports empty file") && passed;
        passed = check(!mirrorfly::inspect_local_file(directory.toUtf8().toStdString()),
                     "inspection rejects directories") &&
            passed;
        passed = check(!mirrorfly::inspect_local_file(
                           QDir(directory).filePath(QStringLiteral("missing.docx")).toUtf8().toStdString()),
                     "inspection rejects missing files") &&
            passed;
        passed = check(!mirrorfly::inspect_local_file(""), "inspection rejects empty path") && passed;
        return passed;
    }

    bool test_storage(const QString& storage_path)
    {
        const std::vector<mirrorfly::RecentFile> files = {
            {u8"/资料/预算.xlsx", u8"预算.xlsx", "2026-09-12 14:00", "1.5 MB",
                mirrorfly::DocumentKind::Sheets, true},
            {"/draft.docx", "draft.docx", "2026-09-11 11:30", "4 KB", mirrorfly::DocumentKind::Writer,
                false}};
        bool passed = check(mirrorfly::load_recent_files().empty(), "missing history starts empty");
        passed = check(mirrorfly::save_recent_files(files), "save metadata atomically") && passed;
        const auto loaded = mirrorfly::load_recent_files();
        passed = check(loaded.size() == 2, "load persisted record count") && passed;

        if (loaded.size() == 2)
        {
            passed =
                check(loaded[0].path == files[0].path && loaded[0].name == files[0].name &&
                        loaded[0].starred && loaded[0].kind == mirrorfly::DocumentKind::Sheets &&
                        loaded[0].modified == files[0].modified && loaded[0].size_text == files[0].size_text,
                    "metadata and UTF-8 round trip without data loss") &&
                passed;
            passed = check(!loaded[1].starred && loaded[1].path == files[1].path,
                         "history order and unstarred state are preserved") &&
                passed;
        }

        passed =
            check(write_file(storage_path, QByteArray("{broken json")), "write corrupt history fixture") &&
            passed;
        passed =
            check(mirrorfly::load_recent_files().empty(), "corrupt JSON recovers to empty history") && passed;
        passed = check(write_file(storage_path, QByteArray("{\"version\":2,\"files\":[]}")),
                     "write unsupported schema fixture") &&
            passed;
        passed = check(mirrorfly::load_recent_files().empty(), "unsupported schema returns empty") && passed;
        passed = check(write_file(
                           storage_path, QByteArray("{\"version\":1,\"files\":[{\"path\":42},false,null]}")),
                     "write invalid record fixture") &&
            passed;
        passed = check(mirrorfly::load_recent_files().empty(), "invalid record types are ignored safely") &&
            passed;
        passed =
            check(mirrorfly::save_recent_files({}), "replace corrupted history with empty list") && passed;
        passed = check(mirrorfly::load_recent_files().empty(), "empty history round trips") && passed;
        return passed;
    }

    bool test_storage_limits(const QString& storage_path)
    {
        std::vector<mirrorfly::RecentFile> files;
        QJsonArray unbounded_entries;

        for (int index = 0; index < 45; ++index)
        {
            const std::string name = std::to_string(index) + ".docx";
            files.push_back(
                {"/" + name, name, "2026-09-12 14:00", "1 KB", mirrorfly::DocumentKind::Writer, false});
            const QJsonObject entry{{"path", QString::fromStdString(files.back().path)},
                {"name", QString::fromStdString(name)}, {"modified", "2026-09-12 14:00"},
                {"size_text", "1 KB"}, {"starred", false}};
            unbounded_entries.append(QJsonObject{{"path", 42}});
            unbounded_entries.append(entry);
            unbounded_entries.append(entry);
        }

        bool passed = check(mirrorfly::save_recent_files(files), "save oversized record list");
        QFile saved_file(storage_path);
        passed = check(saved_file.open(QIODevice::ReadOnly), "read saved record list") && passed;
        const auto saved_document = QJsonDocument::fromJson(saved_file.readAll());
        saved_file.close();
        passed = check(saved_document.object().value("files").toArray().size() == 30,
                     "persist only the first 30 records") &&
            passed;
        const auto saved = mirrorfly::load_recent_files();
        passed =
            check(saved.size() == 30 && saved.front().path == "/0.docx" && saved.back().path == "/29.docx",
                "saved history limit preserves recent order") &&
            passed;

        const QJsonObject unbounded_root{{"version", 1}, {"files", unbounded_entries}};
        QByteArray unbounded_json = QJsonDocument(unbounded_root).toJson(QJsonDocument::Compact);
        passed = check(write_file(storage_path, unbounded_json), "write long history fixture") && passed;
        const auto loaded = mirrorfly::load_recent_files();
        passed =
            check(loaded.size() == 30 && loaded.front().path == "/0.docx" && loaded.back().path == "/29.docx",
                "load at most 30 valid unique records") &&
            passed;

        constexpr qsizetype maximum_bytes = 1024 * 1024;
        unbounded_json.append(QByteArray(maximum_bytes - unbounded_json.size(), ' '));
        passed = check(write_file(storage_path, unbounded_json), "write 1 MiB valid JSON fixture") && passed;
        passed = check(mirrorfly::load_recent_files().size() == 30,
                     "valid history at size limit remains readable") &&
            passed;
        unbounded_json.append(' ');
        passed = check(write_file(storage_path, unbounded_json), "write JSON beyond size limit") && passed;
        passed = check(mirrorfly::load_recent_files().empty(), "oversized valid JSON is rejected safely") &&
            passed;

        passed =
            check(mirrorfly::save_recent_files({files.front()}), "restore a small valid history") && passed;
        mirrorfly::RecentFile huge_record = files.back();
        huge_record.name.assign(static_cast<std::size_t>(maximum_bytes), 'x');
        passed = check(!mirrorfly::save_recent_files({huge_record}),
                     "reject a single record exceeding the JSON size limit") &&
            passed;
        const auto preserved = mirrorfly::load_recent_files();
        passed = check(preserved.size() == 1 && preserved.front().path == files.front().path,
                     "failed oversized save preserves previous history") &&
            passed;
        return passed;
    }

}

int run_platform_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MirrorflyTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Platform-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QTemporaryDir temporary_directory;

    if (!check(temporary_directory.isValid(), "create isolated fixture directory"))
    {
        return 1;
    }

    const QString storage_directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString storage_path = QDir(storage_directory).filePath(QStringLiteral("recent.json"));
    bool passed = test_inspection(temporary_directory.path());
    passed = test_storage(storage_path) && passed;
    passed = test_storage_limits(storage_path) && passed;
    QFile::remove(storage_path);
    QDir().rmdir(storage_directory);

    if (passed)
    {
        std::cout << "Platform tests passed.\n";
    }

    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_platform_tests(argc, argv);
}
