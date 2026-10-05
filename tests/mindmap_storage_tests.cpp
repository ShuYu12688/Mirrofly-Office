#include <mirrorfly/mindmap_storage.hpp>

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
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
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

    void check_round_trip(const QString& directory)
    {
        using namespace mirrorfly;
        const auto independent_path = path_in(directory, QString::fromUtf8(u8"独立文件.mfg"));
        const QByteArray independent("<mirrorfly-map version='1' root='r'>"
                                     "<node id='r' text='根' x='80' y='80' width='220' height='84' border='' "
                                     "fill='' shape='rounded' stroke='2'/>"
                                     "<node id='c' text='子' x='380' y='80' width='220' height='84' "
                                     "border='' fill='' shape='rounded' stroke='2'/>"
                                     "<edge id='e' from='r' to='c' label=''/></mirrorfly-map>");
        expect(write_fixture(independent_path, independent), "independent MFG fixture is created");
        const auto loaded = load_mindmap_file(independent_path);
        expect(loaded.error == MindMapError::None && loaded.document.nodes.size() == 2 &&
                loaded.document.root_id == "r" && loaded.document.edges[0].to == "c",
            "independent MFG XML loads through a UTF-8 path");
        expect(QFileInfo(QString::fromUtf8(loaded.path.c_str())).isAbsolute() &&
                loaded.revision ==
                    QCryptographicHash::hash(independent, QCryptographicHash::Sha256).toHex().toStdString(),
            "load returns an absolute path and original-byte SHA-256 revision");

        auto document = make_mindmap("保存测试");
        MindMapCommand command;
        command.type = MindMapCommandType::AddChild;
        command.target_id = "root";
        command.new_id = "child";
        command.text = "子节点";
        expect(apply_mindmap_command(document, command).changed, "saved fixture receives a child");
        const auto saved_path = path_in(directory, QStringLiteral("saved.MFG"));
        const auto saved = save_mindmap_file(saved_path, document);
        const auto reopened = load_mindmap_file(saved_path);
        expect(saved.error == MindMapError::None && reopened.error == MindMapError::None &&
                saved.revision == reopened.revision && reopened.document.nodes.size() == 2,
            "save writes plain MFG XML and returns the committed revision");
        expect(read_fixture(saved_path).startsWith("<?xml") && !read_fixture(saved_path).startsWith("PK"),
            "MFG storage writes XML rather than a ZIP package");
    }

    void check_conflicts(const QString& directory)
    {
        using namespace mirrorfly;
        const auto path = path_in(directory, QStringLiteral("conflict.mfg"));
        const auto first = save_mindmap_file(path, make_mindmap("first"));
        expect(first.error == MindMapError::None, "conflict fixture saves");
        expect(write_fixture(path, read_fixture(path) + "\n"), "external edit is simulated");
        const auto external = read_fixture(path);
        const auto conflict = save_mindmap_file(path, make_mindmap("local"), first.revision);
        expect(conflict.error == MindMapError::ChangedOnDisk && read_fixture(path) == external,
            "stale revision rejection preserves external bytes");

        const auto current = load_mindmap_file(path);
        expect(QFile::remove(QString::fromUtf8(path.c_str())), "conflict fixture is removed");
        const auto deleted = save_mindmap_file(path, make_mindmap("local"), current.revision);
        expect(deleted.error == MindMapError::ChangedOnDisk &&
                !QFileInfo::exists(QString::fromUtf8(path.c_str())),
            "a stale save does not recreate a deleted source");
    }

    void check_failures(const QString& directory)
    {
        using namespace mirrorfly;
        const auto path = path_in(directory, QStringLiteral("preserve.mfg"));
        expect(write_fixture(path, "<map version='1.0.1'><node TEXT='keep' ID='r'/></map>"),
            "preservation fixture is created");
        const auto original = read_fixture(path);
        auto invalid = make_mindmap();
        invalid.nodes[0].text.assign(maximum_mindmap_node_text_bytes + 1, 'x');
        const auto rejected = save_mindmap_file(path, invalid);
        expect(rejected.error == MindMapError::TooLarge && read_fixture(path) == original,
            "invalid documents fail before modifying an existing file");

        const auto unsupported = path_in(directory, QStringLiteral("map.txt"));
        expect(load_mindmap_file(unsupported).error == MindMapError::UnsupportedType &&
                save_mindmap_file(unsupported, make_mindmap()).error == MindMapError::UnsupportedType,
            "non-MFG extensions are rejected for load and save");
        const auto missing = path_in(directory, QStringLiteral("missing/file.mfg"));
        expect(load_mindmap_file(missing).error == MindMapError::ReadFailed &&
                save_mindmap_file(missing, make_mindmap()).error == MindMapError::WriteFailed,
            "missing input files and output directories return explicit errors");
        const auto folder = path_in(directory, QStringLiteral("folder.mfg"));
        expect(QDir().mkpath(QString::fromUtf8(folder.c_str())), "directory fixture is created");
        expect(load_mindmap_file(folder).error == MindMapError::ReadFailed &&
                save_mindmap_file(folder, make_mindmap()).error == MindMapError::WriteFailed &&
                QFileInfo(QString::fromUtf8(folder.c_str())).isDir(),
            "directories cannot be read or replaced as MFG files");

        const auto large = path_in(directory, QStringLiteral("large.mfg"));
        expect(write_fixture(large, QByteArray(static_cast<qsizetype>(maximum_mindmap_xml_bytes + 1), 'x')),
            "oversized fixture is created");
        expect(load_mindmap_file(large).error == MindMapError::TooLarge,
            "oversized files are rejected before parsing");
    }

    void check_links(const QString& directory)
    {
        using namespace mirrorfly;
        const auto target = path_in(directory, QStringLiteral("target.mfg"));
        const auto link = path_in(directory, QStringLiteral("link.mfg"));
        const QByteArray original("<map version='1.0.1'><node TEXT='target' ID='r'/></map>");
        expect(write_fixture(target, original), "symbolic-link target fixture is created");
        std::error_code error;
        std::filesystem::create_symlink(
            std::filesystem::u8path(target), std::filesystem::u8path(link), error);
        if (error)
        {
            std::cout << "SKIP: symbolic link creation unavailable: " << error.message() << '\n';
            return;
        }
        expect(load_mindmap_file(link).error == MindMapError::ReadFailed &&
                save_mindmap_file(link, make_mindmap("replacement")).error == MindMapError::WriteFailed &&
                read_fixture(target) == original,
            "symbolic links are rejected without reading or replacing their targets");
    }
}

int run_mindmap_storage_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    expect(directory.isValid(), "isolated temporary directory is available");
    if (!directory.isValid())
    {
        return 1;
    }
    check_round_trip(directory.path());
    check_conflicts(directory.path());
    check_failures(directory.path());
    check_links(directory.path());
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_mindmap_storage_tests(argc, argv);
}
