#include "presentation_bridge.hpp"
#include "presentation_edit_layers.hpp"
#include "presentation_render_scheduler.hpp"
#include "presentation_semantics.hpp"
#include "presentation_table_fixture.hpp"
#include "presentation_text_renderer.hpp"
#include "presentation_text_style_adapter.hpp"
#include "slide_renderer.hpp"

#include <mirrorfly/presentation_storage.hpp>

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QThreadPool>

#include <iostream>

namespace
{
    using namespace mirrorfly;
    int failures = 0;

    void check(bool value, const char* label)
    {
        if (!value)
        {
            std::cerr << label << '\n';
            ++failures;
        }
    }

    PresentationScene fixture()
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.width = 320;
        scene.height = 180;
        for (int index = 0; index < 3; ++index)
        {
            PresentationEditCommand add;
            add.action = PresentationEditAction::AddShape;
            add.geometry = "rect";
            add.x = index == 1 ? -20 : 30 + index * 20;
            add.y = 35;
            add.width = 100;
            add.height = 70;
            check(apply_presentation_edit(scene, add).error == PresentationEditError::None,
                "make paint fixture");
            auto& shape = scene.slides.front().shapes.back();
            shape.fill = PresentationFill{index == 0 ? "#FF0000" : index == 1 ? "#00AA00" : "#0000FF", 0.6};
            shape.outline_color.clear();
        }
        return scene;
    }

    void test_thumbnail_revisions()
    {
        PresentationBridge bridge;
        bridge.requestNew();
        check(bridge.applyEdit("addSlide", {{"layout", "blank"}}), "add thumbnail page two");
        check(bridge.applyEdit("addSlide", {{"layout", "blank"}}), "add thumbnail page three");
        bridge.setSlide(1);
        const auto before = bridge.document().value<RenderPresentationPtr>();
        const auto versions = before->thumbnail_revisions;
        QImage marker(24, 14, QImage::Format_ARGB32_Premultiplied);
        marker.fill(Qt::green);
        for (int slide = 0; slide < 3; ++slide)
        {
            cache_presentation_thumbnail(before, presentation_thumbnail_key(before, slide, {}), marker);
            cache_presentation_frame(before, slide, {}, marker.size(), marker);
        }
        SlideThumbnailRenderer item;
        item.setSize(QSizeF(24, 14));
        item.setDocument(bridge.document());
        check(bridge.applyEdit("background", {{"color", "#123456"}}), "edit one thumbnail page");
        auto current = bridge.document().value<RenderPresentationPtr>();
        check(current->thumbnail_revisions[0] == versions[0] &&
                current->thumbnail_revisions[1] != versions[1] &&
                current->thumbnail_revisions[2] == versions[2],
            "only edited page changes visual revision");
        check(current->thumbnail_cache == before->thumbnail_cache, "edits share one bounded thumbnail cache");
        check(current->frame_cache == before->frame_cache &&
                cached_presentation_frame(current, 0, {}, marker.size()) == marker &&
                cached_presentation_frame(current, 1, {}, marker.size()).isNull(),
            "edits share bounded frames without exposing stale pixels for the changed page");
        check(cached_presentation_thumbnail(current, presentation_thumbnail_key(current, 0, {})) == marker &&
                cached_presentation_thumbnail(current, presentation_thumbnail_key(current, 1, {})).isNull(),
            "unaffected page keeps cached pixels and changed page cannot hit stale entry");
        item.setDocument(bridge.document());
        QImage pixels(marker.size(), marker.format());
        QPainter painter(&pixels);
        item.paint(&painter);
        painter.end();
        check(pixels == marker, "unaffected thumbnail never clears on document notification");
        const auto edited = current->thumbnail_revisions;
        bridge.undo();
        check(bridge.document().value<RenderPresentationPtr>()->thumbnail_revisions == versions,
            "undo restores thumbnail revisions");
        bridge.redo();
        check(bridge.document().value<RenderPresentationPtr>()->thumbnail_revisions == edited,
            "redo restores thumbnail revisions");
        bridge.setSlide(1);
        check(bridge.applyEdit("moveSlide", {{"offset", 1}}), "move cached page");
        current = bridge.document().value<RenderPresentationPtr>();
        check(current->thumbnail_revisions == std::vector<std::uint64_t>{edited[0], edited[2], edited[1]},
            "thumbnail revisions follow reordered pages");
        check(bridge.applyEdit("duplicateSlide"), "duplicate cached page");
        current = bridge.document().value<RenderPresentationPtr>();
        check(current->thumbnail_revisions.size() == 4 && current->thumbnail_revisions[0] == edited[0] &&
                current->thumbnail_revisions[2] == edited[1] && current->thumbnail_revisions[3] != edited[1],
            "duplicate receives independent revision without invalidating siblings");
        check(bridge.applyEdit("deleteSlide"), "delete duplicated page");
        current = bridge.document().value<RenderPresentationPtr>();
        check(current->thumbnail_revisions.size() == 3 && current->thumbnail_revisions[2] == edited[1],
            "delete preserves surviving revisions");
        check(presentation_thumbnail_key(current, 0, {}) !=
                presentation_thumbnail_key(current, 0, {{"textColor", "#FF0000"}}),
            "theme change invalidates thumbnail key");
        const auto invalid_before = current->thumbnail_revisions;
        check(!bridge.applyEdit("background", {{"color", "bad color"}}), "invalid edit rejected");
        check(bridge.document().value<RenderPresentationPtr>()->thumbnail_revisions == invalid_before,
            "rejected edit preserves cache identity");
    }

    void test_cache_contract()
    {
        const auto scene = std::make_shared<PresentationScene>(fixture());
        auto document = prepare_presentation(scene);
        PresentationEditLayers layers;
        QImage image(320, 180, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        int cached_objects = 0;
        int selected_objects = 0;
        int backgrounds = 0;
        const auto draw = [&](QPainter& target, int first, int last, bool background)
        {
            if (background)
            {
                ++backgrounds;
                target.fillRect(image.rect(), Qt::white);
            }
            for (int index = first; index < last; ++index)
            {
                if (index == 1)
                    ++selected_objects;
                else
                    ++cached_objects;
            }
        };
        for (int frame = 0; frame < 50; ++frame)
            check(layers.paint(painter, document, 0, 1, -1, {}, image.size(), draw), "paint layer cache");
        check(cached_objects == 2 && backgrounds == 1 && selected_objects == 50,
            "drag repaints selected object only; static layers painted once");
        PresentationPrepareOptions local;
        local.reuse_analysis = true;
        local.edited_slide = 0;
        local.edited_shape = 1;
        document = prepare_presentation(std::make_shared<PresentationScene>(*scene), document, local);
        layers.paint(painter, document, 0, 1, -1, {}, image.size(), draw);
        check(cached_objects == 2 && backgrounds == 1 && selected_objects == 51,
            "committing the selected object retains both static layers across immutable documents");
        document = prepare_presentation(std::make_shared<PresentationScene>(*scene));
        layers.paint(painter, document, 0, 1, -1, {}, image.size(), draw);
        check(cached_objects == 4 && backgrounds == 2, "new immutable document invalidates layers");
        layers.paint(painter, document, 0, 1, 1, {}, image.size(), draw);
        layers.paint(painter, document, 0, 1, 1, {{"accent", "#FF0000"}}, image.size(), draw);
        layers.paint(painter, document, 0, 1, 1, {}, QSize(200, 180), draw);
        check(backgrounds == 5, "text editing, theme and viewport each invalidate layers");
        layers.paint(painter, document, 0, -1, -1, {}, image.size(), draw);
        check(backgrounds == 6, "deselect invalidates selected gap");

        PresentationEditLayers deletion_layers;
        auto deletion_document = prepare_presentation(scene);
        cached_objects = 0;
        selected_objects = 0;
        backgrounds = 0;
        check(!deletion_layers.paint(painter, deletion_document, 0, 1, -1, {}, image.size(), draw, false) &&
                cached_objects == 0 && selected_objects == 0 && backgrounds == 0,
            "cold edit layers can decline a synchronous rebuild without invoking the painter");
        deletion_layers.paint(painter, deletion_document, 0, 1, -1, {}, image.size(), draw);
        const auto before_delete_cached = cached_objects;
        const auto before_delete_backgrounds = backgrounds;
        auto without_selected = std::make_shared<PresentationScene>(*scene);
        without_selected->slides.front().shapes.erase(without_selected->slides.front().shapes.begin() + 1);
        PresentationPrepareOptions removed;
        removed.edited_slide = 0;
        removed.edited_shape = 1;
        removed.edit_layer_change = PresentationEditLayerChange::Remove;
        deletion_document = prepare_presentation(without_selected, deletion_document, removed);
        deletion_layers.paint(painter, deletion_document, 0, -1, -1, {}, image.size(), draw);
        check(cached_objects == before_delete_cached && backgrounds == before_delete_backgrounds,
            "deleting the selected object composes retained layers without repainting the page");

        auto image_scene = std::make_shared<PresentationScene>(fixture());
        image_scene->images.emplace_back("one.png", "image/png", "invalid-one");
        image_scene->images.emplace_back("two.png", "image/png", "invalid-two");
        image_scene->images.emplace_back("three.png", "image/png", "invalid-three");
        auto image_document = prepare_presentation(image_scene);
        auto fewer_images = std::make_shared<PresentationScene>(*image_scene);
        fewer_images->images.erase(fewer_images->images.begin() + 1);
        const auto reduced_document = prepare_presentation(fewer_images, image_document);
        check(reduced_document->image_cache == image_document->image_cache &&
                reduced_document->thumbnail_image_cache == image_document->thumbnail_image_cache,
            "removing an image asset retains decoded image caches for surviving assets");
        check(reduced_document->image_index.value(QStringLiteral("one.png")) == 0 &&
                reduced_document->image_index.value(QStringLiteral("three.png")) == 1,
            "removing an image asset rebuilds indices for the surviving image sequence");

        QImage source(32, 32, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::red);
        QByteArray encoded;
        QBuffer output(&encoded);
        output.open(QIODevice::WriteOnly);
        check(source.save(&output, "PNG"), "encode interaction image fixture");
        auto preview_scene = std::make_shared<PresentationScene>(fixture());
        preview_scene->images.emplace_back("preview.png", "image/png", encoded.toStdString());
        preview_scene->slides.front().shapes.front().image_path = "preview.png";
        const auto full_document = prepare_presentation(preview_scene);
        const auto interaction = presentation_interaction_document(full_document);
        check(presentation_image(interaction, "preview.png").isNull(),
            "interaction preview never waits for an uncached image decode");
        SlideRenderer preview_renderer;
        preview_renderer.setSize(QSizeF(320, 180));
        preview_renderer.setEditingShape(0);
        preview_renderer.setDeferredFrames(true);
        preview_renderer.setDocument(QVariant::fromValue(full_document));
        QImage preview_pixels(320, 180, QImage::Format_ARGB32_Premultiplied);
        preview_pixels.fill(Qt::white);
        QPainter preview_painter(&preview_pixels);
        preview_renderer.paint(&preview_painter);
        preview_painter.end();
        check(presentation_image(interaction, "preview.png").isNull(),
            "editor paint does not decode an uncached slide image");
        const auto decoded = presentation_image(full_document, "preview.png");
        check(!decoded.isNull() && presentation_image(interaction, "preview.png") == decoded,
            "interaction preview reuses a completed image without decoding again");
        auto added_image_scene = std::make_shared<PresentationScene>(*preview_scene);
        added_image_scene->images.emplace_back("added.png", "image/png", encoded.toStdString());
        const auto added_image_document = prepare_presentation(added_image_scene, full_document);
        const auto original_after_add = presentation_image(added_image_document, "preview.png");
        check(added_image_document->image_cache == full_document->image_cache &&
                !original_after_add.isNull() && original_after_add.constBits() == decoded.constBits() &&
                !presentation_image(added_image_document, "added.png").isNull(),
            "adding an image retains decoded pixels for unchanged assets and loads the new asset");
        auto replaced_image_scene = std::make_shared<PresentationScene>(*added_image_scene);
        replaced_image_scene->images.back().path = "replacement.png";
        PresentationPrepareOptions reused_analysis;
        reused_analysis.reuse_analysis = true;
        const auto replaced_image_document =
            prepare_presentation(replaced_image_scene, added_image_document, reused_analysis);
        check(replaced_image_document->image_cache == added_image_document->image_cache &&
                !replaced_image_document->image_index.contains(QStringLiteral("added.png")) &&
                replaced_image_document->image_index.contains(QStringLiteral("replacement.png")),
            "replacing one path retains safe pixels but rebuilds image indices");
        auto removed_image_scene = std::make_shared<PresentationScene>(*added_image_scene);
        removed_image_scene->images.pop_back();
        const auto removed_image_document = prepare_presentation(removed_image_scene, added_image_document);
        QImage changed_source(32, 32, QImage::Format_ARGB32_Premultiplied);
        changed_source.fill(Qt::blue);
        QByteArray changed_bytes;
        QBuffer changed_output(&changed_bytes);
        changed_output.open(QIODevice::WriteOnly);
        check(changed_source.save(&changed_output, "PNG"), "encode reintroduced image fixture");
        auto reintroduced_scene = std::make_shared<PresentationScene>(*removed_image_scene);
        reintroduced_scene->images.emplace_back("added.png", "image/png", changed_bytes.toStdString());
        const auto reintroduced_document = prepare_presentation(reintroduced_scene, removed_image_document);
        check(reintroduced_document->image_cache != removed_image_document->image_cache &&
                presentation_image(reintroduced_document, "added.png").pixelColor(0, 0) == Qt::blue,
            "reintroducing a cached path with new bytes cannot expose the old pixels");
        PresentationPrepareOptions lazy_images;
        lazy_images.eager_image_analysis = false;
        auto cold_scene = std::make_shared<PresentationScene>(*removed_image_scene);
        cold_scene->images.emplace_back("cold.png", "image/png", encoded.toStdString());
        const auto cold_document = prepare_presentation(cold_scene, removed_image_document, lazy_images);
        auto without_cold_scene = std::make_shared<PresentationScene>(*cold_scene);
        without_cold_scene->images.pop_back();
        const auto without_cold_document =
            prepare_presentation(without_cold_scene, cold_document, lazy_images);
        auto revived_scene = std::make_shared<PresentationScene>(*without_cold_scene);
        revived_scene->images.emplace_back("cold.png", "image/png", changed_bytes.toStdString());
        const auto revived_document = prepare_presentation(revived_scene, without_cold_document, lazy_images);
        check(revived_document->image_cache != without_cold_document->image_cache,
            "a removed lazy image path cannot be reused by an older in-flight decode");
    }

    void test_progressive_frames()
    {
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        scene->slides.resize(4, scene->slides.front());
        const auto document = prepare_presentation(scene);
        const QSize maximum_frame(1600, 900);
        for (int slide = 0; slide < 4; ++slide)
        {
            QImage image(maximum_frame, QImage::Format_ARGB32_Premultiplied);
            image.fill(slide == 0 ? Qt::red : Qt::blue);
            cache_presentation_frame(document, slide, {}, maximum_frame, image);
        }
        check(cached_presentation_frame(document, 0, {}, maximum_frame).pixelColor(0, 0) == Qt::red,
            "preloading four maximum-size frames does not evict the visible page");

        const auto cold_document = prepare_presentation(scene);
        PresentationRenderScheduler scheduler;
        QVector<int> ready;
        bool every_frame_cached = true;
        QObject::connect(&scheduler, &PresentationRenderScheduler::frameReady, &scheduler, [&](int slide)
        {
            ready.append(slide);
            every_frame_cached &= !scheduler.frame(cold_document, {}, QSize(320, 180), slide).isNull();
        });
        scheduler.request(cold_document, {}, QSize(320, 180), 1, 0, true);
        QElapsedTimer wait;
        wait.start();
        while (ready.size() < 3 && wait.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        check(ready == QVector<int>({1, 2, 0}) && every_frame_cached,
            "current and neighbor frames report readiness in drawing order with cached pixels");
        scheduler.cancel();

        auto heavy_scene = std::make_shared<PresentationScene>(*scene);
        for (int index = 0; index < 3000; ++index)
        {
            PresentationShape shape;
            shape.width = 30;
            shape.height = 20;
            shape.transform[4] = (index % 80) * 12;
            shape.transform[5] = (index / 80) * 12;
            shape.fill.color = "#506070";
            shape.effects.shadow_color = "#000000";
            shape.effects.shadow_opacity = 0.4;
            shape.effects.shadow_blur = 3;
            heavy_scene->slides[2].shapes.push_back(std::move(shape));
        }
        const auto heavy_document = prepare_presentation(heavy_scene);
        PresentationRenderScheduler progressive;
        QVector<int> progressive_ready;
        bool neighbor_cached_at_first = true;
        QObject::connect(&progressive, &PresentationRenderScheduler::frameReady, &progressive, [&](int slide)
        {
            if (progressive_ready.isEmpty())
            {
                neighbor_cached_at_first = !progressive.frame(heavy_document, {}, maximum_frame, 2).isNull();
            }
            progressive_ready.append(slide);
        });
        progressive.request(heavy_document, {}, maximum_frame, 1, 0, true);
        wait.restart();
        while (progressive_ready.isEmpty() && wait.elapsed() < 10000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        check(progressive_ready.value(0, -1) == 1 && !neighbor_cached_at_first,
            "the visible page reports readiness before a complex neighbor finishes");
        progressive.cancel();

        const auto newer_document = prepare_presentation(scene);
        ready.clear();
        scheduler.request(newer_document, {}, QSize(320, 180), 0, -1, true);
        scheduler.request(newer_document, {}, QSize(320, 180), 3, -1, false);
        wait.restart();
        while (ready.isEmpty() && wait.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        check(ready == QVector<int>({3}), "obsolete frame results cannot notify the current page");
        scheduler.cancel();
    }

    int pixel_difference(const QImage& a, const QImage& b)
    {
        int difference = 0;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x)
            {
                const auto first = a.pixelColor(x, y);
                const auto second = b.pixelColor(x, y);
                if (std::abs(first.red() - second.red()) > 3 ||
                    std::abs(first.green() - second.green()) > 3 ||
                    std::abs(first.blue() - second.blue()) > 3)
                    ++difference;
            }
        return difference;
    }

    void test_layer_pixels()
    {
        const auto scene = std::make_shared<PresentationScene>(fixture());
        const auto document = prepare_presentation(scene);
        const QVariantMap theme{{"accent", "#663399"}};
        SlideRenderer renderer;
        renderer.setDocument(QVariant::fromValue(document));
        renderer.setTheme(theme);
        renderer.setSelectedShape(1);
        renderer.setMediaEnabled(true);
        for (const auto size : {QSize(320, 180), QSize(540, 480)})
        {
            renderer.setSize(size);
            for (int frame = 0; frame < 6; ++frame)
            {
                auto reference = std::make_shared<PresentationScene>(*scene);
                auto& shape = reference->slides.front().shapes[1];
                shape.transform[4] += frame * 15;
                if (frame == 4)
                    shape.width = 135;
                renderer.setTransformPreview({{"id", QString::number(shape.id)}, {"x", shape.transform[4]},
                    {"y", shape.transform[5]}, {"width", shape.width}, {"height", shape.height}});
                QImage actual(size, QImage::Format_ARGB32_Premultiplied);
                actual.fill(Qt::white);
                QPainter output(&actual);
                renderer.paint(&output);
                output.end();
                QImage expected(size, QImage::Format_ARGB32_Premultiplied);
                expected.fill(Qt::white);
                QPainter direct(&expected);
                direct.setRenderHints(
                    QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
                paint_presentation_slide(
                    direct, prepare_presentation(reference), 0, theme, QRectF(QPointF(), size));
                direct.end();
                check(pixel_difference(actual, expected) < size.width() * size.height() / 1000,
                    "layer pixels match direct paint: z-order, alpha, off-page drag, resize and letterbox");
                PresentationPrepareOptions options;
                options.reuse_analysis = true;
                options.edited_slide = 0;
                options.edited_shape = 1;
                const auto committed = prepare_presentation(
                    reference, renderer.document().value<RenderPresentationPtr>(), options);
                renderer.setDocument(QVariant::fromValue(committed));
                renderer.setTransformPreview({});
                actual.fill(Qt::white);
                QPainter committed_painter(&actual);
                renderer.paint(&committed_painter);
                committed_painter.end();
                check(pixel_difference(actual, expected) < size.width() * size.height() / 1000,
                    "committed transforms reuse static layers without stale selected-object pixels");
            }
        }
        const QSize deletion_size(540, 480);
        auto before_delete = renderer.document().value<RenderPresentationPtr>();
        auto deleted_scene = std::make_shared<PresentationScene>(*before_delete->scene);
        deleted_scene->slides.front().shapes.erase(deleted_scene->slides.front().shapes.begin() + 1);
        PresentationPrepareOptions deleted_options;
        deleted_options.edited_slide = 0;
        deleted_options.edited_shape = 1;
        deleted_options.edit_layer_change = PresentationEditLayerChange::Remove;
        const auto deleted = prepare_presentation(deleted_scene, before_delete, deleted_options);
        renderer.setDocument(QVariant::fromValue(deleted));
        renderer.setSelectedShape(-1);
        QImage actual(deletion_size, QImage::Format_ARGB32_Premultiplied);
        actual.fill(Qt::white);
        QPainter actual_painter(&actual);
        renderer.paint(&actual_painter);
        actual_painter.end();
        QImage expected(deletion_size, QImage::Format_ARGB32_Premultiplied);
        expected.fill(Qt::white);
        QPainter expected_painter(&expected);
        paint_presentation_slide(expected_painter, deleted, 0, theme, QRectF(QPointF(), deletion_size));
        expected_painter.end();
        const auto deletion_difference = pixel_difference(actual, expected);
        const auto deletion_tolerance = deletion_size.width() * deletion_size.height() / 200;
        if (deletion_difference >= deletion_tolerance)
        {
            std::cerr << "delete layer pixel difference: " << deletion_difference << '\n';
        }
        check(deletion_difference < deletion_tolerance,
            "deleting the selected object composes retained layers without stale pixels");

        renderer.setSelectedShape(0);
        auto second_deleted_scene = std::make_shared<PresentationScene>(*deleted_scene);
        second_deleted_scene->slides.front().shapes.erase(
            second_deleted_scene->slides.front().shapes.begin());
        PresentationPrepareOptions second_deleted_options;
        second_deleted_options.edited_slide = 0;
        second_deleted_options.edited_shape = 0;
        second_deleted_options.edit_layer_change = PresentationEditLayerChange::Remove;
        const auto second_deleted =
            prepare_presentation(second_deleted_scene, deleted, second_deleted_options);
        renderer.setDocument(QVariant::fromValue(second_deleted));
        renderer.setSelectedShape(-1);
        actual.fill(Qt::white);
        QPainter second_actual_painter(&actual);
        renderer.paint(&second_actual_painter);
        second_actual_painter.end();
        expected.fill(Qt::white);
        QPainter second_expected_painter(&expected);
        paint_presentation_slide(
            second_expected_painter, second_deleted, 0, theme, QRectF(QPointF(), deletion_size));
        second_expected_painter.end();
        check(pixel_difference(actual, expected) < deletion_tolerance,
            "consecutive deletions compose from the last displayed revision without stale pixels");

        SlideRenderer immediate;
        immediate.setSize(deletion_size);
        immediate.setDeferredFrames(true);
        immediate.setTheme(theme);
        QImage immediate_base(deletion_size, QImage::Format_ARGB32_Premultiplied);
        immediate_base.fill(Qt::white);
        QPainter immediate_base_painter(&immediate_base);
        paint_presentation_slide(
            immediate_base_painter, before_delete, 0, theme, QRectF(QPointF(), deletion_size));
        immediate_base_painter.end();
        cache_presentation_frame(before_delete, 0, theme, deletion_size, immediate_base);
        immediate.setDocument(QVariant::fromValue(before_delete));
        immediate.setDocument(QVariant::fromValue(deleted));
        immediate.setDocument(QVariant::fromValue(second_deleted));
        actual.fill(Qt::white);
        QPainter immediate_painter(&actual);
        immediate.paint(&immediate_painter);
        immediate_painter.end();
        check(pixel_difference(actual, expected) < deletion_tolerance,
            "back-to-back document changes compose consecutive deletions before an intermediate paint");

        if (!second_deleted_scene->slides.front().shapes.empty())
        {
            const auto& shape = second_deleted_scene->slides.front().shapes.front();
            auto preview_scene = std::make_shared<PresentationScene>(*second_deleted_scene);
            preview_scene->slides.front().shapes.front().transform[4] += 24;
            immediate.setSelectedShape(0);
            immediate.setTransformPreview({{"id", QString::number(shape.id)}, {"x", shape.transform[4] + 24},
                {"y", shape.transform[5]}, {"width", shape.width}, {"height", shape.height}});
            actual.fill(Qt::white);
            QPainter preview_painter(&actual);
            immediate.paint(&preview_painter);
            preview_painter.end();
            expected.fill(Qt::white);
            QPainter preview_expected_painter(&expected);
            paint_presentation_slide(preview_expected_painter, prepare_presentation(preview_scene), 0, theme,
                QRectF(QPointF(), deletion_size));
            preview_expected_painter.end();
            check(pixel_difference(actual, expected) < deletion_tolerance,
                "dragging immediately after deletion repaints only the affected region without stale pixels");
        }
    }

    QImage render_scene(const std::shared_ptr<PresentationScene>& scene)
    {
        QImage image(320, 180, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        paint_presentation_slide(painter, prepare_presentation(scene), 0, {}, image.rect());
        return image;
    }

    void test_text_pixels()
    {
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        scene->width = 320;
        scene->height = 180;
        PresentationShape shape;
        shape.width = 200;
        shape.height = 22;
        shape.transform[4] = 20;
        shape.transform[5] = 20;
        shape.text.inset_left = shape.text.inset_right = shape.text.inset_top = shape.text.inset_bottom = 0;
        PresentationRun run;
        run.text = "First\nSecond";
        run.font_size = 26;
        PresentationParagraph paragraph;
        paragraph.runs.push_back(run);
        shape.text.paragraphs.push_back(paragraph);
        scene->slides.front().shapes.push_back(shape);
        const auto overflow = render_scene(scene);
        scene->slides.front().shapes.front().text.clip_vertical = true;
        const auto clipped = render_scene(scene);
        check(pixel_difference(overflow, clipped) > 100, "default vertical overflow keeps complete text");
        auto& text = scene->slides.front().shapes.front().text;
        text.clip_vertical = false;
        text.paragraphs.front().runs.front().text = "ABC ";
        auto second = text.paragraphs.front().runs.front();
        second.text = "DEF";
        second.color = "#FF0000";
        text.paragraphs.front().runs.push_back(second);
        const auto plain = render_scene(scene);
        text.paragraphs.front().runs.front().effects.reflection_opacity = 0.8;
        const auto reflected = render_scene(scene);
        int colored_reflections = 0;
        for (int y = 50; y < reflected.height(); ++y)
            for (int x = 0; x < reflected.width(); ++x)
            {
                const auto color = reflected.pixelColor(x, y);
                if (color != plain.pixelColor(x, y) && color.red() - color.green() > 20)
                    ++colored_reflections;
            }
        check(pixel_difference(plain, reflected) > 30 && colored_reflections == 0,
            "reflection belongs only to runs that requested it");
        text.paragraphs.front().runs.front().effects.reflection_end_position = 0.15;
        check(pixel_difference(reflected, render_scene(scene)) > 10,
            "reflection fade position changes pixels and invalidates its cache");
    }

    void test_semantic_contract()
    {
        PresentationBridge bridge;
        bridge.requestNew();
        auto tree = bridge.snapshot();
        const auto nodes = tree.value("nodes").toList();
        check(tree.value("ok").toBool() && !nodes.isEmpty(), "presentation snapshot exposes semantic nodes");
        QSet<QString> ids;
        for (const auto& value : nodes)
            ids.insert(value.toMap().value("id").toString());
        for (const auto& value : nodes)
        {
            const auto node = value.toMap();
            const auto parent = node.value("parentId").toString();
            check(parent.isEmpty() || ids.contains(parent), "every semantic parent resolves in page");
        }
        const auto object = nodes.back().toMap();
        const auto id = object.value("id").toString();
        check(bridge.selectObject(id) && bridge.selection().value("id") == id,
            "semantic ID selects object through public bridge");
        check(
            bridge.applyEdit("updateText", {{"text", "Semantic edit"}}), "semantic selected object editable");
        bridge.undo();
        check(bridge.selection().value("id") == id && bridge.selectObject(id),
            "object identity survives transaction and undo");
        check(!bridge.selectObject("missing") && !bridge.semanticTree(-1, 0).value("ok").toBool() &&
                !bridge.semanticTree(0, -1).value("ok").toBool(),
            "invalid semantic requests do not change selection");
        auto scene =
            std::make_shared<PresentationScene>(parse_presentation(test_fixture::table_package()).scene);
        scene->native_editable = true;
        auto& cells = scene->slides[0].shapes;
        while (cells.size() < 130)
        {
            auto copy = cells.front();
            copy.id = scene->next_shape_id++;
            cells.push_back(std::move(copy));
        }
        const auto first = presentation_semantic_tree(*scene, 0, 0);
        const auto next = presentation_semantic_tree(*scene, 0, 64);
        check(first.value("nextOffset").toInt() == 64 && next.value("nextOffset").toInt() == 128 &&
                presentation_semantic_tree(*scene, 0, 128).value("nextOffset").toInt() == -1,
            "semantic tree pagination is explicit and bounded");
        check(!presentation_capability_names(cells[0]).contains("transformShape") &&
                presentation_capability_names(cells[0]).contains("formatTextStyle"),
            "semantic capabilities reflect cell-safe core operations");
        check(bridge.editSchema().value("formatTextStyle").toMap().contains("reflection"),
            "semantic schema describes nested text effects");
    }

    void test_table_bridge()
    {
        QTemporaryDir directory;
        check(directory.isValid(), "table transaction scratch directory");
        const auto input = directory.filePath("source.pptx");
        const auto output = directory.filePath("copy.pptx");
        check(save_presentation_file(input.toStdString(), test_fixture::table_package(), {}).error ==
                PresentationError::None,
            "save table fixture");
        PresentationBridge bridge;
        const auto wait = [&]()
        {
            QElapsedTimer timer;
            timer.start();
            while ((bridge.busy() || bridge.syncing()) && timer.elapsed() < 5000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return !bridge.busy() && !bridge.syncing();
        };
        check(bridge.requestOpen(QUrl::fromLocalFile(input)) && wait(), "read table via public bridge");
        bridge.createEditableCopy();
        bridge.selectSaveFile(QUrl::fromLocalFile(output));
        check(wait() && bridge.editable(), "table requires distinct editable copy");
        bridge.selectShape(0);
        const auto original = bridge.selection().value("text");
        int document_changes = 0;
        QObject::connect(&bridge, &PresentationBridge::documentChanged, &bridge, [&document_changes]()
        {
            ++document_changes;
        });
        check(bridge.selection().value("isTableCell").toBool() &&
                bridge.applyEdit("updateText", {{"text", "Table UI transaction"}}),
            "table selection exposes editable text through GUI command");
        check(bridge.applyEdit("updateText", {{"text", "Table UI queued"}}) &&
                bridge.selection().value("text") == "Table UI queued" && bridge.syncing() &&
                document_changes == 2,
            "ordered imported edits update the visual model before package commits");
        check(wait() && document_changes == 2 && bridge.canUndo(),
            "ordered background package commits do not redraw the optimistic page");
        bridge.undo();
        check(bridge.selection().value("text") == "Table UI transaction",
            "table text undo restores the preceding committed edit");
        bridge.undo();
        check(bridge.selection().value("text") == original, "table text undo restores original content");
        bridge.redo();
        bridge.redo();
        check(bridge.selection().value("text") == "Table UI queued", "table text redo restores queued edits");
        check(!bridge.applyEdit("transformShape", {{"x", 99}}), "GUI command cannot move a table cell");
        bridge.save();
        check(wait() && !bridge.modified(), "table copy saves through public storage transaction");
        const auto reread = load_presentation_file(output.toStdString());
        check(reread.error == PresentationError::None &&
                reread.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text == "Table UI queued",
            "table public-bridge edit survives save and reopen");
        const auto source = load_presentation_file(input.toStdString());
        check(source.error == PresentationError::None &&
                QString::fromStdString(source.scene.slides[0].shapes[0].text.paragraphs[0].runs[0].text) ==
                    original.toString(),
            "table import remains unchanged");
    }

    void test_cell_fit()
    {
        QTextDocument document;
        document.setDocumentMargin(0);
        document.setTextWidth(180);
        QTextCursor cursor(&document);
        QFont font("Arial");
        font.setPixelSize(20);
        QTextCharFormat format;
        format.setFont(font);
        cursor.setBlockCharFormat(format);
        cursor.insertText("First row", format);
        cursor.insertBlock();
        cursor.insertText("Second row", format);
        check(document.size().height() > 30, "cell fixture exceeds available height");
        fit_presentation_text(document, QSizeF(180, 30), true);
        check(document.size().height() <= 30.01 && document.toPlainText() == "First row\nSecond row",
            "cell layout fits bounded height without removing text or moving cell");
    }

    void test_style_bridge_and_panel()
    {
        PresentationBridge bridge;
        bridge.requestNew();
        bridge.selectShape(0);
        const QVariantMap gradient{{"angle", 90},
            {"stops",
                QVariantList{QVariantMap{{"position", 0}, {"color", "#FF0000"}},
                    QVariantMap{{"position", 1}, {"color", "#0000FF"}}}}};
        check(bridge.applyEdit("formatTextStyle",
                  {{"fill", gradient}, {"shadow", QVariantMap{{"color", "#111111"}, {"opacity", 0.5}}},
                      {"reflection", QVariantMap{{"opacity", 0.55}, {"endPosition", 0.455}}}}),
            "public bridge accepts style command");
        const auto styled = bridge.selection().value("textStyle").toMap();
        check(styled.value("fill").toMap().value("stops").toList().size() == 2,
            "selection publishes text style through public bridge");
        bridge.undo();
        check(bridge.selection()
                  .value("textStyle")
                  .toMap()
                  .value("fill")
                  .toMap()
                  .value("stops")
                  .toList()
                  .isEmpty(),
            "style undo restores previous fill");
        bridge.redo();
        check(bridge.selection().value("textStyle").toMap() == styled, "style redo restores full state");
        check(!bridge.applyEdit("formatTextStyle", {{"shadow", "not-an-object"}}) &&
                !bridge.applyEdit("formatTextStyle", {{"reflection", QVariantMap{{"opacity", "invalid"}}}}) &&
                !bridge.applyEdit("formatTextStyle", {{"bogus", 1}}),
            "bridge rejects malformed style payloads");
        const auto document = bridge.document().value<RenderPresentationPtr>();
        const QDir output(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY));
        const auto saved =
            save_presentation_file(output.filePath("presentation-text-style.pptx").toStdString(),
                serialize_presentation(*document->scene).parts, {});
        check(saved.error == PresentationError::None, "export independent WordArt interop fixture");

        const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QFile theme_file(source.filePath("config/theme.json"));
        check(theme_file.open(QIODevice::ReadOnly), "read theme for isolated QML test");
        const auto theme = QJsonDocument::fromJson(theme_file.readAll()).object().toVariantMap();
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"qml(import QtQuick
import "../ui/components"
PresentationTextStyleTools
{
    property var controller
    selection: controller ? controller.selection : ({})
    onEditRequested: function(action, options) { controller.applyEdit(action, options); }
})qml",
            QUrl::fromLocalFile(source.filePath("tests/wordart-panel-test.qml")));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties(
            {{"theme", theme}, {"controller", QVariant::fromValue(&bridge)}, {"width", 980}}));
        check(panel != nullptr, "WordArt panel constructs without an application window");
        if (!panel)
        {
            std::cerr << component.errorString().toStdString();
            return;
        }
        for (const auto* section : {"fill", "outline", "shadow", "glow", "reflection", "warp"})
        {
            panel->setProperty("section", section);
            QCoreApplication::processEvents();
            qobject_cast<QQuickItem*>(panel.get())->ensurePolished();
            check(panel->property("implicitHeight").toDouble() > 0,
                "every WordArt subtool lays out without a window");
        }
        panel->setProperty("section", "shadow");
        check(QMetaObject::invokeMethod(
                  panel.get(), "changeNumber", Q_ARG(QVariant, "opacity"), Q_ARG(QVariant, 0.25)),
            "invoke panel edit command");
        check(bridge.selection()
                    .value("textStyle")
                    .toMap()
                    .value("shadow")
                    .toMap()
                    .value("opacity")
                    .toDouble() == 0.25,
            "QML nested style payload reaches core transaction and selection feedback");
    }
}

int run_edit_render_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    test_cache_contract();
    test_progressive_frames();
    test_thumbnail_revisions();
    test_layer_pixels();
    test_text_pixels();
    test_style_bridge_and_panel();
    test_semantic_contract();
    test_cell_fit();
    test_table_bridge();
    QThreadPool::globalInstance()->waitForDone();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_edit_render_tests(argc, argv);
}
