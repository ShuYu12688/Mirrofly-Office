#include "presentation_scene.hpp"

#include <mirrorfly/presentation_storage.hpp>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>

namespace
{
    QJsonArray warnings(const std::vector<std::string>& source)
    {
        QJsonArray result;
        for (const auto& text : source)
            result.append(QString::fromStdString(text));
        return result;
    }

    int run_corpus_probe(int argc, char* argv[])
    {
#ifdef Q_OS_WIN
        qputenv("QT_QPA_PLATFORM", "windows");
#else
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
        QGuiApplication application(argc, argv);
        const auto arguments = application.arguments();
        const bool edit_probe = arguments.size() == 4 && arguments[3] == "--edit-style";
        const bool structure_probe = arguments.size() == 4 && arguments[3] == "--edit-structure";
        if (arguments.size() != 3 && !edit_probe && !structure_probe)
        {
            std::cerr << "Usage: mirrorfly_presentation_corpus_probe input-file-or-directory "
                         "output-directory [--edit-style|--edit-structure]\n";
            return 2;
        }
        const QFileInfo input_information(arguments[1]);
        const QDir input(input_information.isDir() ? input_information.absoluteFilePath()
                                                   : input_information.absolutePath());
        const QDir output(arguments[2]);
        if (!input_information.exists() || !QDir().mkpath(output.absolutePath()))
            return 2;
        QStringList files;
        if (input_information.isFile())
        {
            const auto suffix = input_information.suffix();
            if (suffix.compare(QStringLiteral("pptx"), Qt::CaseInsensitive) != 0 &&
                suffix.compare(QStringLiteral("potx"), Qt::CaseInsensitive) != 0)
                return 2;
            files.append(input_information.absoluteFilePath());
        }
        else
        {
            QDirIterator entries(
                input.absolutePath(), {"*.pptx", "*.potx"}, QDir::Files, QDirIterator::Subdirectories);
            while (entries.hasNext())
                files.append(entries.next());
        }
        files.sort();
        if (files.isEmpty() || files.size() > 256)
            return 2;
        QJsonArray report;
        int failures = 0;
        const auto environment = mirrorfly::presentation_render_environment();
        for (const auto& path : files)
        {
            QElapsedTimer total_clock;
            total_clock.start();
            QFile source(path);
            if (!source.open(QIODevice::ReadOnly))
            {
                ++failures;
                continue;
            }
            QCryptographicHash source_hash(QCryptographicHash::Sha256);
            QByteArray hash_buffer(1024 * 1024, Qt::Uninitialized);
            while (!source.atEnd())
            {
                const qint64 read = source.read(hash_buffer.data(), hash_buffer.size());
                if (read <= 0)
                    return 2;
                source_hash.addData(QByteArrayView(hash_buffer.constData(), read));
            }
            if (source.error() != QFileDevice::NoError)
                return 2;
            const auto hash = source_hash.result().toHex();
            const auto name = QFileInfo(path).completeBaseName() + "-" + QString::fromLatin1(hash.left(12));
            QDir destination(output.filePath(name));
            if (!QDir().mkpath(destination.absolutePath()))
                return 2;
            QTemporaryDir temporary;
            QString load_path = path;
            if (QFileInfo(path).suffix().compare(QStringLiteral("potx"), Qt::CaseInsensitive) == 0)
            {
                if (!temporary.isValid())
                    return 2;
                load_path = temporary.filePath("input.pptx");
                if (!QFile::copy(path, load_path))
                    return 2;
            }
            QElapsedTimer parse_clock;
            parse_clock.start();
            auto parsed = mirrorfly::load_presentation_file(load_path.toUtf8().toStdString());
            const auto parse_ms = parse_clock.elapsed();
            QJsonObject entry{{"source", input.relativeFilePath(path)}, {"sha256", QString::fromLatin1(hash)},
                {"directory", name}, {"error", static_cast<int>(parsed.error)},
                {"message", QString::fromStdString(parsed.message)}, {"parseMs", parse_ms},
                {"archiveBytes", QFileInfo(path).size()}};
            if (parsed.error != mirrorfly::PresentationError::None)
            {
                ++failures;
                report.append(entry);
                continue;
            }
            auto scene = std::make_shared<mirrorfly::PresentationScene>(std::move(parsed.scene));
            QElapsedTimer prepare_clock;
            prepare_clock.start();
            mirrorfly::PresentationPrepareOptions prepare_options;
            prepare_options.environment = environment;
            prepare_options.eager_image_analysis = false;
            const auto document = mirrorfly::prepare_presentation(scene, {}, prepare_options);
            const auto prepare_ms = prepare_clock.elapsed();
            entry["prepareMs"] = prepare_ms;
            entry["readyMs"] = parse_ms + prepare_ms;
            entry["width"] = scene->width;
            entry["height"] = scene->height;
            entry["fonts"] = document->font_summary;
            entry["warnings"] = warnings(scene->warnings);
            QJsonArray slides;
            const double factor = std::min(1280.0 / scene->width, 960.0 / scene->height);
            const QSize size(
                std::max(1, qRound(scene->width * factor)), std::max(1, qRound(scene->height * factor)));
            for (std::size_t index = 0; index < scene->slides.size(); ++index)
            {
                const auto& slide = scene->slides[index];
                QImage image(size, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::white);
                QPainter painter(&image);
                painter.setRenderHints(
                    QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
                mirrorfly::paint_presentation_slide(
                    painter, document, index, {}, QRectF(QPointF(0, 0), size));
                painter.end();
                const auto image_name =
                    QStringLiteral("slide-%1.png").arg(index + 1, 3, 10, QLatin1Char('0'));
                if (!image.save(destination.filePath(image_name)))
                    return 2;
                QJsonArray objects;
                for (const auto& shape : slide.shapes)
                {
                    if (objects.size() >= 128)
                        break;
                    QJsonObject object{{"id", QString::fromStdString(shape.source_id)},
                        {"name", QString::fromStdString(shape.name)}, {"width", shape.width},
                        {"height", shape.height}, {"x", shape.transform[4]}, {"y", shape.transform[5]},
                        {"scaleX", shape.transform[0]}, {"scaleY", shape.transform[3]},
                        {"fill", QString::fromStdString(shape.fill.color)},
                        {"fontScale", shape.text.font_scale}};
                    if (!shape.text.paragraphs.empty() && !shape.text.paragraphs[0].runs.empty())
                    {
                        const auto& run = shape.text.paragraphs[0].runs[0];
                        object["font"] = QString::fromStdString(run.font_family);
                        object["fontSize"] = run.font_size;
                        object["textColor"] = QString::fromStdString(run.color);
                    }
                    objects.append(object);
                }
                slides.append(QJsonObject{{"image", image_name},
                    {"title", QString::fromStdString(slide.title)},
                    {"shapes", static_cast<int>(slide.shapes.size())}, {"warnings", warnings(slide.warnings)},
                    {"animations", static_cast<int>(slide.animations.size())},
                    {"mediaCues", static_cast<int>(slide.media_cues.size())},
                    {"background", QString::fromStdString(slide.background.color)}, {"objects", objects}});
            }
            entry["slides"] = slides;
            scene->native_editable = true;
            const auto serialized = mirrorfly::serialize_presentation(*scene);
            const auto saved = mirrorfly::save_presentation_file(
                destination.filePath("roundtrip.pptx").toUtf8().toStdString(), serialized.parts, {});
            entry["saveError"] = static_cast<int>(
                serialized.error != mirrorfly::PresentationError::None ? serialized.error : saved.error);
            if (serialized.error != mirrorfly::PresentationError::None ||
                saved.error != mirrorfly::PresentationError::None)
                ++failures;
            if (edit_probe)
            {
                const auto& shapes = scene->slides.front().shapes;
                const auto found = std::find_if(shapes.begin(), shapes.end(), [](const auto& shape)
                {
                    return shape.editable && !shape.text.paragraphs.empty();
                });
                if (found == shapes.end())
                    return 2;
                mirrorfly::PresentationEditCommand command;
                command.action = mirrorfly::PresentationEditAction::FormatTextStyle;
                command.shape_index = static_cast<std::size_t>(found - shapes.begin());
                command.text_style.shadow = mirrorfly::PresentationShadowStyle{"#224466", 0.4, 2, 1, 2};
                const auto edited = mirrorfly::apply_presentation_edit(*scene, command);
                const auto edited_package = mirrorfly::serialize_presentation(*scene);
                const auto edited_save = mirrorfly::save_presentation_file(
                    destination.filePath("style-edit.pptx").toUtf8().toStdString(), edited_package.parts, {});
                entry["editError"] = static_cast<int>(edited.error);
                entry["editSaveError"] = static_cast<int>(edited_save.error);
                if (edited.error != mirrorfly::PresentationEditError::None ||
                    edited_package.error != mirrorfly::PresentationError::None ||
                    edited_save.error != mirrorfly::PresentationError::None)
                    ++failures;
            }
            if (structure_probe)
            {
                QJsonArray changes;
                bool group_done = false;
                bool cell_done = false;
                for (std::size_t page = 0; page < scene->slides.size(); ++page)
                    for (std::size_t index = 0; index < scene->slides[page].shapes.size(); ++index)
                    {
                        const auto original = scene->slides[page].shapes[index];
                        const bool cell = original.table_cell.has_value();
                        if (!original.editable ||
                            (cell ? cell_done : group_done || original.source_groups.empty()))
                            continue;
                        mirrorfly::PresentationEditCommand command;
                        command.slide_index = page;
                        command.shape_index = index;
                        command.action = cell ? mirrorfly::PresentationEditAction::FormatText
                                              : mirrorfly::PresentationEditAction::TransformShape;
                        if (cell)
                            command.text_color = "#13579B";
                        else
                        {
                            command.x = original.transform[4] + 3;
                            command.y = original.transform[5] + 2;
                        }
                        const auto result = mirrorfly::apply_presentation_edit(*scene, command);
                        if (result.error != mirrorfly::PresentationEditError::None)
                        {
                            std::cerr << result.message << '\n';
                            ++failures;
                            continue;
                        }
                        changes.append(QJsonObject{{"page", static_cast<int>(page + 1)},
                            {"index", static_cast<int>(index)}, {"cell", cell},
                            {"sourceId", QString::fromStdString(original.source_id)},
                            {"part", QString::fromStdString(original.source_part)}});
                        if (cell)
                            cell_done = true;
                        else
                            group_done = true;
                    }
                const auto package = mirrorfly::serialize_presentation(*scene);
                const auto result = mirrorfly::save_presentation_file(
                    destination.filePath("structure-edit.pptx").toUtf8().toStdString(), package.parts, {});
                auto reopened = mirrorfly::parse_presentation(package.parts);
                if (!group_done || !cell_done || result.error != mirrorfly::PresentationError::None ||
                    reopened.error != mirrorfly::PresentationError::None)
                    ++failures;
                else
                    for (const auto& value : changes)
                    {
                        const auto change = value.toObject();
                        const auto page = change["page"].toInt() - 1;
                        const auto index = change["index"].toInt();
                        const auto& expected = scene->slides[page].shapes[index];
                        const auto& actual = reopened.scene.slides[page].shapes[index];
                        const auto& parent = expected.source_parent_transform;
                        // Some producers store group children in tiny integer coordinate grids.
                        const double tolerance = 0.002 +
                            std::max(std::abs(parent[0]) + std::abs(parent[2]),
                                std::abs(parent[1]) + std::abs(parent[3])) /
                                12700;
                        for (std::size_t element = 0; element < 6; ++element)
                            if (std::abs(expected.transform[element] - actual.transform[element]) > tolerance)
                            {
                                std::cerr << "Transform mismatch page " << page + 1 << " source "
                                          << expected.source_id << " element " << element << " expected "
                                          << expected.transform[element] << " actual "
                                          << actual.transform[element] << '\n';
                                ++failures;
                            }
                        if (change["cell"].toBool() &&
                            actual.text.paragraphs.front().runs.front().color != "#13579B")
                            ++failures;
                    }
                entry["structureChanges"] = changes;
            }
            entry["totalMs"] = total_clock.elapsed();
            report.append(entry);
        }
        QFile summary(output.filePath("report.json"));
        if (!summary.open(QIODevice::WriteOnly) || summary.write(QJsonDocument(report).toJson()) < 0)
            return 2;
        std::cout << "Examined " << files.size() << " files; " << failures << " load/save failures.\n";
        return failures ? 1 : 0;
    }
}

int main(int argc, char* argv[])
{
    return run_corpus_probe(argc, argv);
}
