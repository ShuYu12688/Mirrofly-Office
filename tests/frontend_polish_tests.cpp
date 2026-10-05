#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <iostream>
#include <memory>

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

    QQuickItem* visual_child(QQuickItem* item, const QString& name)
    {
        if (item->objectName() == name)
            return item;
        for (auto* child : item->childItems())
            if (auto* found = visual_child(child, name))
                return found;
        return nullptr;
    }

    std::unique_ptr<QObject> create(
        QQmlEngine& engine, const QDir& source, const QString& path, const QVariantMap& properties)
    {
        QQmlComponent component(&engine, QUrl::fromLocalFile(source.filePath(path)));
        std::unique_ptr<QObject> object(component.createWithInitialProperties(properties));
        if (!object)
            std::cerr << component.errorString().toStdString();
        return object;
    }

    int column_luminance(const QImage& image, int x)
    {
        int maximum = 0;
        for (int y = 0; y < image.height(); ++y)
            maximum = std::max(maximum, qGray(image.pixelColor(x, y).rgb()));
        return maximum;
    }

    bool test_ribbon(QQmlEngine& engine, const QDir& source, const QVariantMap& theme)
    {
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(640, 120);
        auto ribbon = create(engine, source, "ui/components/SpectrumRibbon.qml",
            {{"theme", theme}, {"width", 640}, {"height", 120}});
        if (!ribbon)
            return false;
        qobject_cast<QQuickItem*>(ribbon.get())->setParentItem(window.contentItem());
        window.show();
        QTest::qWait(160);
        const auto pixels = window.grabWindow();
        return check(!pixels.isNull() && column_luminance(pixels, 0) < 5 &&
                column_luminance(pixels, pixels.width() - 1) < 5 &&
                column_luminance(pixels, pixels.width() / 5) > 20,
            "both ribbon ends fade to transparency while the interior remains visible");
    }

    bool test_ribbon_designs(QQmlEngine& engine, const QDir& source, QVariantMap theme)
    {
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(640, 120);
        theme["motionEnabled"] = false;
        auto ribbon = create(engine, source, "ui/components/WorkspaceRibbon.qml",
            {{"theme", theme}, {"width", 640}, {"height", 120}});
        if (!ribbon)
            return false;
        qobject_cast<QQuickItem*>(ribbon.get())->setParentItem(window.contentItem());
        window.show();
        QList<QImage> pictures;
        bool passed = true;
        for (int design = 0; design < 5; ++design)
        {
            ribbon->setProperty("design", design);
            QTest::qWait(65);
            const auto pixels = window.grabWindow();
            passed = check(!pixels.isNull() && column_luminance(pixels, 0) < 5 &&
                             column_luminance(pixels, pixels.width() - 1) < 5,
                         "each ribbon design keeps soft, transparent ends") &&
                passed;
            for (const auto& earlier : pictures)
                passed = check(earlier != pixels, "all five ribbon designs have distinct geometry") && passed;
            pictures.append(pixels);
        }
        auto* emblem = visual_child(qobject_cast<QQuickItem*>(ribbon.get()), "ribbonEmblem");
        if (!emblem)
            return check(false, "ribbon has a separate emblem with fixed proportions");
        const double original_scale = emblem->property("shapeScale").toDouble();
        ribbon->setProperty("width", 1040);
        QTest::qWait(35);
        passed = check(qFuzzyCompare(emblem->property("shapeScale").toDouble(), original_scale),
                     "a wider header lengthens tails without stretching the knot silhouette") &&
            passed;
        ribbon->setProperty("width", 640);
        ribbon->setProperty("design", 0);
        theme["motionEnabled"] = true;
        theme["workspaceRibbonHold"] = 80;
        theme["workspaceRibbonFade"] = 30;
        ribbon->setProperty("theme", theme);
        QTest::qWait(180);
        passed = check(ribbon->property("design").toInt() == 1,
                     "ribbon first holds, fades out and then reveals the next design") &&
            passed;
        passed = check(QTest::qWaitFor(
                           [&]()
        {
            return ribbon->property("design").toInt() == 0;
        }, 250),
                     "bow fades back to a straight ribbon before the next knot") &&
            passed;
        passed = check(QTest::qWaitFor(
                           [&]()
        {
            return ribbon->property("design").toInt() == 2;
        }, 250),
                     "straight ribbon then fades into the Chinese knot") &&
            passed;
        window.hide();
        passed =
            check(!ribbon->property("motionActive").toBool() && ribbon->property("reveal").toDouble() == 1,
                "hidden ribbon stops its timer and resets to a static shape") &&
            passed;
        return passed;
    }

    bool test_loader(QQmlEngine& engine, const QDir& source, const QVariantMap& theme)
    {
        QQuickWindow window;
        window.setColor(QColor("#203050"));
        window.resize(800, 600);
        auto loader = create(engine, source, "ui/components/FeatureIntro.qml",
            {{"theme", theme}, {"width", 800}, {"height", 600}, {"contentReady", false}});
        if (!loader)
            return false;
        qobject_cast<QQuickItem*>(loader.get())->setParentItem(window.contentItem());
        QMetaObject::invokeMethod(loader.get(), "beginLoading", Q_ARG(QVariant, QStringLiteral("word")));
        loader->setProperty("loadingProgress", 0.65);
        window.show();
        QTest::qWait(theme.value("featureLoadingArrivalDuration").toInt() + 100);
        const auto pixels = window.grabWindow();
        auto* status = loader->findChild<QObject*>("featureLoadingStatus");
        return check(!pixels.isNull() && pixels.pixelColor(5, 5) == window.color() &&
                pixels.pixelColor(795, 595) == window.color() &&
                qGray(pixels.pixelColor(400, 300).rgb()) > 200 && status &&
                status->property("text").toString().contains("65%"),
            "loading leaves the viewport transparent and shows real progress on the paper card");
    }

    bool test_effort(QQmlEngine& engine, const QDir& source, QVariantMap theme)
    {
        QQuickWindow window;
        window.resize(180, 70);
        theme["motionEnabled"] = true;
        auto control = create(engine, source, "ui/AiIslandEffortControl.qml",
            {{"theme", theme}, {"width", 140}, {"height", 30}, {"effort", "low"}});
        if (!control)
            return false;
        auto* item = qobject_cast<QQuickItem*>(control.get());
        item->setPosition(QPointF(10, 20));
        item->setParentItem(window.contentItem());
        window.show();
        QTest::qWait(100);
        auto* maximum = visual_child(item, "effortSlider");
        if (!maximum)
            return check(false, "effort slider exists");
        auto* track = visual_child(item, "effortTrack");
        auto* thumb = visual_child(item, "effortThumb");
        bool passed = check(track && thumb && track->height() == 30 &&
                track->property("radius").toDouble() == 15 && thumb->width() == thumb->height() &&
                thumb->property("radius").toDouble() == thumb->width() / 2,
            "effort control has a pill track enclosing a circular thumb");
        QSignalSpy requested(control.get(), SIGNAL(effortRequested(QString)));
        const auto position = maximum->mapToScene(QPointF(maximum->width() - 9, 15)).toPoint();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, position);
        passed = check(requested.size() == 1 && requested.first().first() == "max" &&
                         control->property("effort").toString() == "low" &&
                         control->property("selectedIndex").toInt() == 1,
                     "effort selection requests the public interface and waits for confirmed state") &&
            passed;
        control->setProperty("effort", "max");
        QTest::qWait(90);
        passed = check(control->property("selectedIndex").toInt() == 3 &&
                         control->property("motionPhase").toDouble() > 0,
                     "maximum effort drives its stronger slider motion") &&
            passed;
        theme["motionEnabled"] = false;
        control->setProperty("theme", theme);
        passed = check(control->property("motionPhase").toDouble() == 0,
                     "reduced motion immediately stops slider motion") &&
            passed;
        control->setProperty("enabled", false);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, position);
        passed = check(requested.size() == 1, "disabled effort cannot submit another change") && passed;
        return passed;
    }

    bool test_breath(QQmlEngine& engine, const QDir& source, QVariantMap theme)
    {
        theme["motionEnabled"] = true;
        theme["islandGlowBreathPeriod"] = 400;
        auto glow = create(engine, source, "ui/ScreenGlowWindow.qml",
            {{"theme", theme}, {"glowing", true},
                {"screenGeometry", QVariantMap{{"x", 0}, {"y", 0}, {"width", 480}, {"height", 360}}}});
        auto* window = qobject_cast<QQuickWindow*>(glow.get());
        if (!window)
            return false;
        QTest::qWait(120);
        const double breath = glow->property("breath").toDouble();
        bool passed = check(breath >= 0.92 && breath < 0.998 && window->opacity() == 1 &&
                window->flags().testFlag(Qt::WindowTransparentForInput) &&
                window->flags().testFlag(Qt::WindowDoesNotAcceptFocus),
            "glow breathes gently inside its surface without changing native opacity or input flags");
        glow->setProperty("glowing", false);
        passed = check(!glow->property("motionActive").toBool(), "hidden glow stops breathing") && passed;
        return passed;
    }
}

int run_frontend_polish_tests(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QQuickWindow::setDefaultAlphaBuffer(true);
    QGuiApplication application(argc, argv);
    QQmlEngine engine;
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& errors)
    {
        for (const auto& error : errors)
            warnings.append(error.toString());
    });
    const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
    QFile file(source.filePath("config/theme.json"));
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    const auto theme = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    bool passed = test_ribbon(engine, source, theme);
    passed = test_ribbon_designs(engine, source, theme) && passed;
    passed = test_loader(engine, source, theme) && passed;
    passed = test_effort(engine, source, theme) && passed;
    passed = test_breath(engine, source, theme) && passed;
    for (const auto& warning : warnings)
        std::cerr << warning.toStdString() << '\n';
    return passed && warnings.isEmpty() ? 0 : 1;
}
int main(int argc, char* argv[])
{
    return run_frontend_polish_tests(argc, argv);
}
