#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QTimer>

#include <iostream>
#include <memory>

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

    void advance(int milliseconds)
    {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    }

    bool wait_phase(const QObject& item, const QString& phase, int timeout)
    {
        QElapsedTimer timer;
        timer.start();
        while (item.property("phase").toString() != phase && timer.elapsed() < timeout)
            advance(15);
        return item.property("phase").toString() == phase;
    }

    bool test_sidebar_reveal(QQmlEngine& engine, const QDir& source, const QVariantMap& theme)
    {
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/components/SidebarNavigation.qml")));
        std::unique_ptr<QObject> sidebar(component.createWithInitialProperties(
            {{"theme", theme}, {"controlsOpacity", 0.0}, {"width", 224}, {"height", 600}}));
        if (!sidebar)
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        auto* surface = sidebar->findChild<QQuickItem*>(QStringLiteral("sidebarSurface"));
        if (surface == nullptr || surface->opacity() != 0)
            return check(false, "the navigation surface is hidden during the black-to-white transition");
        sidebar->setProperty("controlsOpacity", 0.5);
        return check(surface->opacity() == 0.5, "navigation background fades with the home controls");
    }

    bool test_startup(QQmlEngine& engine, const QDir& source, QVariantMap theme)
    {
        QQuickItem viewport;
        viewport.setSize(QSizeF(1000, 720));
        QQuickItem destination(&viewport);
        destination.setPosition(QPointF(55, 60));
        destination.setSize(QSizeF(36, 36));
        QQmlComponent component(&engine, QUrl::fromLocalFile(source.filePath("ui/LoadingPage.qml")));
        std::unique_ptr<QObject> startup(component.createWithInitialProperties({{"theme", theme},
            {"width", 1000}, {"height", 720}, {"destinationMark", QVariant::fromValue(&destination)}}));
        if (!check(startup != nullptr, "construct the startup transition without an application window"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        auto* item = qobject_cast<QQuickItem*>(startup.get());
        item->setParentItem(&viewport);
        bool passed = check(startup->property("phase") == "loading" &&
                startup->property("backgroundOpacity").toDouble() == 1 &&
                startup->property("controlsOpacity").toDouble() == 0,
            "startup first frame is black and hides home controls");
        startup->setProperty("ready", true);
        passed = check(wait_phase(*startup, "ring", 300), "readiness starts the orbit fill") && passed;
        passed =
            check(wait_phase(*startup, "seal", 1100) && startup->property("readyOpacity").toDouble() == 0,
                "the orbit contracts before the ready label appears") &&
            passed;
        passed = check(wait_phase(*startup, "ready", 1100) &&
                         startup->property("backgroundOpacity").toDouble() == 1 &&
                         startup->property("ringScale").toDouble() > 0.9,
                     "orbit fills before the ready label, while the background remains black") &&
            passed;
        auto* label = startup->findChild<QObject*>(QStringLiteral("startupReadyText"));
        passed = check(label && label->property("text") == QStringLiteral("就绪"),
                     "startup shows the requested ready label") &&
            passed;
        passed = check(wait_phase(*startup, "travel", 700) &&
                         startup->property("backgroundOpacity").toDouble() == 1,
                     "logo travels before the home background appears") &&
            passed;
        passed = check(wait_phase(*startup, "background", 1200),
                     "background transition starts after logo travel") &&
            passed;
        auto* emblem = startup->findChild<QQuickItem*>(QStringLiteral("startupEmblem"));
        const auto arrived =
            emblem ? emblem->mapToItem(item, QPointF(emblem->width() / 2, emblem->height() / 2)) : QPointF{};
        const auto target = destination.mapToItem(item, QPointF(18, 18));
        passed = check(emblem && QLineF(arrived, target).length() < 1 &&
                         startup->property("controlsOpacity").toDouble() == 0,
                     "logo lands at the real sidebar mark before controls appear") &&
            passed;
        passed = check(wait_phase(*startup, "controls", 850) &&
                         startup->property("backgroundOpacity").toDouble() < 0.001,
                     "home controls fade in after the background becomes visible") &&
            passed;
        passed = check(wait_phase(*startup, "complete", 700) && !startup->property("visible").toBool() &&
                         startup->property("controlsOpacity").toDouble() > 0.999,
                     "startup releases the home after all phases") &&
            passed;
        startup->setProperty("ready", false);
        passed = check(startup->property("phase") == "loading" && startup->property("visible").toBool() &&
                         startup->property("backgroundOpacity").toDouble() == 1,
                     "replay resets the opaque first frame") &&
            passed;
        theme["motionEnabled"] = false;
        startup->setProperty("theme", theme);
        startup->setProperty("ready", true);
        advance(40);
        passed = check(startup->property("phase") == "complete" && !startup->property("visible").toBool(),
                     "reduced motion reveals the home without waiting on animation") &&
            passed;
        return passed;
    }

    bool test_window_frame(QQmlEngine& engine, const QDir& source, const QVariantMap& theme)
    {
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/components/WindowFrame.qml")));
        std::unique_ptr<QObject> frame(
            component.createWithInitialProperties({{"theme", theme}, {"width", 800}, {"height", 600}}));
        if (!check(frame != nullptr, "construct the shared window frame without an application window"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        auto* outline = frame->findChild<QObject*>(QStringLiteral("windowFrameOutline"));
        auto* titlebar = frame->findChild<QObject*>(QStringLiteral("windowFrameTitlebar"));
        bool passed =
            check(outline && titlebar && QQmlProperty(outline, "border.width").read().toDouble() == 1,
                "home retains the subtle outer line");
        frame->setProperty("outlineVisible", false);
        frame->setProperty("chromeVisible", false);
        passed = check(outline && titlebar && QQmlProperty(outline, "border.width").read().toDouble() == 0 &&
                         !titlebar->property("visible").toBool(),
                     "feature loading shows only its centered card without outer frame chrome") &&
            passed;
        frame->setProperty("isolatedLoading", true);
        auto* mask = frame->findChild<QObject*>(QStringLiteral("windowFrameMask"));
        auto* surface = frame->findChild<QObject*>(QStringLiteral("windowFrameSurface"));
        passed = check(mask && surface && mask->property("color").value<QColor>().alpha() == 0 &&
                         QQmlProperty(surface, "layer.enabled").read().toBool() &&
                         surface->property("visible").toBool(),
                     "loading masks the whole frame while retaining the editor render surface") &&
            passed;
        frame->setProperty("isolatedLoading", false);
        passed = check(mask->property("color").value<QColor>().alpha() == 255,
                     "loading restores the normal frame mask") &&
            passed;
        frame->setProperty("outlineVisible", true);
        frame->setProperty("chromeVisible", true);
        passed = check(outline && titlebar && QQmlProperty(outline, "border.width").read().toDouble() == 1 &&
                         titlebar->property("visible").toBool(),
                     "home restores the frame and titlebar after loading") &&
            passed;
        return passed;
    }

    bool test_intro(QQmlEngine& engine, const QDir& source, QVariantMap theme)
    {
        QQuickItem viewport;
        viewport.setSize(QSizeF(1040, 676));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/components/FeatureIntro.qml")));
        std::unique_ptr<QObject> intro(component.createWithInitialProperties(
            {{"theme", theme}, {"width", 1040}, {"height", 676}, {"contentReady", false}}));
        if (!check(intro != nullptr, "construct the feature transition without an application window"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        qobject_cast<QQuickItem*>(intro.get())->setParentItem(&viewport);
        const int interval = theme.value("featureIntroDuration").toInt();
        bool passed =
            check(!intro->property("running").toBool() && intro->property("duration").toInt() == interval,
                "transition starts idle and uses the configured presentation interval");
        auto* card = intro->findChild<QQuickItem*>(QStringLiteral("featureIntroCard"));
        const QPointF position = card ? card->position() : QPointF{};
        QMetaObject::invokeMethod(intro.get(), "play", Q_ARG(QVariant, QStringLiteral("slides")));
        passed = check(intro->property("running").toBool() && intro->property("visible").toBool(),
                     "activation immediately gates the editor behind the loading page") &&
            passed;
        advance(interval - 250);
        passed = check(intro->property("running").toBool() && !intro->property("elapsed").toBool(),
                     "the transition cannot finish early") &&
            passed;
        advance(350);
        passed = check(intro->property("running").toBool() && intro->property("elapsed").toBool(),
                     "an unfinished document remains gated after the animation interval") &&
            passed;
        intro->setProperty("contentReady", true);
        passed = check(!intro->property("running").toBool() && !intro->property("visible").toBool(),
                     "the editor is revealed only after both content and timing are ready") &&
            passed;
        theme["motionEnabled"] = false;
        intro->setProperty("theme", theme);
        QMetaObject::invokeMethod(intro.get(), "play", Q_ARG(QVariant, QStringLiteral("writer")));
        advance(450);
        QElapsedTimer replay_time;
        replay_time.start();
        QMetaObject::invokeMethod(intro.get(), "play", Q_ARG(QVariant, QStringLiteral("markdown")));
        advance(interval - 250);
        passed = check(intro->property("running").toBool() && !intro->property("motionActive").toBool() &&
                         intro->property("kind").toString() == QStringLiteral("markdown"),
                     "restarting a static transition resets its timer and feature palette") &&
            passed;
        while (intro->property("running").toBool() && replay_time.elapsed() < interval + 1000)
        {
            advance(20);
        }
        passed = check(!intro->property("running").toBool() && replay_time.elapsed() >= interval - 100,
                     "reduced-motion transitions retain the gate without a stuck loading page") &&
            passed;
        QMetaObject::invokeMethod(intro.get(), "beginLoading", Q_ARG(QVariant, QStringLiteral("slides")));
        intro->setProperty("loadingProgress", 0.65);
        intro->setProperty("loadingStatus", QStringLiteral("正在解析幻灯片 2/3"));
        auto* status = intro->findChild<QObject*>(QStringLiteral("featureLoadingStatus"));
        passed = check(status && status->property("text").toString().contains("65%"),
                     "loading status displays the actual percentage") &&
            passed;
        advance(interval + 150);
        passed = check(intro->property("running").toBool() && intro->property("loadingMode").toBool() &&
                         !intro->property("loadingComplete").toBool(),
                     "presentation loading remains gated after the minimum animation interval") &&
            passed;
        QMetaObject::invokeMethod(intro.get(), "finishLoading");
        passed = check(!intro->property("running").toBool() && !intro->property("visible").toBool() &&
                         intro->property("loadingComplete").toBool(),
                     "the loading page releases only after the first presentation frame") &&
            passed;
        passed = check(card && card->position() == position && card->rotation() == 0 && card->scale() == 1,
                     "the loading card stays fixed in place") &&
            passed;
        return passed;
    }

    int run_tests(int argc, char* argv[])
    {
        QGuiApplication application(argc, argv);
        QQmlEngine engine;
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
        const bool passed = test_sidebar_reveal(engine, source, theme) &&
            test_startup(engine, source, theme) && test_window_frame(engine, source, theme) &&
            test_intro(engine, source, theme);
        for (const auto& warning : warnings)
        {
            std::cerr << warning.toStdString() << '\n';
        }
        return passed && warnings.empty() ? 0 : 1;
    }
}

int main(int argc, char* argv[])
{
    return run_tests(argc, argv);
}
