#include "presentation_bridge.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <memory>

void qml_register_types_Mirrorfly_Native();

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

    bool wait_for(mirrorfly::PresentationBridge& bridge)
    {
        QElapsedTimer timer;
        timer.start();
        while (bridge.busy() && timer.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        QCoreApplication::processEvents();
        return check(!bridge.busy(), "isolated canvas operation completes");
    }

    bool check_rotated_resize(QQmlEngine& engine, const QVariantMap& theme, const QDir& source)
    {
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/components/PresentationTransformOverlay.qml")));
        const QVariantMap selection{{"valid", true}, {"id", "rotated"}, {"a", 0}, {"b", 1}, {"c", -1},
            {"d", 0}, {"x", 100}, {"y", 100}, {"width", 200}, {"height", 100}};
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{"theme", theme}, {"selection", selection}, {"sceneScale", 0.5}}));
        if (!check(object != nullptr, "rotated transform controller constructs without a window"))
            return false;
        auto* outline = object->findChild<QObject*>(QStringLiteral("presentationSelectionOutline"));
        bool passed = check(outline && outline->property("visible").toBool(),
            "selection outline is a lightweight QML overlay independent of slide painting");
        QMetaObject::invokeMethod(
            object.get(), "beginGesture", Q_ARG(QVariant, "nw"), Q_ARG(QVariant, 0), Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(
            object.get(), "updateGesture", Q_ARG(QVariant, -10), Q_ARG(QVariant, 15), Q_ARG(QVariant, false));
        const auto preview = object->property("preview").value<QJSValue>().toVariant().toMap();
        passed = check(preview.value("x").toDouble() == 80 && preview.value("y").toDouble() == 130 &&
                preview.value("width").toDouble() == 170 && preview.value("height").toDouble() == 80,
            "rotated resize inverts the linear transform and keeps the opposite corner fixed");
        QMetaObject::invokeMethod(object.get(), "updateGesture", Q_ARG(QVariant, -1000000),
            Q_ARG(QVariant, 1000000), Q_ARG(QVariant, true));
        const auto bounded = object->property("preview").value<QJSValue>().toVariant().toMap();
        passed = check(bounded.value("width").toDouble() >= 1 && bounded.value("height").toDouble() >= 1 &&
                         bounded.value("width").toDouble() / bounded.value("height").toDouble() == 2,
                     "proportional resize remains positive when dragging beyond the opposite corner") &&
            passed;
        object->setProperty("enabled", false);
        return check(!object->property("dragging").toBool(),
                   "disabling the controller cancels transient geometry") &&
            passed;
    }

    bool run_cases(QQmlEngine& engine, const QVariantMap& theme, const QDir& source)
    {
        if (!check_rotated_resize(engine, theme, source))
            return false;
        mirrorfly::PresentationBridge bridge;
        bridge.requestNew();
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import "../ui"
PresentationPage
{
    required property var backend
    document: backend.document
    selection: backend.selection
    selectedShape: backend.selectedShape
    currentSlide: backend.currentSlide
    slideCount: backend.slideCount
    slideWidth: backend.slideWidth
    slideHeight: backend.slideHeight
    editable: backend.editable
    busy: backend.locked
    onSlideRequested: function(index) { backend.setSlide(index); }
    onEditRequested: function(action, options)
    {
        if (!backend.applyEdit(action, options))
        {
            restoreSelection();
        }
    }
    onZoomRequested: function(value) { zoom = value; }
}
)qml",
            QUrl::fromLocalFile(source.filePath("tests/canvas-contract.qml")));
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{"backend", QVariant::fromValue(&bridge)}, {"theme", theme}, {"width", 1040}, {"height", 676}}));
        auto* page = qobject_cast<QQuickItem*>(object.get());
        if (!check(page != nullptr, "canvas test constructs an isolated page without a window"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        for (int pass = 0; pass < 4; ++pass)
        {
            QCoreApplication::processEvents();
            page->ensurePolished();
        }
        auto* input = page->findChild<QObject*>(QStringLiteral("presentationTextInput"));
        if (!check(input != nullptr, "canvas exposes an inline text control"))
        {
            return false;
        }
        auto* viewport = page->findChild<QQuickItem*>(QStringLiteral("presentationSlideViewport"));
        auto* full_slide = page->findChild<QQuickItem*>(QStringLiteral("presentationFullSlide"));
        auto* pan_area = page->findChild<QQuickItem*>(QStringLiteral("presentationCanvasPanArea"));
        if (!check(viewport && full_slide && pan_area, "canvas exposes zoom and panning surfaces"))
        {
            return false;
        }
        const QSizeF raster_size(full_slide->width(), full_slide->height());
        QMetaObject::invokeMethod(page, "requestCanvasZoom", Q_ARG(QVariant, 2.0),
            Q_ARG(QVariant, viewport->width() / 2), Q_ARG(QVariant, viewport->height() / 2));
        for (int pass = 0; pass < 3; ++pass)
        {
            QCoreApplication::processEvents();
            page->ensurePolished();
        }
        bool passed = check(page->property("zoom").toDouble() == 2.0 &&
                std::abs(full_slide->width() - raster_size.width()) < 0.01 &&
                std::abs(full_slide->height() - raster_size.height()) < 0.01 &&
                std::abs(full_slide->scale() - 2.0) < 0.01 &&
                viewport->property("contentWidth").toDouble() > viewport->width(),
            "anchored zoom uses a display transform without rebuilding the slide raster");
        const double pan_start = viewport->property("contentX").toDouble();
        QVariant began_pan;
        QMetaObject::invokeMethod(page, "beginCanvasPan", Q_RETURN_ARG(QVariant, began_pan),
            Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject*>(pan_area))),
            Q_ARG(QVariant, viewport->width() / 2), Q_ARG(QVariant, viewport->height() / 2));
        QMetaObject::invokeMethod(page, "updateCanvasPan",
            Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject*>(pan_area))),
            Q_ARG(QVariant, viewport->width() / 2 - 80), Q_ARG(QVariant, viewport->height() / 2));
        QMetaObject::invokeMethod(page, "endCanvasPan");
        passed = check(began_pan.toBool() && viewport->property("contentX").toDouble() > pan_start + 50,
                     "mouse panning moves the zoomed slide without editing document geometry") &&
            passed;
        QMetaObject::invokeMethod(page, "requestCanvasZoom", Q_ARG(QVariant, 0.0),
            Q_ARG(QVariant, viewport->width() / 2), Q_ARG(QVariant, viewport->height() / 2));
        QCoreApplication::processEvents();
        passed = check(page->property("zoom").toDouble() == 0.0 &&
                         viewport->property("contentX").toDouble() == 0.0 &&
                         viewport->property("contentY").toDouble() == 0.0,
                     "fit-to-window resets the panned viewport") &&
            passed;
        const QString original = bridge.selection().value("text").toString();
        QMetaObject::invokeMethod(page, "beginTextEditing");
        passed =
            check(page->property("canvasEditing").toBool(), "selected text enters inline editing") && passed;
        const QString edited = QStringLiteral("在画布中输入中文与 English\n第二行");
        input->setProperty("text", edited);
        passed = check(!bridge.modified() && bridge.selection().value("text").toString() == original &&
                         input->property("text").toString() == edited,
                     "inline typing stays local until the editing gesture finishes") &&
            passed;
        QMetaObject::invokeMethod(page, "finishTextEditing");
        passed = check(bridge.modified() && bridge.selection().value("text").toString() == edited,
                     "finishing inline text creates one document command and dirty state") &&
            passed;
        bridge.undo();
        QCoreApplication::processEvents();
        passed = check(input->property("text").toString() == original && !bridge.modified(),
                     "undo synchronizes the canvas with the saved scene") &&
            passed;
        bridge.redo();
        QCoreApplication::processEvents();
        passed =
            check(input->property("text").toString() == edited, "redo returns the accepted canvas text") &&
            passed;
        QMetaObject::invokeMethod(page, "beginTextEditing");
        input->setProperty("text", QStringLiteral("Rejected text") + QChar(1));
        QMetaObject::invokeMethod(page, "finishTextEditing");
        passed = check(input->property("text").toString() == edited &&
                         bridge.selection().value("text").toString() == edited && !bridge.error().isEmpty(),
                     "rejected invalid input restores committed text without losing the draft") &&
            passed;
        auto* transform = page->findChild<QObject*>(QStringLiteral("presentationTransformOverlay"));
        if (!check(transform != nullptr, "canvas provides transform controller"))
            return false;
        const auto initial = bridge.selection();
        const double scale = transform->property("sceneScale").toDouble();
        QVariant began;
        QMetaObject::invokeMethod(transform, "beginGesture", Q_RETURN_ARG(QVariant, began),
            Q_ARG(QVariant, "move"), Q_ARG(QVariant, 0), Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(transform, "updateGesture", Q_ARG(QVariant, 50 * scale),
            Q_ARG(QVariant, 20 * scale), Q_ARG(QVariant, false));
        passed = check(began.toBool() && bridge.selection().value("x") == initial.value("x"),
                     "drag preview does not mutate committed scene") &&
            passed;
        QMetaObject::invokeMethod(transform, "endGesture");
        passed = check(bridge.selection().value("x").toDouble() == initial.value("x").toDouble() + 50 &&
                         bridge.selection().value("y").toDouble() == initial.value("y").toDouble() + 20,
                     "drag converts viewport coordinates to document units") &&
            passed;
        bridge.undo();
        passed = check(bridge.selection().value("x") == initial.value("x") &&
                         bridge.selection().value("y") == initial.value("y"),
                     "one undo reverses the entire gesture") &&
            passed;
        QMetaObject::invokeMethod(
            transform, "beginGesture", Q_ARG(QVariant, "nw"), Q_ARG(QVariant, 0), Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(transform, "updateGesture", Q_ARG(QVariant, 20 * scale),
            Q_ARG(QVariant, 10 * scale), Q_ARG(QVariant, false));
        QMetaObject::invokeMethod(transform, "endGesture");
        passed =
            check(bridge.selection().value("width").toDouble() == initial.value("width").toDouble() - 20 &&
                    bridge.selection().value("x").toDouble() == initial.value("x").toDouble() + 20,
                "top-left grip resizes while fixing the opposite corner") &&
            passed;
        bridge.undo();
        QMetaObject::invokeMethod(
            transform, "beginGesture", Q_ARG(QVariant, "se"), Q_ARG(QVariant, 0), Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(
            transform, "updateGesture", Q_ARG(QVariant, 90), Q_ARG(QVariant, 40), Q_ARG(QVariant, true));
        QMetaObject::invokeMethod(transform, "cancel");
        passed = check(bridge.selection().value("width") == initial.value("width"),
                     "cancelled drag keeps committed geometry") &&
            passed;
        QVariant nudged;
        QMetaObject::invokeMethod(
            page, "nudgeSelection", Q_RETURN_ARG(QVariant, nudged), Q_ARG(QVariant, 1), Q_ARG(QVariant, -10));
        passed = check(nudged.toBool() &&
                         bridge.selection().value("y").toDouble() == initial.value("y").toDouble() - 10,
                     "keyboard nudge follows the public transform command") &&
            passed;
        bridge.undo();
        QMetaObject::invokeMethod(page, "beginTextEditing");
        QMetaObject::invokeMethod(
            page, "nudgeSelection", Q_RETURN_ARG(QVariant, nudged), Q_ARG(QVariant, 1), Q_ARG(QVariant, 0));
        passed = check(!nudged.toBool(), "text-editing arrow keys cannot move the object") && passed;
        QMetaObject::invokeMethod(page, "finishTextEditing");
        for (const auto* layout : {"reportOutline", "researchPlan", "comparison", "references", "conclusion",
                 "projectStatus", "meetingSummary", "milestones"})
        {
            const int previous_count = bridge.slideCount();
            passed = check(bridge.applyEdit(QStringLiteral("addSlide"), {{"layout", layout}}) &&
                             bridge.slideCount() == previous_count + 1,
                         "academic page commands reach the core through the bridge") &&
                passed;
            bridge.undo();
            passed =
                check(bridge.slideCount() == previous_count, "one undo removes an entire template") && passed;
            bridge.redo();
            passed =
                check(bridge.slideCount() == previous_count + 1, "one redo restores the entire template") &&
                passed;
        }
        auto* menu = page->findChild<QObject*>(QStringLiteral("presentationSlideMenu"));
        if (!check(menu != nullptr, "thumbnail menu is part of the isolated page"))
        {
            return false;
        }
        const int previous_count = bridge.slideCount();
        menu->setProperty("targetSlide", 0);
        QMetaObject::invokeMethod(menu, "actionRequested", Q_ARG(QString, QStringLiteral("addSlide")),
            Q_ARG(QVariant, QVariant(QVariantMap{{"layout", "blank"}, {"before", true}})));
        passed = check(bridge.slideCount() == previous_count + 1 && bridge.currentSlide() == 0,
                     "thumbnail menu inserts before its target, independent of the previous selection") &&
            passed;
        menu->setProperty("targetSlide", 0);
        QMetaObject::invokeMethod(menu, "actionRequested", Q_ARG(QString, QStringLiteral("deleteSlide")),
            Q_ARG(QVariant, QVariant(QVariantMap{})));
        passed = check(bridge.slideCount() == previous_count, "thumbnail menu deletes its selected target") &&
            passed;
        bridge.undo();
        bridge.undo();
        QTemporaryDir directory;
        if (!check(directory.isValid(), "canvas test owns an isolated save directory"))
        {
            return false;
        }
        const QString path = directory.filePath(QStringLiteral("canvas.pptx"));
        bridge.saveAs();
        bridge.selectSaveFile(QUrl::fromLocalFile(path));
        passed = wait_for(bridge) && passed;
        const auto saved = mirrorfly::load_presentation_file(path.toUtf8().toStdString());
        passed =
            check(!bridge.modified() && saved.error == mirrorfly::PresentationError::None &&
                    saved.scene.slides.size() == 9 &&
                    saved.scene.slides.front().shapes.front().text.paragraphs.front().runs.front().text ==
                        QStringLiteral("在画布中输入中文与 English").toUtf8().toStdString(),
                "canvas edits survive the actual PPTX write and reopen path") &&
            passed;
        bridge.requestOpen(QUrl::fromLocalFile(path));
        passed = wait_for(bridge) && passed;
        QMetaObject::invokeMethod(page, "beginTextEditing");
        passed = check(!bridge.editable() && !page->property("canvasEditing").toBool(),
                     "read-only imported slides cannot enter inline editing") &&
            passed;
        return passed;
    }
}

int run_presentation_canvas_tests(int argc, char* argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
    QGuiApplication application(argc, argv);
    qml_register_types_Mirrorfly_Native();
    QQmlEngine engine;
    engine.addImportPath(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY));
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& errors)
    {
        for (const auto& error : errors)
        {
            warnings.append(error.toString());
        }
    });
    const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
    QFile file(source.filePath("config/theme.json"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return 1;
    }
    const auto theme = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    const bool passed = run_cases(engine, theme, source);
    for (const auto& warning : warnings)
    {
        std::cerr << warning.toStdString() << '\n';
    }
    return passed && warnings.empty() ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_presentation_canvas_tests(argc, argv);
}
