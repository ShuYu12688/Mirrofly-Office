#include <mirrorfly/spreadsheet_storage.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct Entry
    {
        std::string name;
        std::string bytes;
    };

    void append16(std::string& output, std::uint16_t value)
    {
        output.push_back(static_cast<char>(value));
        output.push_back(static_cast<char>(value >> 8));
    }

    void append32(std::string& output, std::uint32_t value)
    {
        append16(output, static_cast<std::uint16_t>(value));
        append16(output, static_cast<std::uint16_t>(value >> 16));
    }

    std::uint32_t crc32(const std::string& bytes)
    {
        std::uint32_t value = 0xffffffff;
        for (const unsigned char byte : bytes)
        {
            value ^= byte;
            for (int bit = 0; bit < 8; ++bit)
            {
                value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0);
            }
        }
        return ~value;
    }

    std::string archive(const std::vector<Entry>& entries)
    {
        std::string output;
        std::string central;
        for (const auto& entry : entries)
        {
            const auto offset = static_cast<std::uint32_t>(output.size());
            const auto size = static_cast<std::uint32_t>(entry.bytes.size());
            append32(output, 0x04034b50);
            append16(output, 20);
            append16(output, 0);
            append16(output, 0);
            append32(output, 0);
            append32(output, crc32(entry.bytes));
            append32(output, size);
            append32(output, size);
            append16(output, static_cast<std::uint16_t>(entry.name.size()));
            append16(output, 0);
            output += entry.name;
            output += entry.bytes;

            append32(central, 0x02014b50);
            append16(central, 20);
            append16(central, 20);
            append16(central, 0);
            append16(central, 0);
            append32(central, 0);
            append32(central, crc32(entry.bytes));
            append32(central, size);
            append32(central, size);
            append16(central, static_cast<std::uint16_t>(entry.name.size()));
            append16(central, 0);
            append16(central, 0);
            append16(central, 0);
            append16(central, 0);
            append32(central, 0);
            append32(central, offset);
            central += entry.name;
        }
        const auto central_offset = static_cast<std::uint32_t>(output.size());
        output += central;
        append32(output, 0x06054b50);
        append16(output, 0);
        append16(output, 0);
        append16(output, static_cast<std::uint16_t>(entries.size()));
        append16(output, static_cast<std::uint16_t>(entries.size()));
        append32(output, static_cast<std::uint32_t>(central.size()));
        append32(output, central_offset);
        append16(output, 0);
        return output;
    }

    bool write_file(const QString& path, const std::string& bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) &&
            file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size());
    }

    std::string read_file(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        const auto bytes = file.readAll();
        return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }

    bool check(bool condition, const char* label)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << label << '\n';
        }
        return condition;
    }

    bool run_cases(const QString& directory)
    {
        using namespace mirrorfly;
        auto document = make_spreadsheet();
        SpreadsheetEditCommand command;
        command.address = {0, 0};
        command.value = {SpreadsheetValueKind::Text, u8"季度数据 🦋"};
        bool passed = check(apply_spreadsheet_edit(document, command).error == SpreadsheetError::None,
            "prepare editable spreadsheet draft");
        const auto package = serialize_spreadsheet(document);
        const auto path = QDir(directory).filePath(QStringLiteral("数据 🦋.xlsx"));
        auto saved = save_spreadsheet_file(path.toUtf8().toStdString(), package.parts, {});
        passed =
            check(saved.error == SpreadsheetError::None && !saved.path.empty() && saved.revision.size() == 64,
                "atomically save a bounded XLSX and return revision only on success") &&
            passed;
        auto loaded = load_spreadsheet_file(path.toUtf8().toStdString());
        passed = check(loaded.error == SpreadsheetError::None && loaded.revision == saved.revision &&
                         spreadsheet_cell(loaded.document, 0, {0, 0}).value.text == u8"季度数据 🦋",
                     "load the saved ZIP through the shared bounded archive reader") &&
            passed;

        command.address = {1, 0};
        command.value = {SpreadsheetValueKind::Number, "9007199254740993"};
        passed = check(apply_spreadsheet_edit(loaded.document, command).error == SpreadsheetError::None,
                     "retain a local draft before conflict checks") &&
            passed;
        const auto changed_package = serialize_spreadsheet(loaded.document);
        passed = write_file(path, "external change") && passed;
        const auto conflict =
            save_spreadsheet_file(path.toUtf8().toStdString(), changed_package.parts, loaded.revision);
        passed = check(conflict.error == SpreadsheetError::ChangedOnDisk && conflict.revision.empty() &&
                         read_file(path) == "external change" &&
                         spreadsheet_cell(loaded.document, 0, {1, 0}).value.text == "9007199254740993",
                     "external modification preserves disk bytes and the unsaved sparse draft") &&
            passed;

        auto invalid_parts = changed_package.parts;
        invalid_parts.push_back(invalid_parts.front());
        const auto invalid_save = save_spreadsheet_file(path.toUtf8().toStdString(), invalid_parts, {});
        passed = check(invalid_save.error == SpreadsheetError::InvalidPackage && invalid_save.path.empty() &&
                         invalid_save.revision.empty() && read_file(path) == "external change" &&
                         spreadsheet_cell(loaded.document, 0, {1, 0}).value.text == "9007199254740993",
                     "invalid save input returns no identity and preserves disk bytes and draft") &&
            passed;

        const auto malformed_path = QDir(directory).filePath(QStringLiteral("malformed.xlsx"));
        std::vector<Entry> entries;
        for (const auto& part : package.parts)
        {
            entries.push_back({part.path, part.bytes});
        }
        auto directory_entries = entries;
        directory_entries.insert(directory_entries.begin(), {"xl/", {}});
        directory_entries.insert(directory_entries.begin(), {"xl\\worksheets/", {}});
        passed = write_file(malformed_path, archive(directory_entries)) && passed;
        passed = check(load_spreadsheet_file(malformed_path.toUtf8().toStdString()).error ==
                         SpreadsheetError::None,
                     "shared archive reader ignores ZIP directory markers with mixed separators") &&
            passed;
        entries.push_back({"../escape.xml", "<x/>"});
        passed = write_file(malformed_path, archive(entries)) && passed;
        passed = check(load_spreadsheet_file(malformed_path.toUtf8().toStdString()).error ==
                         SpreadsheetError::InvalidPackage,
                     "shared archive reader rejects traversal paths") &&
            passed;
        entries.pop_back();
        entries.push_back({"XL/WORKBOOK.XML", "<x/>"});
        passed = write_file(malformed_path, archive(entries)) && passed;
        passed = check(load_spreadsheet_file(malformed_path.toUtf8().toStdString()).error ==
                         SpreadsheetError::InvalidPackage,
                     "shared archive reader rejects case-folded duplicate paths") &&
            passed;
        passed = check(load_spreadsheet_file("not-a-sheet.pptx").error == SpreadsheetError::UnsupportedType,
                     "spreadsheet storage rejects unsupported paths") &&
            passed;
        return passed;
    }

}

int run_spreadsheet_storage_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    return check(directory.isValid(), "create isolated spreadsheet storage directory") &&
            run_cases(directory.path())
        ? 0
        : 1;
}

int main(int argc, char* argv[])
{
    return run_spreadsheet_storage_tests(argc, argv);
}
