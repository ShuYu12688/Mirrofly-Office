#include "presentation_bridge.hpp"
#include "presentation_scene.hpp"
#include "slide_renderer.hpp"

#include <mirrorfly/presentation_storage.hpp>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>

namespace
{
    int run_edit_performance(int argc, char* argv[])
    {
        QGuiApplication application(argc, argv);
        const QString kind = application.arguments().value(2, "text");
        if (kind == "bridge-delete-burst")
        {
            if (argc < 4)
            {
                return 2;
            }
            mirrorfly::PresentationBridge bridge;
            const auto wait_for = [&](const auto& ready, int timeout)
            {
                QElapsedTimer wait;
                wait.start();
                while (!ready() && wait.elapsed() < timeout)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(2);
                }
                return ready();
            };
            if (!bridge.requestOpen(QUrl::fromLocalFile(application.arguments().at(1))) ||
                !wait_for(
                    [&bridge]()
            {
                return !bridge.busy();
            }, 120000) ||
                !bridge.active())
            {
                return 3;
            }
            bridge.finishLoadingFrame();
            QTemporaryDir directory;
            if (!directory.isValid())
            {
                return 3;
            }
            bridge.createEditableCopy();
            bridge.selectSaveFile(
                QUrl::fromLocalFile(directory.filePath(QStringLiteral("editable-copy.pptx"))));
            if (!wait_for(
                    [&bridge]()
            {
                return !bridge.busy();
            }, 120000) ||
                !bridge.editable())
            {
                return 3;
            }
            const int page = application.arguments().at(3).toInt() - 1;
            bridge.setSlide(page);
            auto document = bridge.document().value<mirrorfly::RenderPresentationPtr>();
            if (!document || !document->scene || page < 0 ||
                page >= static_cast<int>(document->scene->slides.size()))
            {
                return 3;
            }
            const QSize viewport(1280, 720);
            QImage base(viewport, QImage::Format_ARGB32_Premultiplied);
            base.fill(Qt::white);
            QPainter base_painter(&base);
            mirrorfly::paint_presentation_slide(
                base_painter, document, static_cast<std::size_t>(page), {}, base.rect());
            base_painter.end();
            mirrorfly::cache_presentation_frame(document, page, {}, viewport, base);
            mirrorfly::SlideRenderer renderer;
            renderer.setSize(viewport);
            renderer.setDeferredFrames(true);
            renderer.setDocument(bridge.document());
            renderer.setSlideIndex(page);
            double maximum_editor_state_ms = 0;
            // Mirror the live QML binding: document signals also refresh the selected text editor.
            QObject::connect(&renderer, &mirrorfly::SlideRenderer::textEditorChanged, &renderer, [&]()
            {
                QElapsedTimer timer;
                timer.start();
                renderer.textEditorState();
                maximum_editor_state_ms = std::max(maximum_editor_state_ms, timer.nsecsElapsed() / 1e6);
            });
            QObject::connect(&bridge, &mirrorfly::PresentationBridge::documentChanged, &renderer, [&]()
            {
                renderer.setDocument(bridge.document());
            });
            QObject::connect(&bridge, &mirrorfly::PresentationBridge::selectionChanged, &renderer, [&]()
            {
                renderer.setSelectedShape(bridge.selectedShape());
            });
            const auto original_fonts = document->font_loader;
            bool reused_fonts = true;
            QImage image(viewport, QImage::Format_ARGB32_Premultiplied);
            const auto paint = [&]()
            {
                image.fill(Qt::transparent);
                QPainter painter(&image);
                renderer.paint(&painter);
                painter.end();
            };
            paint();
            std::vector<double> edit_calls;
            std::vector<double> paints;
            for (int operation = 0; operation < 5; ++operation)
            {
                document = bridge.document().value<mirrorfly::RenderPresentationPtr>();
                const auto& shapes = document->scene->slides[page].shapes;
                const auto found = std::find_if(shapes.begin(), shapes.end(), [](const auto& shape)
                {
                    const auto actions = mirrorfly::presentation_edit_capabilities(shape);
                    return std::find(actions.begin(), actions.end(),
                               mirrorfly::PresentationEditAction::DeleteShape) != actions.end();
                });
                if (found == shapes.end())
                {
                    break;
                }
                const int index = static_cast<int>(found - shapes.begin());
                bridge.selectShape(index);
                renderer.setSelectedShape(index);
                QElapsedTimer timer;
                timer.start();
                if (!bridge.applyEdit(QStringLiteral("deleteShape")))
                {
                    return 4;
                }
                edit_calls.push_back(timer.nsecsElapsed() / 1e6);
                timer.restart();
                paint();
                paints.push_back(timer.nsecsElapsed() / 1e6);
                const auto edited_document = bridge.document().value<mirrorfly::RenderPresentationPtr>();
                reused_fonts = reused_fonts && edited_document->font_loader == original_fonts;
            }
            std::vector<double> drag_frames;
            document = bridge.document().value<mirrorfly::RenderPresentationPtr>();
            const auto& remaining = document->scene->slides[page].shapes;
            const auto draggable = std::find_if(remaining.begin(), remaining.end(), [](const auto& shape)
            {
                const auto actions = mirrorfly::presentation_edit_capabilities(shape);
                return std::find(actions.begin(), actions.end(),
                           mirrorfly::PresentationEditAction::TransformShape) != actions.end();
            });
            if (draggable != remaining.end())
            {
                const int index = static_cast<int>(draggable - remaining.begin());
                bridge.selectShape(index);
                renderer.setSelectedShape(index);
                for (int frame = 0; frame < 12; ++frame)
                {
                    const QVariantMap preview{{"id", QString::number(draggable->id)},
                        {"x", draggable->transform[4] + frame * 2}, {"y", draggable->transform[5] + frame},
                        {"width", draggable->width}, {"height", draggable->height}};
                    QElapsedTimer timer;
                    timer.start();
                    renderer.setTransformPreview(preview);
                    paint();
                    drag_frames.push_back(timer.nsecsElapsed() / 1e6);
                }
                renderer.setTransformPreview({});
            }
            const bool committed = wait_for([&bridge]()
            {
                return !bridge.syncing();
            }, 120000);
            document = bridge.document().value<mirrorfly::RenderPresentationPtr>();
            const bool frame_ready = wait_for([&]()
            {
                return !mirrorfly::cached_presentation_frame(document, page, {}, viewport).isNull();
            }, 120000);
            const auto maximum_edit =
                edit_calls.empty() ? 0 : *std::max_element(edit_calls.begin(), edit_calls.end());
            const auto maximum_paint = paints.empty() ? 0 : *std::max_element(paints.begin(), paints.end());
            const auto maximum_drag =
                drag_frames.empty() ? 0 : *std::max_element(drag_frames.begin(), drag_frames.end());
            QJsonArray report;
            report.append(QJsonObject{{"page", page + 1}, {"kind", kind},
                {"operations", static_cast<int>(edit_calls.size())}, {"maximumEditCallMs", maximum_edit},
                {"maximumDeletePaintMs", maximum_paint}, {"committed", committed},
                {"maximumDragFrameMs", maximum_drag}, {"frameReady", frame_ready},
                {"maximumEditorStateMs", maximum_editor_state_ms}, {"reusedFontSession", reused_fonts},
                {"underOneSecond",
                    maximum_edit < 1000 && maximum_paint < 1000 && maximum_drag < 1000 &&
                        maximum_editor_state_ms < 1000}});
            std::cout << QJsonDocument(report).toJson().constData() << std::flush;
            return committed && frame_ready && maximum_edit < 1000 && maximum_paint < 1000 &&
                    maximum_drag < 1000 && maximum_editor_state_ms < 1000 && reused_fonts
                ? 0
                : 5;
        }
        auto scene = std::make_shared<mirrorfly::PresentationScene>();
        if (argc > 1)
        {
            auto loaded = mirrorfly::load_presentation_file(application.arguments().at(1).toStdString());
            if (loaded.error != mirrorfly::PresentationError::None)
                return 2;
            *scene = std::move(loaded.scene);
        }
        else
        {
            *scene = mirrorfly::make_presentation(mirrorfly::PresentationSlideLayout::Blank);
            for (int index = 0; index < 120; ++index)
            {
                mirrorfly::PresentationEditCommand command;
                command.action = mirrorfly::PresentationEditAction::AddText;
                command.text = "Office performance / editing 123";
                command.x = (index % 10) * 66;
                command.y = (index / 10) * 38;
                command.width = 180;
                command.height = 48;
                if (mirrorfly::apply_presentation_edit(*scene, command).error !=
                    mirrorfly::PresentationEditError::None)
                    return 2;
            }
        }
        const bool context_delete_burst =
            kind == "context-delete-burst" || kind == "context-delete-images-burst";
        const bool context_delete =
            kind == "context-delete" || kind == "context-delete-images" || context_delete_burst;
        mirrorfly::PresentationPrepareOptions options;
        options.eager_image_analysis = false;
        const auto document = mirrorfly::prepare_presentation(scene, {}, options);
        QJsonArray report;
        if (kind == "font-selection")
        {
            const int page = application.arguments().value(3, "1").toInt() - 1;
            if (page < 0 || page >= static_cast<int>(scene->slides.size()) || !document->font_loader)
            {
                return 2;
            }
            mirrorfly::SlideRenderer renderer;
            renderer.setSize(QSizeF(1280, 720));
            renderer.setDocument(QVariant::fromValue(document));
            renderer.setSlideIndex(page);
            auto loading = std::async(std::launch::async, [document, page]()
            {
                QElapsedTimer timer;
                timer.start();
                mirrorfly::ensure_presentation_fonts(document, static_cast<std::size_t>(page));
                return timer.elapsed();
            });
            const auto finished = [&loading]()
            {
                return loading.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
            };
            QElapsedTimer timeline;
            timeline.start();
            double maximum_read_ms = 0;
            int valid_reads = 0;
            do
            {
                for (int index = 0; index < static_cast<int>(scene->slides[page].shapes.size()); ++index)
                {
                    renderer.setSelectedShape(index);
                    QElapsedTimer timer;
                    timer.start();
                    const auto state = renderer.textEditorState();
                    maximum_read_ms = std::max(maximum_read_ms, timer.nsecsElapsed() / 1e6);
                    if (state.value("valid").toBool())
                    {
                        ++valid_reads;
                    }
                }
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                QThread::msleep(2);
            } while (!finished() && timeline.elapsed() < 30000);
            const bool completed = finished();
            const qint64 load_ms = loading.get();
            report.append(QJsonObject{{"kind", kind}, {"page", page + 1}, {"fontLoadMs", load_ms},
                {"maximumEditorStateMs", maximum_read_ms}, {"validEditorReads", valid_reads},
                {"completed", completed}});
            std::cout << QJsonDocument(report).toJson().constData() << std::flush;
            return completed && valid_reads > 0 && maximum_read_ms < 100 ? 0 : 5;
        }
        if (kind == "thumbnails")
        {
            const int count = std::min(8, static_cast<int>(scene->slides.size()));
            std::vector<std::unique_ptr<mirrorfly::SlideThumbnailRenderer>> thumbnails;
            thumbnails.reserve(static_cast<std::size_t>(count));
            QElapsedTimer timeline;
            timeline.start();
            for (int page = 0; page < count; ++page)
            {
                auto thumbnail = std::make_unique<mirrorfly::SlideThumbnailRenderer>();
                thumbnail->setSize(QSizeF(128, 72));
                thumbnail->setDocument(QVariant::fromValue(document));
                thumbnail->setSlideIndex(page);
                thumbnails.push_back(std::move(thumbnail));
            }
            int ready = 0;
            qint64 first_ready = -1;
            qint64 maximum_gap = 0;
            qint64 previous_sample = timeline.elapsed();
            double maximum_foreground_ms = 0;
            QImage pixels(128, 72, QImage::Format_ARGB32_Premultiplied);
            do
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                const qint64 now = timeline.elapsed();
                maximum_gap = std::max(maximum_gap, now - previous_sample);
                previous_sample = now;
                ready = 0;
                for (int page = 0; page < count; ++page)
                {
                    if (!mirrorfly::cached_presentation_thumbnail(
                            document, mirrorfly::presentation_thumbnail_key(document, page, {}))
                            .isNull())
                    {
                        ++ready;
                    }
                    pixels.fill(Qt::transparent);
                    QPainter painter(&pixels);
                    QElapsedTimer paint_time;
                    paint_time.start();
                    thumbnails[static_cast<std::size_t>(page)]->paint(&painter);
                    painter.end();
                    maximum_foreground_ms = std::max(maximum_foreground_ms, paint_time.nsecsElapsed() / 1e6);
                }
                if (ready > 0 && first_ready < 0)
                {
                    first_ready = timeline.elapsed();
                }
                QThread::msleep(2);
            } while (ready < count && timeline.elapsed() < 20000);
            report.append(QJsonObject{{"kind", kind}, {"requested", count}, {"ready", ready},
                {"firstReadyMs", first_ready}, {"allReadyMs", timeline.elapsed()},
                {"foregroundMaxMs", maximum_foreground_ms}, {"maximumEventGapMs", maximum_gap},
                {"underOneSecond", maximum_foreground_ms < 1000}});
            std::cout << QJsonDocument(report).toJson().constData();
            return ready == count && maximum_foreground_ms < 1000 ? 0 : 3;
        }
        const std::vector<int> pages = argc > 3 ? std::vector<int>{application.arguments().at(3).toInt() - 1}
            : argc > 1                          ? std::vector<int>{0, 3, 13, 14, 15, 32, 35}
                                                : std::vector<int>{0};
        for (const int page : pages)
        {
            if (page >= static_cast<int>(scene->slides.size()))
                continue;
            const auto& shapes = scene->slides[page].shapes;
            const auto matches = [&kind, context_delete_burst](const auto& shape)
            {
                if (!shape.editable)
                    return false;
                if (kind == "context-delete-images-burst")
                    return !shape.image_path.empty() || !shape.fill.image_path.empty();
                if (kind == "delete" || kind == "context-delete" || context_delete_burst)
                    return true;
                if (kind == "delete-images" || kind == "context-delete-images")
                    return !shape.image_path.empty() || !shape.fill.image_path.empty();
                if (kind == "delete-timeline")
                    return !shape.image_path.empty() || !shape.fill.image_path.empty();
                if (kind == "delete-switch")
                    return !shape.image_path.empty() || !shape.fill.image_path.empty();
                if (kind == "images")
                    return !shape.image_path.empty() || !shape.fill.image_path.empty();
                if (kind == "switch")
                    return !shape.image_path.empty() || !shape.fill.image_path.empty();
                if (kind == "effects")
                {
                    for (const auto& paragraph : shape.text.paragraphs)
                        for (const auto& run : paragraph.runs)
                            if (!run.fill.stops.empty() || run.effects.shadow_opacity > 0 ||
                                run.effects.glow_opacity > 0 || run.effects.reflection_opacity > 0)
                                return true;
                    return false;
                }
                return !shape.text.paragraphs.empty();
            };
            for (auto chosen = shapes.begin(); chosen != shapes.end(); ++chosen)
            {
                if (!matches(*chosen))
                    continue;
                mirrorfly::SlideRenderer renderer;
                renderer.setSize(QSizeF(1280, 720));
                if (kind == "switch" || kind == "delete-timeline" || kind == "delete-switch" ||
                    context_delete)
                    renderer.setDeferredFrames(true);
                if (kind == "delete-timeline" || kind == "delete-switch")
                {
                    const QSize viewport(1280, 720);
                    QImage fallback(viewport, QImage::Format_ARGB32_Premultiplied);
                    fallback.fill(QColor(28, 36, 52));
                    mirrorfly::cache_presentation_frame(document, page, {}, viewport, fallback);
                    renderer.setDocument(QVariant::fromValue(document));
                    renderer.setSlideIndex(page);
                    int switch_target = -1;
                    qint64 thumbnail_ready = -1;
                    if (kind == "delete-switch")
                    {
                        switch_target = argc > 4
                            ? application.arguments().at(4).toInt() - 1
                            : std::min(page + 1, static_cast<int>(scene->slides.size()) - 1);
                        mirrorfly::SlideThumbnailRenderer thumbnail;
                        thumbnail.setSize(QSizeF(128, 72));
                        thumbnail.setDocument(QVariant::fromValue(document));
                        thumbnail.setSlideIndex(switch_target);
                        QElapsedTimer thumbnail_wait;
                        thumbnail_wait.start();
                        while (mirrorfly::cached_presentation_thumbnail(document,
                                   mirrorfly::presentation_thumbnail_key(document, switch_target, {}))
                                   .isNull() &&
                            thumbnail_wait.elapsed() < 10000)
                        {
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                            QThread::msleep(2);
                        }
                        thumbnail_ready = thumbnail_wait.elapsed();
                    }
                    auto deleted_scene = std::make_shared<mirrorfly::PresentationScene>(*scene);
                    deleted_scene->slides[page].shapes.erase(
                        deleted_scene->slides[page].shapes.begin() + (chosen - shapes.begin()));
                    mirrorfly::PresentationPrepareOptions deleted_options;
                    deleted_options.eager_image_analysis = false;
                    deleted_options.thumbnail_revisions = document->thumbnail_revisions;
                    deleted_options.thumbnail_revisions[static_cast<std::size_t>(page)] = 0;
                    deleted_options.edited_slide = page;
                    deleted_options.edited_shape = static_cast<int>(chosen - shapes.begin());
                    deleted_options.edit_layer_change = mirrorfly::PresentationEditLayerChange::Remove;
                    QElapsedTimer timer;
                    timer.start();
                    const auto deleted =
                        mirrorfly::prepare_presentation(deleted_scene, document, deleted_options);
                    const auto preparation = timer.nsecsElapsed() / 1e6;
                    renderer.setDocument(QVariant::fromValue(deleted));
                    renderer.setSelectedShape(-1);
                    if (kind == "delete-switch")
                    {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                        QElapsedTimer switch_time;
                        switch_time.start();
                        renderer.setSlideIndex(switch_target);
                        const auto switch_call = switch_time.nsecsElapsed() / 1e6;
                        QImage target_pixels(viewport, QImage::Format_ARGB32_Premultiplied);
                        target_pixels.fill(Qt::transparent);
                        QPainter target_painter(&target_pixels);
                        timer.restart();
                        renderer.paint(&target_painter);
                        target_painter.end();
                        const auto first_paint = timer.nsecsElapsed() / 1e6;
                        QElapsedTimer target_wait;
                        target_wait.start();
                        while (mirrorfly::cached_presentation_frame(deleted, switch_target, {}, viewport)
                                   .isNull() &&
                            target_wait.elapsed() < 20000)
                        {
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                            QThread::msleep(2);
                        }
                        const bool ready =
                            !mirrorfly::cached_presentation_frame(deleted, switch_target, {}, viewport)
                                 .isNull();
                        report.append(QJsonObject{{"page", page + 1}, {"targetPage", switch_target + 1},
                            {"kind", kind}, {"shapes", static_cast<int>(shapes.size())},
                            {"index", static_cast<int>(chosen - shapes.begin())},
                            {"name", QString::fromStdString(chosen->name)}, {"deletePrepareMs", preparation},
                            {"thumbnailReadyMs", thumbnail_ready}, {"switchCallMs", switch_call},
                            {"switchFirstPaintMs", first_paint}, {"targetReadyMs", target_wait.elapsed()},
                            {"ready", ready}, {"underOneSecond", first_paint < 1000}});
                        break;
                    }
                    QImage image(viewport, QImage::Format_ARGB32_Premultiplied);
                    std::vector<double> durations;
                    QElapsedTimer timeline;
                    timeline.start();
                    qint64 previous_sample = timeline.elapsed();
                    qint64 maximum_gap = 0;
                    do
                    {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                        const qint64 now = timeline.elapsed();
                        maximum_gap = std::max(maximum_gap, now - previous_sample);
                        previous_sample = now;
                        image.fill(Qt::transparent);
                        QPainter painter(&image);
                        timer.restart();
                        renderer.paint(&painter);
                        painter.end();
                        durations.push_back(timer.nsecsElapsed() / 1e6);
                        QThread::msleep(2);
                    } while (mirrorfly::cached_presentation_frame(deleted, page, {}, viewport).isNull() &&
                        timeline.elapsed() < 15000);
                    std::sort(durations.begin(), durations.end());
                    const double maximum = durations.empty() ? 0 : durations.back();
                    const double p95 = durations.empty()
                        ? 0
                        : durations[std::min(
                              durations.size() - 1, static_cast<std::size_t>(durations.size() * 95 / 100))];
                    const bool ready =
                        !mirrorfly::cached_presentation_frame(deleted, page, {}, viewport).isNull();
                    report.append(QJsonObject{{"page", page + 1}, {"kind", kind},
                        {"shapes", static_cast<int>(shapes.size())},
                        {"index", static_cast<int>(chosen - shapes.begin())},
                        {"name", QString::fromStdString(chosen->name)}, {"deletePrepareMs", preparation},
                        {"samples", static_cast<int>(durations.size())}, {"foregroundMaxMs", maximum},
                        {"foregroundP95Ms", p95}, {"maximumEventGapMs", maximum_gap},
                        {"backgroundMs", timeline.elapsed()}, {"ready", ready},
                        {"underOneSecond", maximum < 1000}});
                    break;
                }
                if (context_delete)
                {
                    QImage base(1280, 720, QImage::Format_ARGB32_Premultiplied);
                    base.fill(Qt::white);
                    QPainter base_painter(&base);
                    base_painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                        QPainter::SmoothPixmapTransform);
                    mirrorfly::paint_presentation_slide(
                        base_painter, document, static_cast<std::size_t>(page), {}, base.rect());
                    base_painter.end();
                    mirrorfly::cache_presentation_frame(document, page, {}, base.size(), base);
                }
                renderer.setDocument(QVariant::fromValue(document));
                renderer.setSlideIndex(page);
                renderer.setMediaEnabled(true);
                QElapsedTimer selection_timer;
                selection_timer.start();
                renderer.setSelectedShape(kind == "switch" ? -1 : static_cast<int>(chosen - shapes.begin()));
                const auto selection = selection_timer.nsecsElapsed() / 1e6;
                QImage image(1280, 720, QImage::Format_ARGB32_Premultiplied);
                const auto paint = [&]()
                {
                    image.fill(Qt::transparent);
                    QPainter painter(&image);
                    renderer.paint(&painter);
                };
                QElapsedTimer timer;
                timer.start();
                paint();
                const auto first = timer.nsecsElapsed() / 1e6;
                if (context_delete_burst)
                {
                    auto current_scene = std::make_shared<mirrorfly::PresentationScene>(*scene);
                    auto current_document = document;
                    std::vector<double> delete_paints;
                    std::vector<double> preparations;
                    for (int operation = 0; operation < 5; ++operation)
                    {
                        const auto& current_shapes = current_scene->slides[page].shapes;
                        const auto deletable = std::find_if(current_shapes.begin(), current_shapes.end(),
                            [&kind](const auto& shape)
                        {
                            const auto actions = mirrorfly::presentation_edit_capabilities(shape);
                            const bool supports_delete =
                                std::find(actions.begin(), actions.end(),
                                    mirrorfly::PresentationEditAction::DeleteShape) != actions.end();
                            const bool matches_asset = kind != "context-delete-images-burst" ||
                                !shape.image_path.empty() || !shape.fill.image_path.empty();
                            return supports_delete && matches_asset;
                        });
                        if (deletable == current_shapes.end())
                        {
                            break;
                        }
                        const int index = static_cast<int>(deletable - current_shapes.begin());
                        renderer.setSelectedShape(index);
                        auto next_scene = std::make_shared<mirrorfly::PresentationScene>(*current_scene);
                        next_scene->slides[page].shapes.erase(
                            next_scene->slides[page].shapes.begin() + index);
                        mirrorfly::PresentationPrepareOptions next_options;
                        next_options.eager_image_analysis = false;
                        next_options.thumbnail_revisions = current_document->thumbnail_revisions;
                        next_options.thumbnail_revisions[static_cast<std::size_t>(page)] = 0;
                        next_options.edited_slide = page;
                        next_options.edited_shape = index;
                        next_options.edit_layer_change = mirrorfly::PresentationEditLayerChange::Remove;
                        timer.restart();
                        const auto next_document =
                            mirrorfly::prepare_presentation(next_scene, current_document, next_options);
                        preparations.push_back(timer.nsecsElapsed() / 1e6);
                        renderer.setDocument(QVariant::fromValue(next_document));
                        renderer.setSelectedShape(-1);
                        timer.restart();
                        paint();
                        delete_paints.push_back(timer.nsecsElapsed() / 1e6);
                        current_scene = std::move(next_scene);
                        current_document = next_document;
                    }
                    QElapsedTimer wait;
                    wait.start();
                    const QSize viewport(1280, 720);
                    while (
                        mirrorfly::cached_presentation_frame(current_document, page, {}, viewport).isNull() &&
                        wait.elapsed() < 15000)
                    {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                        QThread::msleep(2);
                    }
                    const auto maximum_paint = delete_paints.empty()
                        ? 0
                        : *std::max_element(delete_paints.begin(), delete_paints.end());
                    const auto maximum_preparation = preparations.empty()
                        ? 0
                        : *std::max_element(preparations.begin(), preparations.end());
                    report.append(QJsonObject{{"page", page + 1}, {"kind", kind},
                        {"operations", static_cast<int>(delete_paints.size())}, {"firstMs", first},
                        {"maximumDeletePaintMs", maximum_paint}, {"maximumPrepareMs", maximum_preparation},
                        {"backgroundMs", wait.elapsed()},
                        {"ready",
                            !mirrorfly::cached_presentation_frame(current_document, page, {}, viewport)
                                .isNull()},
                        {"underOneSecond", maximum_paint < 1000 && maximum_preparation < 1000}});
                    break;
                }
                if (kind == "switch")
                {
                    QElapsedTimer wait;
                    wait.start();
                    const QSize viewport(1280, 720);
                    while (mirrorfly::cached_presentation_frame(document, page, {}, viewport).isNull() &&
                        wait.elapsed() < 15000)
                    {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                        QThread::msleep(2);
                    }
                    timer.restart();
                    paint();
                    const auto cached_paint = timer.nsecsElapsed() / 1e6;
                    report.append(QJsonObject{{"page", page + 1}, {"kind", kind},
                        {"shapes", static_cast<int>(shapes.size())}, {"firstMs", first},
                        {"backgroundMs", wait.elapsed()}, {"cachedPaintMs", cached_paint},
                        {"ready",
                            !mirrorfly::cached_presentation_frame(document, page, {}, viewport).isNull()}});
                    break;
                }
                if (kind == "delete" || kind == "delete-images" || context_delete)
                {
                    timer.restart();
                    auto transacted_scene = std::make_shared<mirrorfly::PresentationScene>(*scene);
                    transacted_scene->native_editable = true;
                    mirrorfly::PresentationEditCommand transaction_command;
                    transaction_command.action = mirrorfly::PresentationEditAction::DeleteShape;
                    transaction_command.slide_index = static_cast<std::size_t>(page);
                    transaction_command.shape_index = static_cast<std::size_t>(chosen - shapes.begin());
                    const auto transaction_result =
                        mirrorfly::apply_presentation_edit(*transacted_scene, transaction_command);
                    const auto transaction = timer.nsecsElapsed() / 1e6;
                    timer.restart();
                    auto deleted_scene = std::make_shared<mirrorfly::PresentationScene>(*scene);
                    deleted_scene->slides[page].shapes.erase(
                        deleted_scene->slides[page].shapes.begin() + (chosen - shapes.begin()));
                    mirrorfly::PresentationPrepareOptions deleted_options;
                    deleted_options.eager_image_analysis = false;
                    deleted_options.edited_slide = page;
                    deleted_options.edited_shape = static_cast<int>(chosen - shapes.begin());
                    deleted_options.edit_layer_change = mirrorfly::PresentationEditLayerChange::Remove;
                    const auto deleted =
                        mirrorfly::prepare_presentation(deleted_scene, document, deleted_options);
                    const auto preparation = timer.nsecsElapsed() / 1e6;
                    renderer.setDocument(QVariant::fromValue(deleted));
                    renderer.setSelectedShape(-1);
                    timer.restart();
                    paint();
                    const auto delete_paint = timer.nsecsElapsed() / 1e6;
                    double uncached_delete = -1;
                    if (context_delete)
                    {
                        QElapsedTimer wait;
                        wait.start();
                        const QSize viewport(1280, 720);
                        while (mirrorfly::cached_presentation_frame(deleted, page, {}, viewport).isNull() &&
                            wait.elapsed() < 15000)
                        {
                            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                            QThread::msleep(2);
                        }
                    }
                    else
                    {
                        renderer.setDocument(QVariant::fromValue(
                            mirrorfly::prepare_presentation(deleted_scene, {}, deleted_options)));
                        timer.restart();
                        paint();
                        uncached_delete = timer.nsecsElapsed() / 1e6;
                    }
                    report.append(QJsonObject{{"page", page + 1}, {"kind", kind},
                        {"shapes", static_cast<int>(shapes.size())},
                        {"index", static_cast<int>(chosen - shapes.begin())},
                        {"name", QString::fromStdString(chosen->name)}, {"firstMs", first},
                        {"selectionMs", selection}, {"transactionMs", transaction},
                        {"transactionOk", transaction_result.error == mirrorfly::PresentationEditError::None},
                        {"deletePrepareMs", preparation}, {"deletePaintMs", delete_paint},
                        {"uncachedDeletePaintMs", uncached_delete},
                        {"underOneSecond", selection < 1000 && first < 1000 && delete_paint < 1000}});
                    break;
                }
                std::vector<double> durations;
                for (int frame = 0; frame < 30; ++frame)
                {
                    renderer.setTransformPreview({{"id", QString::number(chosen->id)},
                        {"x", chosen->transform[4] + frame}, {"y", chosen->transform[5] + frame / 2.0},
                        {"width", chosen->width}, {"height", chosen->height}});
                    timer.restart();
                    paint();
                    durations.push_back(timer.nsecsElapsed() / 1e6);
                }
                std::sort(durations.begin(), durations.end());
                timer.restart();
                auto committed_scene = std::make_shared<mirrorfly::PresentationScene>(*scene);
                committed_scene->slides[page].shapes[chosen - shapes.begin()].transform[4] += 30;
                mirrorfly::PresentationPrepareOptions committed_options;
                committed_options.reuse_analysis = true;
                committed_options.edited_slide = page;
                committed_options.edited_shape = static_cast<int>(chosen - shapes.begin());
                const auto committed =
                    mirrorfly::prepare_presentation(committed_scene, document, committed_options);
                const auto preparation = timer.nsecsElapsed() / 1e6;
                renderer.setDocument(QVariant::fromValue(committed));
                renderer.setTransformPreview({});
                timer.restart();
                paint();
                const auto commit_paint = timer.nsecsElapsed() / 1e6;
                committed_options.edited_shape = -1;
                renderer.setDocument(QVariant::fromValue(
                    mirrorfly::prepare_presentation(committed_scene, committed, committed_options)));
                timer.restart();
                paint();
                const auto uncached_commit = timer.nsecsElapsed() / 1e6;
                report.append(QJsonObject{{"page", page + 1}, {"kind", kind},
                    {"shapes", static_cast<int>(shapes.size())},
                    {"index", static_cast<int>(chosen - shapes.begin())},
                    {"name", QString::fromStdString(chosen->name)},
                    {"asset", QString::fromStdString(chosen->image_path)}, {"firstMs", first},
                    {"dragMedianMs", durations[15]}, {"dragP95Ms", durations[28]},
                    {"commitPrepareMs", preparation}, {"commitPaintMs", commit_paint},
                    {"uncachedCommitPaintMs", uncached_commit}});
            }
        }
        std::cout << QJsonDocument(report).toJson().constData();
        return report.isEmpty() ? 1 : 0;
    }
}

int main(int argc, char* argv[])
{
    return run_edit_performance(argc, argv);
}
