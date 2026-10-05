#include <mirrorfly/presentation_storage.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
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
        std::uint16_t flags = 0;
        std::uint32_t declared_size = 0;
        std::uint16_t method = 0;
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
            const auto unpacked = entry.declared_size == 0 ? size : entry.declared_size;
            append32(output, 0x04034b50);
            append16(output, 20);
            append16(output, entry.flags);
            append16(output, entry.method);
            append32(output, 0);
            append32(output, crc32(entry.bytes));
            append32(output, size);
            append32(output, unpacked);
            append16(output, static_cast<std::uint16_t>(entry.name.size()));
            append16(output, 0);
            output += entry.name;
            output += entry.bytes;

            append32(central, 0x02014b50);
            append16(central, 20);
            append16(central, 20);
            append16(central, entry.flags);
            append16(central, entry.method);
            append32(central, 0);
            append32(central, crc32(entry.bytes));
            append32(central, size);
            append32(central, unpacked);
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

    std::vector<Entry> package()
    {
        return {
            {"[Content_Types].xml",
                R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/><Override PartName="/ppt/slides/slide1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/></Types>)"},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="r1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="ppt/presentation.xml"/></Relationships>)"},
            {"ppt/presentation.xml",
                R"(<p:presentation xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><p:sldIdLst><p:sldId id="256" r:id="r1"/></p:sldIdLst><p:sldSz cx="9144000" cy="5143500"/></p:presentation>)"},
            {"ppt/_rels/presentation.xml.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="r1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="slides/slide1.xml"/></Relationships>)"},
            {"ppt/slides/slide1.xml",
                R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"><p:cSld><p:spTree/></p:cSld></p:sld>)"}};
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

    mirrorfly::PresentationScene validation_scene()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Title);
        scene.slides.front().title = u8"大学课程汇报";
        scene.slides.front().shapes.front().text.paragraphs.front().runs.front().text =
            u8"大学课程汇报 · Mirrorfly";
        scene.slides.front().shapes.front().text.paragraphs.front().runs.front().bold = true;

        for (const auto layout : {PresentationSlideLayout::TitleContent, PresentationSlideLayout::TwoColumns,
                 PresentationSlideLayout::Blank})
        {
            PresentationEditCommand command;
            command.action = PresentationEditAction::AddSlide;
            command.slide_index = scene.slides.size() - 1;
            command.layout = layout;
            apply_presentation_edit(scene, command);
        }
        scene.slides[1].hidden = true;
        auto& paragraph = scene.slides[1].shapes.back().text.paragraphs.front();
        paragraph.bullet = u8"•";
        paragraph.line_spacing = 1.35;
        paragraph.space_before = 5;
        paragraph.space_after = 8;
        paragraph.runs.front().text = u8"中英混排 Campus life";
        paragraph.runs.front().italic = true;
        paragraph.runs.front().underline = true;

        auto& blank = scene.slides.back();
        blank.background.color.clear();
        blank.background.angle_degrees = 35;
        blank.background.stops = {{0, "#F5EFE6", 1}, {1, "#D5DDD3", 1}};
        const double radians = 30 * 3.14159265358979323846 / 180;
        const std::vector<std::string> geometries{"rect", "roundRect", "ellipse", "line", "rightArrow"};
        for (std::size_t index = 0; index < geometries.size(); ++index)
        {
            PresentationShape shape;
            shape.id = scene.next_shape_id++;
            shape.name = "Validation shape " + std::to_string(index + 1);
            shape.geometry = geometries[index];
            shape.width = index == 3 ? 160 : 110;
            shape.height = index == 3 ? 1 : 65;
            shape.transform = {std::cos(radians) * 1.2, std::sin(radians) * 1.2, -std::sin(radians) * 0.8,
                std::cos(radians) * 0.8, 70.0 + static_cast<double>(index) * 125,
                95.0 + static_cast<double>(index % 2) * 120};
            shape.fill.color = index == 3 ? "" : "#9B6B43";
            shape.outline_color = "#59685B";
            blank.shapes.push_back(std::move(shape));
        }

        const QByteArray image_bytes = QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
        scene.images.push_back({"validation.png", "image/png",
            std::string(image_bytes.constData(), static_cast<std::size_t>(image_bytes.size()))});
        PresentationShape image;
        image.id = scene.next_shape_id++;
        image.name = "Validation image";
        image.image_path = "validation.png";
        image.width = 180;
        image.height = 110;
        image.transform = {1, 0, 0, 1, 70, 350};
        image.image_crop = {0.05, 0.1, 0.08, 0.04};
        blank.shapes.push_back(std::move(image));
        return scene;
    }

    bool run_cases(const QString& directory)
    {
        using namespace mirrorfly;
        const auto compressed = load_presentation_file(
            std::string(MIRRORFLY_TEST_FIXTURE_DIRECTORY) + "/presentation-deflate.pptx");
        bool passed = check(compressed.error == PresentationError::None &&
                compressed.scene.slides.size() == 1 && compressed.scene.slides.front().shapes.size() == 1 &&
                compressed.scene.slides.front().shapes.front().text.paragraphs.front().runs.front().text ==
                    u8"Mirrorfly 课堂预览 🦋",
            "load independently deflated PPTX with Unicode runs");
        const QString saved_path = QDir(directory).filePath(QStringLiteral("Mirrorfly 保存.pptx"));
        auto editable = make_presentation(PresentationSlideLayout::TwoColumns);
        auto serialized = serialize_presentation(editable);
        auto saved = save_presentation_file(saved_path.toUtf8().toStdString(), serialized.parts, {});
        passed = check(saved.error == PresentationError::None && !saved.revision.empty(),
                     "stream bounded PPTX package through miniz and atomic storage") &&
            passed;
        auto reopened = load_presentation_file(saved_path.toUtf8().toStdString());
        passed = check(reopened.error == PresentationError::None && reopened.scene.slides.size() == 1 &&
                         !reopened.source_bytes.empty() && reopened.source_bytes == read_file(saved_path),
                     "small packages retain exact source bytes for compatible save-as") &&
            passed;
        const QString exact_copy = QDir(directory).filePath(QStringLiteral("exact-copy.pptx"));
        const auto copied =
            save_presentation_bytes(exact_copy.toUtf8().toStdString(), reopened.source_bytes, {});
        passed =
            check(copied.error == PresentationError::None && read_file(exact_copy) == reopened.source_bytes,
                "unmodified save-as preserves the original package bytes") &&
            passed;
        passed = write_file(saved_path, "external change") && passed;
        const auto conflict =
            save_presentation_file(saved_path.toUtf8().toStdString(), serialized.parts, reopened.revision);
        passed = check(conflict.error == PresentationError::ChangedOnDisk &&
                         read_file(saved_path) == "external change",
                     "external modification blocks atomic overwrite and preserves disk bytes") &&
            passed;
        const auto path = QDir(directory).filePath(QStringLiteral("课程 🦋.PPTX"));
        const auto utf8_path = path.toUtf8().toStdString();
        auto entries = package();
        passed = check(write_file(path, archive(entries)), "create independent stored ZIP fixture") && passed;
        std::vector<PresentationLoadProgress> progress;
        auto result = load_presentation_file(utf8_path, [&progress](const PresentationLoadProgress& update)
        {
            progress.push_back(update);
        });
        passed = check(result.error == PresentationError::None && result.scene.slides.size() == 1 &&
                         result.scene.width == 720 && result.scene.height == 405,
                     "read actual local PPTX with Unicode path and slide dimensions") &&
            passed;
        const auto saw_stage = [&progress](PresentationLoadStage stage)
        {
            return std::any_of(progress.cbegin(), progress.cend(), [stage](const auto& update)
            {
                return update.stage == stage;
            });
        };
        const auto parsed = std::find_if(progress.crbegin(), progress.crend(), [](const auto& update)
        {
            return update.stage == PresentationLoadStage::Parsing;
        });
        passed =
            check(saw_stage(PresentationLoadStage::Reading) && saw_stage(PresentationLoadStage::Validating) &&
                    saw_stage(PresentationLoadStage::Extracting) && parsed != progress.crend() &&
                    parsed->total == 1 && parsed->completed == 1,
                "report bounded archive and slide parsing progress") &&
            passed;
        entries.insert(entries.begin(), {"ppt\\slides/", {}});
        entries.insert(entries.begin(), {"ppt/", {}});
        passed = write_file(path, archive(entries)) && passed;
        passed = check(load_presentation_file(utf8_path).error == PresentationError::None,
                     "ignore ZIP directory markers with mixed path separators") &&
            passed;
        entries = package();
        entries.push_back({"ppt\\slides\\escape.xml", "<a/>"});
        passed = write_file(path, archive(entries)) && passed;
        passed = check(load_presentation_file(utf8_path).error == PresentationError::InvalidPackage,
                     "reject backslashes in actual archive file paths") &&
            passed;
        entries = package();
        entries.push_back({"../escape.xml", "<a/>"});
        passed = write_file(path, archive(entries)) && passed;
        passed = check(load_presentation_file(utf8_path).error == PresentationError::InvalidPackage,
                     "reject archive traversal before parsing") &&
            passed;
        entries = package();
        entries.push_back({"PPT/PRESENTATION.XML", "<a/>"});
        passed = write_file(path, archive(entries)) && passed;
        passed = check(load_presentation_file(utf8_path).error == PresentationError::InvalidPackage,
                     "reject ambiguous case-folded duplicate parts") &&
            passed;
        entries = package();
        entries.back().flags = 1;
        passed = write_file(path, archive(entries)) && passed;
        passed = check(load_presentation_file(utf8_path).error == PresentationError::EncryptedArchive,
                     "reject encrypted member without attempting to read it") &&
            passed;
        entries = package();
        entries.back().declared_size = static_cast<std::uint32_t>(maximum_presentation_xml_bytes + 1);
        entries.back().method = 8;
        passed = write_file(path, archive(entries)) && passed;
        passed = check(load_presentation_file(utf8_path).error == PresentationError::TooLarge,
                     "bound declared expansion before allocating XML") &&
            passed;
        auto corrupt = archive(package());
        const auto offset = corrupt.find("<Types");
        corrupt[offset + 1] = 'X';
        passed = write_file(path, corrupt) && passed;
        passed = check(load_presentation_file(utf8_path).error == PresentationError::InvalidArchive,
                     "verify ZIP CRC and retain no partial scene") &&
            passed;
        passed = write_file(path, "not a zip") && passed;
        passed = check(load_presentation_file(utf8_path).error == PresentationError::InvalidArchive,
                     "reject a malformed archive") &&
            passed;
        passed = check(load_presentation_file(utf8_path + ".txt").error == PresentationError::UnsupportedType,
                     "reject a non-PPTX path") &&
            passed;
        QFile::remove(path);
        passed = check(load_presentation_file(utf8_path).error == PresentationError::ReadFailed,
                     "report missing local file") &&
            passed;
        const QString validation_output = qEnvironmentVariable("MIRRORFLY_PPTX_VALIDATION_OUTPUT");
        if (!validation_output.isEmpty())
        {
            const auto package = serialize_presentation(validation_scene());
            const auto validation =
                save_presentation_file(validation_output.toUtf8().toStdString(), package.parts, {});
            passed =
                check(package.error == PresentationError::None && validation.error == PresentationError::None,
                    "write opt-in comprehensive validation package") &&
                passed;
        }
        const QString theme_output = qEnvironmentVariable("MIRRORFLY_PPTX_AUTHORED_THEME_OUTPUT");
        if (!theme_output.isEmpty())
        {
            auto authored = make_presentation(PresentationSlideLayout::Title);
            PresentationEditCommand add;
            add.action = PresentationEditAction::AddSlide;
            add.slide_index = 1;
            add.layout = PresentationSlideLayout::ResearchStudio;
            add.template_palette = {"#243246", "#F7F8FA", "#FFFFFF", "#697484", "#426EA8", "#DFEAF5"};
            const auto first = apply_presentation_edit(authored, add);
            add.slide_index = 2;
            add.layout = PresentationSlideLayout::ProjectDashboard;
            add.template_palette.accent = "#985935";
            const auto second = apply_presentation_edit(authored, add);
            PresentationEditCommand theme;
            theme.action = PresentationEditAction::ApplyTheme;
            theme.slide_index = 1;
            theme.theme_colors = {{"accent1", "#123456"}};
            theme.theme_fonts = {{"majorLatin", "Georgia"}, {"majorEastAsian", "Georgia"}};
            const auto themed = apply_presentation_edit(authored, theme);
            const auto package = serialize_presentation(authored);
            const auto theme_saved =
                save_presentation_file(theme_output.toUtf8().toStdString(), package.parts, {});
            passed = check(first.error == PresentationEditError::None &&
                             second.error == PresentationEditError::None &&
                             themed.error == PresentationEditError::None &&
                             package.error == PresentationError::None &&
                             theme_saved.error == PresentationError::None,
                         "write opt-in authored multi-theme validation package") &&
                passed;
        }
        const QString table_output = qEnvironmentVariable("MIRRORFLY_PPTX_TABLE_STRUCTURE_OUTPUT");
        if (!table_output.isEmpty())
        {
            auto authored = make_presentation(PresentationSlideLayout::Blank);
            authored.native_editable = true;
            PresentationEditCommand table;
            table.action = PresentationEditAction::InsertTable;
            table.table_rows = 3;
            table.table_columns = 3;
            const auto inserted = apply_presentation_edit(authored, table);
            const auto cell_index = [&authored](std::size_t row, std::size_t column)
            {
                const auto& shapes = authored.slides[0].shapes;
                for (std::size_t index = 0; index < shapes.size(); ++index)
                    if (shapes[index].table_cell && shapes[index].table_cell->row == row &&
                        shapes[index].table_cell->column == column)
                        return index;
                return shapes.size();
            };
            table.action = PresentationEditAction::MergeTableCell;
            table.table_direction = "right";
            table.shape_index = cell_index(0, 0);
            const auto first_merge = apply_presentation_edit(authored, table);
            table.action = PresentationEditAction::InsertTableRow;
            table.shape_index = cell_index(1, 2);
            const auto extra_row = apply_presentation_edit(authored, table);
            table.action = PresentationEditAction::MergeTableCell;
            table.shape_index = cell_index(1, 0);
            const auto second_merge = apply_presentation_edit(authored, table);
            const auto package = serialize_presentation(authored);
            const auto structure_saved =
                save_presentation_file(table_output.toUtf8().toStdString(), package.parts, {});
            passed = check(inserted.error == PresentationEditError::None &&
                             first_merge.error == PresentationEditError::None &&
                             extra_row.error == PresentationEditError::None &&
                             second_merge.error == PresentationEditError::None &&
                             package.error == PresentationError::None &&
                             structure_saved.error == PresentationError::None,
                         "write opt-in table structure interop package") &&
                passed;
        }
        const QString transition_output = qEnvironmentVariable("MIRRORFLY_PPTX_TRANSITION_OUTPUT");
        if (!transition_output.isEmpty())
        {
            auto authored = make_presentation(PresentationSlideLayout::Blank);
            PresentationEditCommand add;
            add.action = PresentationEditAction::AddSlide;
            add.slide_index = 1;
            add.layout = PresentationSlideLayout::Blank;
            const auto added = apply_presentation_edit(authored, add);
            PresentationEditCommand transition;
            transition.action = PresentationEditAction::SetSlideTransition;
            transition.slide_index = 1;
            PresentationTransition value;
            value.type = "push";
            value.direction = "l";
            value.duration = 1.0;
            value.advance_on_click = false;
            value.advance_after = 2.5;
            transition.slide_transition = value;
            const auto edited = apply_presentation_edit(authored, transition);
            const auto package = serialize_presentation(authored);
            const auto transition_saved =
                save_presentation_file(transition_output.toUtf8().toStdString(), package.parts, {});
            passed = check(added.error == PresentationEditError::None &&
                             edited.error == PresentationEditError::None &&
                             package.error == PresentationError::None &&
                             transition_saved.error == PresentationError::None,
                         "write opt-in slide transition interop package") &&
                passed;
        }
        const QString group_output = qEnvironmentVariable("MIRRORFLY_PPTX_GROUP_LAYER_OUTPUT");
        if (!group_output.isEmpty())
        {
            auto authored = make_presentation(PresentationSlideLayout::Title);
            PresentationEditCommand add;
            add.action = PresentationEditAction::AddShape;
            add.geometry = "rect";
            const auto added = apply_presentation_edit(authored, add);
            PresentationEditCommand group;
            group.action = PresentationEditAction::GroupAdjacent;
            group.shape_index = 0;
            group.target_index = 1;
            const auto grouped = apply_presentation_edit(authored, group);
            PresentationEditCommand layer;
            layer.action = PresentationEditAction::ReorderGroup;
            if (!authored.slides[0].groups.empty())
                layer.group_id = authored.slides[0].groups[0].source_id;
            layer.group_layer_position = "front";
            const auto reordered = apply_presentation_edit(authored, layer);
            const auto package = serialize_presentation(authored);
            const auto group_saved =
                save_presentation_file(group_output.toUtf8().toStdString(), package.parts, {});
            passed = check(added.error == PresentationEditError::None &&
                             grouped.error == PresentationEditError::None &&
                             reordered.error == PresentationEditError::None &&
                             package.error == PresentationError::None &&
                             group_saved.error == PresentationError::None,
                         "write opt-in group layer interop package") &&
                passed;
        }
        return passed;
    }
}

int run_presentation_storage_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    return check(directory.isValid(), "create isolated test directory") && run_cases(directory.path()) ? 0
                                                                                                       : 1;
}

int main(int argc, char* argv[])
{
    return run_presentation_storage_tests(argc, argv);
}
