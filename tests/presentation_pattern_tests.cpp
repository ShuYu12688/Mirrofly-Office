#include "automation_bridge.hpp"
#include "office_ai_page_layout.hpp"
#include "office_ai_style.hpp"
#include "office_ai_toolbox.hpp"
#include "presentation_bridge.hpp"
#include "presentation_edit_adapter.hpp"
#include "presentation_fill_renderer.hpp"
#include "presentation_semantics.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/presentation_storage.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    using namespace mirrorfly;
    int failures = 0;
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
    bool wait_for(const std::function<bool()>& condition)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!condition() && elapsed.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return condition();
    }
    QImage render(const PresentationScene& scene, int slide = 0)
    {
        QImage image(960, 540, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        paint_presentation_slide(painter, prepare_presentation(std::make_shared<PresentationScene>(scene)),
            slide, {}, image.rect());
        return image;
    }
    PresentationShape sample(int index)
    {
        PresentationShape shape;
        shape.id = index + 1;
        shape.name = "Style sample " + std::to_string(index + 1);
        shape.geometry = "rect";
        shape.transform = {1, 0, 0, 1, 20.0 + index % 4 * 235, 20.0 + index / 4 * 125};
        shape.width = 205;
        shape.height = 90;
        shape.outline_color.clear();
        shape.fill.color = "#00FF00";
        shape.fill.pattern_foreground_color = "#FF0000";
        return shape;
    }
    void test_pattern_roundtrip(const QDir& output)
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.slides.front().shapes.clear();
        int index = 0;
        for (const auto& pattern : presentation_pattern_presets())
        {
            auto shape = sample(index++);
            shape.fill.pattern = pattern;
            scene.slides.front().shapes.push_back(std::move(shape));
        }
        scene.next_shape_id = 100;
        const auto saved = serialize_presentation(scene);
        check(saved.error == PresentationError::None, "all common patterns serialize");
        auto loaded = parse_presentation(saved.parts);
        check(loaded.error == PresentationError::None, "all common patterns parse from actual DrawingML");
        const auto image = render(loaded.scene);
        for (index = 0; index < 16; ++index)
        {
            const auto& shape = loaded.scene.slides[0].shapes[index];
            check(shape.fill.pattern == presentation_pattern_presets()[index] &&
                    shape.fill.color == "#00FF00" && shape.fill.pattern_foreground_color == "#FF0000",
                "read preserves independent pattern foreground and background colors");
            int foreground = 0;
            int background = 0;
            for (int y = shape.transform[5] + 5; y < shape.transform[5] + 85; ++y)
                for (int x = shape.transform[4] + 5; x < shape.transform[4] + 200; ++x)
                {
                    const auto pixel = image.pixelColor(x, y);
                    foreground += pixel.red() > 240 && pixel.green() < 15;
                    background += pixel.green() > 240 && pixel.red() < 15;
                }
            check(foreground > 20 && background > 20,
                "every pattern paints both colors, never a solid fallback");
        }
        check(image.save(output.filePath("presentation-patterns.png")), "save all-pattern rendered evidence");
        check(save_presentation_file(
                  output.filePath("presentation-patterns.pptx").toStdString(), saved.parts, {})
                    .error == PresentationError::None,
            "save independent pattern interop package");

        PresentationFill transparent;
        transparent.pattern = "cross";
        transparent.color = "#008000";
        transparent.pattern_foreground_color = "#FF0000";
        transparent.pattern_foreground_opacity = 0;
        auto tile = presentation_pattern_fill(transparent).textureImage();
        check(tile.pixelColor(0, 0).alpha() == 0 && tile.pixelColor(1, 1) == QColor("#008000"),
            "transparent foreground replaces its pattern pixels and retains opaque background");
        transparent.pattern_foreground_opacity = 0.5;
        tile = presentation_pattern_fill(transparent).textureImage();
        QImage composed(8, 8, QImage::Format_ARGB32_Premultiplied);
        composed.fill(Qt::blue);
        {
            QPainter painter(&composed);
            painter.drawImage(0, 0, tile);
        }
        const auto foreground_pixel = composed.pixelColor(0, 0);
        check(std::abs(foreground_pixel.red() - 128) <= 1 && foreground_pixel.green() == 0 &&
                std::abs(foreground_pixel.blue() - 127) <= 1 &&
                composed.pixelColor(1, 1) == QColor("#008000"),
            "independent-alpha composition matches the Microsoft HatchBrush bitmap reference");
        check(composed.save(output.filePath("presentation-pattern-alpha.png")),
            "save independently composited alpha reference");
        transparent.opacity = 0.25;
        tile = presentation_pattern_fill(transparent).textureImage();
        check(std::abs(tile.pixelColor(0, 0).alpha() - 128) <= 1 &&
                std::abs(tile.pixelColor(1, 1).alpha() - 64) <= 1,
            "pattern foreground and background alpha remain independent");
        auto alpha_scene = make_presentation(PresentationSlideLayout::Blank);
        auto alpha_shape = sample(0);
        alpha_shape.fill = transparent;
        alpha_scene.slides[0].shapes = {alpha_shape};
        const auto alpha = parse_presentation(serialize_presentation(alpha_scene).parts);
        check(std::abs(alpha.scene.slides[0].shapes[0].fill.opacity - 0.25) < 1e-6 &&
                std::abs(alpha.scene.slides[0].shapes[0].fill.pattern_foreground_opacity - 0.5) < 1e-6,
            "both alpha channels roundtrip through independent DrawingML colors");
        check(save_presentation_file(output.filePath("presentation-pattern-alpha.pptx").toStdString(),
                  serialize_presentation(alpha_scene).parts, {})
                    .error == PresentationError::None,
            "save independently verifiable alpha sample");

        loaded.scene.native_editable = true;
        PresentationEditCommand edit;
        edit.action = PresentationEditAction::FormatShape;
        edit.fill_pattern = "zigZag";
        check(apply_presentation_edit(loaded.scene, edit).error == PresentationEditError::InvalidValue,
            "unsupported authoring pattern fails atomically");
        edit.fill_pattern = "cross";
        edit.pattern_foreground_color = "bad";
        check(apply_presentation_edit(loaded.scene, edit).error == PresentationEditError::InvalidValue,
            "invalid pattern color fails atomically");
        edit.pattern_foreground_color = "#112233";
        edit.fill_color = "#FFFFFF";
        check(apply_presentation_edit(loaded.scene, edit).error == PresentationEditError::InvalidValue,
            "conflicting solid and pattern fills are rejected");
        edit = {};
        edit.action = PresentationEditAction::FormatShape;
        edit.fill_pattern = "horz";
        auto default_scene = make_presentation(PresentationSlideLayout::Blank);
        auto default_shape = sample(0);
        default_shape.fill = {};
        default_scene.slides[0].shapes = {default_shape};
        check(apply_presentation_edit(default_scene, edit).error == PresentationEditError::None &&
                default_scene.slides[0].shapes[0].fill.color == "#FFFFFF" &&
                default_scene.slides[0].shapes[0].fill.pattern_foreground_color == "#000000",
            "new pattern without explicit colors uses readable independent defaults");
        edit.fill_pattern = "none";
        check(apply_presentation_edit(default_scene, edit).error == PresentationEditError::None &&
                default_scene.slides[0].shapes[0].fill.pattern.empty(),
            "explicit pattern removal returns to the background solid fill");
        edit.fill_pattern = "horz";
        apply_presentation_edit(default_scene, edit);
        edit = {};
        edit.action = PresentationEditAction::FormatShape;
        edit.gradient_start_color = "#10233F";
        edit.gradient_end_color = "#FFFFFF";
        check(apply_presentation_edit(default_scene, edit).error == PresentationEditError::None &&
                default_scene.slides[0].shapes[0].fill.pattern.empty() &&
                default_scene.slides[0].shapes[0].fill.stops.size() == 2,
            "explicit gradient replacement removes the former pattern");
    }
    void test_preservation_and_dashes(const QDir& output)
    {
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.slides[0].shapes.clear();
        int index = 0;
        for (const auto& dash : presentation_line_dash_presets())
        {
            auto shape = sample(index++);
            shape.transform = {1, 0, 0, 1, 40, 25.0 + (index - 1) * 45};
            shape.width = 880;
            shape.height = 20;
            shape.fill = {};
            shape.outline_color = "#193049";
            shape.outline_width = 2;
            shape.line_style.dashes = presentation_line_dash_pattern(dash);
            scene.slides[0].shapes.push_back(std::move(shape));
        }
        const auto saved = serialize_presentation(scene);
        auto loaded = parse_presentation(saved.parts);
        for (index = 0; index < 11; ++index)
            check(presentation_line_dash_name(loaded.scene.slides[0].shapes[index].line_style.dashes) ==
                    presentation_line_dash_presets()[index],
                "all eleven preset dashes roundtrip natively");
        check(render(loaded.scene).save(output.filePath("presentation-dashes.png")),
            "save real dashed outlines");
        check(
            save_presentation_file(output.filePath("presentation-dashes.pptx").toStdString(), saved.parts, {})
                    .error == PresentationError::None,
            "save independent dash preset package");

        scene.slides[0].shapes.resize(1);
        auto& shape = scene.slides[0].shapes[0];
        shape.fill.pattern = "zigZag";
        shape.fill.color = "#00FF00";
        shape.fill.pattern_foreground_color = "#FF0000";
        shape.line_style.dashes = {2.3, 4.7, 1.2, 3.4};
        auto original = serialize_presentation(scene).parts;
        for (auto& part : original)
            if (part.path == "ppt/slides/slide1.xml")
            {
                const auto at = part.bytes.find("<a:pattFill ");
                check(at != std::string::npos, "unknown native pattern fixture is present");
                part.bytes.insert(at + 12, "data-preserve=\"yes\" ");
            }
        auto unknown = parse_presentation(original);
        unknown.scene.native_editable = true;
        check(!presentation_pattern_supported(unknown.scene.slides[0].shapes[0].fill.pattern) &&
                presentation_line_dash_name(unknown.scene.slides[0].shapes[0].line_style.dashes) == "custom",
            "unknown pattern and custom dash remain accurately identified");
        PresentationEditCommand edit;
        edit.action = PresentationEditAction::FormatShape;
        edit.line_dash = "sysDashDotDot";
        check(apply_presentation_edit(unknown.scene, edit).error == PresentationEditError::None,
            "editing an unrelated outline does not forbid preserved unknown fill");
        const auto after = serialize_presentation(unknown.scene);
        for (const auto& part : after.parts)
            if (part.path == "ppt/slides/slide1.xml")
                check(part.bytes.find("data-preserve=\"yes\"") != std::string::npos &&
                        part.bytes.find("prst=\"zigZag\"") != std::string::npos,
                    "unrelated style edits keep unknown fill XML attributes");
        edit.line_dash = "not-a-dash";
        check(apply_presentation_edit(unknown.scene, edit).error == PresentationEditError::InvalidValue,
            "invalid dash cannot silently become solid");
    }
    void test_bridge(const QDir& source, const QDir& output)
    {
        PresentationBridge bridge;
        bridge.requestNew();
        check(bridge.applyEdit("addShape", {{"geometry", "rect"}}), "add public bridge shape");
        wait_for([&]()
        {
            return !bridge.syncing();
        });
        bridge.selectShape(0);
        check(bridge.applyEdit("formatShape",
                  {{"fillPattern", "smCheck"}, {"patternForegroundColor", "#193049"},
                      {"patternBackgroundColor", "#EAF1FA"}, {"outlineColor", "#193049"}, {"outlineWidth", 2},
                      {"lineDash", "lgDashDotDot"}}),
            "public bridge accepts pattern and extended dash");
        wait_for([&]()
        {
            return !bridge.syncing();
        });
        const auto styled = bridge.selection();
        check(styled.value("fillPattern") == "smCheck" && styled.value("lineDash") == "lgDashDotDot" &&
                styled.value("patternPresets").toStringList().size() == 16 &&
                styled.value("lineDashPresets").toStringList().size() == 11,
            "public selection exposes real pattern and dash capabilities");
        bridge.undo();
        check(bridge.selection().value("fillPattern").toString().isEmpty(), "style undo restores solid fill");
        bridge.redo();
        check(bridge.selection().value("fillPattern") == "smCheck", "style redo restores pattern fill");
        const auto schema = presentation_edit_schema().value("formatShape").toMap();
        check(schema.contains("fillPattern") && schema.value("lineDash").toString().contains("sysDashDotDot"),
            "AI format schema exposes the same supported presets");

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"qml(import QtQuick
import "../ui/components"
PresentationObjectTools
{
    property var controller
    function automationReady() { return true; }
    function automationState() { return {module: "slides", pendingInput: false}; }
    section: "appearance"
    selection: controller ? controller.selection : ({})
    onEditRequested: function(action, options) { controller.applyEdit(action, options); }
})qml",
            QUrl::fromLocalFile(source.filePath("tests/pattern-panel-test.qml")));
        QFile theme_file(source.filePath("config/theme.json"));
        check(theme_file.open(QIODevice::ReadOnly), "read real theme for isolated QML");
        std::unique_ptr<QObject> panel(component.createWithInitialProperties(
            {{"theme", QJsonDocument::fromJson(theme_file.readAll()).object().toVariantMap()},
                {"controller", QVariant::fromValue(&bridge)}}));
        check(panel != nullptr, "actual appearance parent constructs pattern and dash children offscreen");
        if (!panel)
        {
            std::cerr << component.errorString().toStdString();
            return;
        }
        auto* pattern = panel->findChild<QObject*>("presentationPatternPreset");
        auto* dash = panel->findChild<QObject*>("presentationLineDashPreset");
        check(pattern && dash, "pattern and dash controls belong to the real parent");
        if (pattern && dash)
        {
            pattern->setProperty("currentIndex", 9);
            check(QMetaObject::invokeMethod(pattern, "activated", Q_ARG(int, 9)),
                "activate real pattern child");
            wait_for([&]()
            {
                return !bridge.syncing();
            });
            check(bridge.selection().value("fillPattern") == "dnDiag", "child signal edits selected shape");
            dash->setProperty("currentIndex", 10);
            QMetaObject::invokeMethod(dash, "activated", Q_ARG(int, 10));
            wait_for([&]()
            {
                return !bridge.syncing();
            });
            check(bridge.selection().value("lineDash") == "sysDashDotDot",
                "all-preset dash child edits selected shape");
        }
        AutomationBridge automation;
        automation.registerModule("slides", &bridge);
        automation.setUiRoot(panel.get());
        OfficeAiToolbox toolbox(output.absolutePath());
        toolbox.query("office_load_group", {{"group", "slides"}});
        toolbox.beginResponse(QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot()))
                                  .object()
                                  .value("revision")
                                  .toString(),
            1);
        bool completed = false;
        QJsonObject result;
        toolbox.execute("office_action",
            {{"op", "slides.applyEdit"},
                {"args",
                    QJsonObject{{"action", "formatShape"},
                        {"options",
                            QJsonObject{{"fillPattern", "pct25"}, {"patternForegroundColor", "#123456"},
                                {"lineDash", "sysDot"}}}}}},
            [&](const QJsonObject& value)
        {
            result = value;
            completed = true;
        });
        const bool action_completed = wait_for([&]()
        {
            return completed;
        });
        if (!action_completed || !result.value("ok").toBool())
            std::cerr << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        check(action_completed && result.value("ok").toBool() &&
                bridge.selection().value("fillPattern") == "pct25" &&
                bridge.selection().value("lineDash") == "sysDot",
            "real AI action routes pattern and dash through public edit");
        const auto document = bridge.document().value<RenderPresentationPtr>();
        check(save_presentation_file(output.filePath("presentation-pattern-ui.pptx").toStdString(),
                  serialize_presentation(*document->scene).parts, {})
                    .error == PresentationError::None,
            "save actual UI and AI style result for independent verification");
    }

    void test_art_layouts(const QDir& output)
    {
        const QJsonArray blocks{
            QJsonObject{{"heading", "Observe"}, {"text", "Read the landscape before acting."}},
            QJsonObject{{"heading", "Connect"}, {"text", "Link local knowledge with field observations."}},
            QJsonObject{{"heading", "Care"}, {"text", "Protect habitats through steady daily choices."}}};
        const QJsonObject page{{"title", "A living landscape"}, {"blocks", blocks}};
        QJsonArray fingerprints;
        auto deck = make_presentation(PresentationSlideLayout::Blank);
        deck.slides.clear();
        for (const QString style : {"editorial", "research", "modern", "natural"})
        {
            QJsonArray previous;
            for (int number : {3, 4})
            {
                const auto recipe =
                    office_ai_page_recipe(page, office_ai_style_theme(style), 960, 540, number);
                check(recipe.value("ok").toBool(), "every art direction fits the same complete information");
                if (!recipe.value("ok").toBool())
                {
                    std::cerr << QJsonDocument(recipe).toJson().toStdString();
                    continue;
                }
                QJsonArray fingerprint;
                QStringList texts;
                auto scene = make_presentation(PresentationSlideLayout::Blank);
                scene.slides[0].shapes.clear();
                scene.slides[0].background.color = recipe.value("background").toString().toStdString();
                for (const auto& value : recipe.value("elements").toArray())
                {
                    const auto element = value.toObject();
                    auto options = element.toVariantMap();
                    const bool text = element.value("type") == "text";
                    auto add = presentation_edit_command(text ? "addText" : "addShape", options, 0, 0);
                    check(apply_presentation_edit(scene, add).error == PresentationEditError::None,
                        "art recipe materializes via public core edits");
                    const int index = static_cast<int>(scene.slides[0].shapes.size()) - 1;
                    const auto format = presentation_edit_command(text ? "formatText" : "formatShape",
                        element.value("style").toObject().toVariantMap(), 0, index);
                    check(apply_presentation_edit(scene, format).error == PresentationEditError::None,
                        "art recipe typography and paint materialize via public edits");
                    if (text)
                    {
                        texts.append(element.value("text").toString());
                        fingerprint.append(QJsonArray{element.value("x"), element.value("y"),
                            element.value("width"), element.value("height")});
                    }
                }
                for (const auto& block : blocks)
                {
                    check(texts.contains(block.toObject().value("text").toString()) &&
                            texts.contains(block.toObject().value("heading").toString()),
                        "art direction retains each supplied heading and fact without made-up values");
                }
                fingerprints.append(fingerprint);
                if (number == 4)
                    check(fingerprint != previous,
                        "consecutive pages of equal information use different geometry within one art "
                        "direction");
                previous = fingerprint;
                const auto image = render(scene);
                check(
                    image.save(output.filePath(QString("presentation-art-%1-%2.png").arg(style).arg(number))),
                    "save the complete offscreen art composition for review");
                deck.slides.push_back(scene.slides[0]);
            }
        }
        for (int left = 0; left < fingerprints.size(); ++left)
            for (int right = left + 1; right < fingerprints.size(); ++right)
                if (left % 2 == right % 2)
                    check(fingerprints[left] != fingerprints[right],
                        "every same-number page differs geometrically across all four art directions");
        check(save_presentation_file(output.filePath("presentation-art-directions.pptx").toStdString(),
                  serialize_presentation(deck).parts, {})
                    .error == PresentationError::None,
            "save all actual art directions and consecutive pages as an independent deck");
    }
}

int run_presentation_pattern_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--render")
    {
        const QFileInfo file(QString::fromLocal8Bit(argv[2]));
        const auto loaded = load_presentation_file(file.absoluteFilePath().toStdString());
        if (loaded.error != PresentationError::None)
            return 2;
        const QDir directory(file.absolutePath());
        for (std::size_t index = 0; index < loaded.scene.slides.size(); ++index)
        {
            const auto path =
                directory.filePath(QString("live-presentation-%1.png").arg(index + 1, 3, 10, QChar('0')));
            if (!render(loaded.scene, static_cast<int>(index)).save(path))
                return 3;
            std::cout << path.toStdString() << '\n';
        }
        return 0;
    }
    const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
    const QDir output(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY));
    test_pattern_roundtrip(output);
    test_preservation_and_dashes(output);
    test_bridge(source, output);
    test_art_layouts(output);
    QThreadPool::globalInstance()->waitForDone();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_presentation_pattern_tests(argc, argv);
}
