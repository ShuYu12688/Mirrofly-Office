#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>

#include <iostream>
#include <memory>

namespace
{
    void settle(int milliseconds = 300)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    void mouse_event(QQuickWindow& window, QEvent::Type type, const QPoint& global, Qt::MouseButtons buttons)
    {
        const QPointF local = window.mapFromGlobal(global);
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, local, local, global, button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &event);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    QList<QQuickItem*> visual_items(QQuickItem& parent)
    {
        QList<QQuickItem*> items = parent.childItems();
        for (auto* child : parent.childItems())
            items.append(visual_items(*child));
        return items;
    }

    bool drag(QQuickWindow& window, const char* area_name, const QPoint& delta)
    {
        auto* area = window.findChild<QQuickItem*>(QString::fromLatin1(area_name));
        if (area == nullptr)
            return false;
        const QPoint before = window.position();
        const QPoint start = window.mapToGlobal(area->mapToScene(QPointF(40, 16)).toPoint());
        mouse_event(window, QEvent::MouseButtonPress, start, Qt::LeftButton);
        mouse_event(window, QEvent::MouseMove, start + QPoint(3, 2), Qt::LeftButton);
        if (window.position() != before)
            return false;
        mouse_event(window, QEvent::MouseMove, start + delta, Qt::LeftButton);
        mouse_event(window, QEvent::MouseButtonRelease, start + delta, Qt::NoButton);
        return window.position() == before + delta;
    }

    bool check_drag_and_receipts(QQuickWindow& window, QVariantMap& client, QQuickItem& prompt)
    {
        if (!drag(window, "aiIslandHeaderDrag", QPoint(60, 12)))
        {
            std::cerr << "expanded header drag failed or moved below its threshold\n";
            return false;
        }
        const int center = window.x() + window.width() / 2;
        const int top = window.y();
        client["expanded"] = false;
        window.setProperty("client", client);
        settle();
        if (window.x() + window.width() / 2 != center || window.y() != top ||
            !drag(window, "aiIslandCapsuleDrag", QPoint(20, 8)))
        {
            std::cerr << "capsule drag or retained collapsed position failed\n";
            return false;
        }
        client["expanded"] = true;
        client["trace"] = QVariantList{QVariantMap{{"kind", "tool"}, {"title", "office_save"},
            {"state", "done"}, {"elapsedMs", 125}, {"detail", "RAW_TOOL_JSON_MUST_NOT_BE_DISPLAYED"}}};
        window.setProperty("client", client);
        settle();
        if (window.x() + window.width() / 2 != center + 20 || window.y() != top + 8 ||
            prompt.property("text") != "a")
        {
            std::cerr << "reopening must retain the dragged position and input draft\n";
            return false;
        }
        client["compact"] = true;
        client["busy"] = true;
        client["status"] = "思考中";
        client["answer"] = "RAW_MODEL_OUTPUT_MUST_NOT_BE_DISPLAYED";
        window.setProperty("client", client);
        settle();
        auto* status_line = window.findChild<QQuickItem*>(QStringLiteral("aiIslandStatusLine"));
        if (window.height() > 120 || prompt.isVisible() || status_line == nullptr ||
            status_line->property("text") != "思考中")
        {
            std::cerr << "a submitted task must use a small island with a single status line\n";
            return false;
        }
        for (auto* item : visual_items(*window.contentItem()))
        {
            const QString text = item->property("text").toString();
            if (item->isVisible() &&
                (text.contains("RAW_TOOL_JSON_MUST_NOT_BE_DISPLAYED") ||
                    text.contains("RAW_MODEL_OUTPUT_MUST_NOT_BE_DISPLAYED")))
                return false;
        }
        client["compact"] = false;
        client["busy"] = false;
        window.setProperty("client", client);
        settle();
        if (!prompt.isVisible() || prompt.property("text") != "a")
            return false;
        auto* area = window.findChild<QQuickItem*>(QStringLiteral("aiIslandHeaderDrag"));
        const QPoint start = window.mapToGlobal(area->mapToScene(QPointF(40, 16)).toPoint());
        mouse_event(window, QEvent::MouseButtonPress, start, Qt::LeftButton);
        mouse_event(window, QEvent::MouseMove, start + QPoint(2000, 2000), Qt::LeftButton);
        mouse_event(window, QEvent::MouseButtonRelease, start + QPoint(2000, 2000), Qt::NoButton);
        if (window.x() < 0 || window.y() < 0 || window.x() + window.width() > 800 ||
            window.y() + window.height() > 600)
        {
            std::cerr << "dragging must keep the island reachable inside the work area\n";
            return false;
        }
        const QPoint monitor_start = window.mapToGlobal(area->mapToScene(QPointF(40, 16)).toPoint());
        mouse_event(window, QEvent::MouseButtonPress, monitor_start, Qt::LeftButton);
        mouse_event(window, QEvent::MouseMove, monitor_start + QPoint(-1300, -20), Qt::LeftButton);
        mouse_event(window, QEvent::MouseButtonRelease, monitor_start + QPoint(-1300, -20), Qt::NoButton);
        settle();
        if (window.x() < -900 || window.x() + window.width() > -100 || window.y() < 30 ||
            window.y() + window.height() > 600)
        {
            std::cerr << "dragging to another monitor must respect its coordinates and work area: x="
                      << window.x() << " y=" << window.y() << " width=" << window.width()
                      << " height=" << window.height()
                      << " island=" << window.property("islandPosition").toPointF().x() << ','
                      << window.property("islandPosition").toPointF().y() << '\n';
            return false;
        }
        return true;
    }

    bool check_effort(QQmlEngine& engine, const QVariantMap& theme)
    {
        QQmlComponent component(&engine);
        component.setData(
            "import QtQuick\nAiIslandEffortControl { property string requestedEffort: \"\"; "
            "onEffortRequested: function(value) { requestedEffort = value; effortPending = true; } }",
            QUrl::fromLocalFile(
                QFileInfo(QStringLiteral(MIRRORFLY_ISLAND_QML)).absolutePath() + "/EffortTest.qml"));
        QQuickWindow window;
        window.resize(280, 80);
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"theme", theme}}));
        auto* control = qobject_cast<QQuickItem*>(object.get());
        if (!control)
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        control->setParentItem(window.contentItem());
        control->setSize(QSizeF(theme.value("islandEffortWidth").toDouble(), 30));
        control->setPosition(QPointF(20, 20));
        window.show();
        settle(50);
        auto* slider = control->findChild<QQuickItem*>("effortSlider");
        auto* thumb = control->findChild<QQuickItem*>("effortThumb");
        if (!slider || !thumb)
            return false;
        const QPoint start = window.mapToGlobal(slider->mapToScene(QPointF(15, 15)).toPoint());
        const QPoint end =
            window.mapToGlobal(slider->mapToScene(QPointF(slider->width() - 15, 15)).toPoint());
        mouse_event(window, QEvent::MouseButtonPress, start, Qt::LeftButton);
        mouse_event(window, QEvent::MouseMove, end, Qt::LeftButton);
        mouse_event(window, QEvent::MouseButtonRelease, end, Qt::NoButton);
        const qreal released_x = thumb->x();
        settle(250);
        if (control->property("requestedEffort") != "max" || slider->property("value").toInt() != 3 ||
            !qFuzzyCompare(thumb->x(), released_x))
        {
            std::cerr << "effort release snapped back while host acknowledgement was delayed\n";
            return false;
        }
        control->setProperty("effort", "max");
        control->setProperty("effortPending", false);
        settle(50);
        if (!qFuzzyCompare(thumb->x(), released_x))
        {
            std::cerr << "acknowledgement moved the thumb\n";
            return false;
        }
        const auto particles = [&]()
        {
            QList<QQuickItem*> found;
            for (auto* item : visual_items(*control))
                if (item->objectName() == "effortParticle")
                    found.append(item);
            return found;
        };
        const auto animated = particles();
        if (animated.size() != theme.value("islandEffortMaxParticles").toInt())
        {
            std::cerr << "particle animation count: " << animated.size() << '\n';
            return false;
        }
        for (auto* particle : animated)
            if (!particle->property("animating").toBool())
            {
                std::cerr << "visible maximum particle not running\n";
                return false;
            }
        const qreal phase = animated.first()->property("phase").toDouble();
        const qreal particle_x = animated.first()->x();
        QList<qreal> particle_lanes;
        for (auto* particle : animated)
            particle_lanes.append(particle->y());
        settle(50);
        if (qFuzzyCompare(phase, animated.first()->property("phase").toDouble()) ||
            animated.first()->x() >= particle_x)
            return false;
        for (qsizetype index = 0; index < animated.size(); ++index)
            if (!qFuzzyCompare(animated[index]->y(), particle_lanes[index]))
                return false;
        window.setVisibility(QWindow::Minimized);
        settle(25);
        for (auto* particle : particles())
            if (particle->property("animating").toBool())
                return false;
        window.setVisibility(QWindow::Windowed);
        settle(25);
        control->forceActiveFocus();
        QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &left);
        if (control->property("requestedEffort") != "high" || slider->property("value").toInt() != 2)
        {
            std::cerr << "keyboard change: request="
                      << control->property("requestedEffort").toString().toStdString()
                      << " value=" << slider->property("value").toDouble() << '\n';
            return false;
        }
        control->setProperty("effortPending", false);
        if (slider->property("value").toInt() != 3)
        {
            std::cerr << "rejected effort was not restored\n";
            return false;
        }
        control->setVisible(false);
        settle(25);
        for (auto* particle : particles())
            if (particle->property("animating").toBool())
            {
                std::cerr << "hidden particle still running\n";
                return false;
            }
        QVariantMap reduced = theme;
        reduced["motionEnabled"] = false;
        control->setProperty("theme", reduced);
        control->setVisible(true);
        settle(25);
        for (auto* particle : particles())
            if (particle->property("animating").toBool())
            {
                std::cerr << "reduced motion particle still running\n";
                return false;
            }
        return true;
    }

    int run_tests(int argc, char* argv[])
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QSG_RHI_BACKEND", "software");
        QQuickStyle::setStyle("Basic");
        QGuiApplication application(argc, argv);

        QFile theme_file(QStringLiteral(MIRRORFLY_THEME_JSON));
        if (!theme_file.open(QIODevice::ReadOnly))
            return 1;
        const QVariantMap theme = QJsonDocument::fromJson(theme_file.readAll()).object().toVariantMap();
        QVariantMap client{{"ready", true}, {"expanded", false}, {"configured", true}, {"busy", false},
            {"resumable", false}, {"status", "就绪"}, {"location", "首页"}, {"contextText", ""},
            {"trace", QVariantList{}}};
        QQmlEngine engine;
        if (!check_effort(engine, theme))
        {
            std::cerr << "effort drag, acknowledgement, keyboard or animation lifecycle failed\n";
            return 1;
        }
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MIRRORFLY_ISLAND_QML)));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"client", client},
            {"theme", theme},
            {"screens",
                QVariantList{QVariantMap{{"x", 0}, {"y", 0}, {"width", 800}, {"height", 600}},
                    QVariantMap{{"x", -900}, {"y", 0}, {"width", 800}, {"height", 600}, {"availableX", -900},
                        {"availableY", 30}, {"availableWidth", 800}, {"availableHeight", 570}}}}}));
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        if (window == nullptr)
        {
            std::cerr << component.errorString().toStdString() << '\n';
            return 1;
        }

        client["expanded"] = true;
        if (!window->setProperty("client", client))
            return 1;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1500)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QQuickItem* focus = window->activeFocusItem();
            if (focus != nullptr && focus->objectName() == "aiIslandPrompt")
            {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
                QCoreApplication::sendEvent(window, &press);
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
                QCoreApplication::sendEvent(window, &release);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                bool found_glow = false;
                for (QWindow* candidate : QGuiApplication::allWindows())
                {
                    if (candidate->objectName() != "aiScreenGlow")
                        continue;
                    found_glow = true;
                    if (candidate->transientParent() != nullptr)
                    {
                        std::cerr << "screen glow must not cover its transient parent\n";
                        return 1;
                    }
                }
                if (!found_glow)
                {
                    std::cerr << "screen glow window was not created\n";
                    return 1;
                }
                if (focus->property("text").toString() == "a")
                {
                    QElapsedTimer animation;
                    animation.start();
                    while (animation.elapsed() < 300)
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (window->height() < 200)
                    {
                        std::cerr << "expanded AI island was clipped to height " << window->height() << '\n';
                        return 1;
                    }
                    const QPointF local = focus->mapToScene(QPointF(focus->width() / 2, focus->height() / 2));
                    const QPointF global = window->mapToGlobal(local.toPoint());
                    QMouseEvent click(QEvent::MouseButtonPress, local, local, global, Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &click);
                    QMouseEvent release_click(QEvent::MouseButtonRelease, local, local, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &release_click);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    const QImage frame = window->grabWindow();
                    if (window->isVisible() && focus->isVisible() && !frame.isNull() &&
                        qGray(frame.pixelColor(local.toPoint()).rgb()) > 5)
                        return check_drag_and_receipts(*window, client, *focus) ? 0 : 1;
                    std::cerr << "clicking the prompt hid its input surface: window_visible="
                              << window->isVisible() << " input_visible=" << focus->isVisible()
                              << " frame_null=" << frame.isNull() << " pixel_gray="
                              << (frame.isNull() ? -1 : qGray(frame.pixelColor(local.toPoint()).rgb()))
                              << " local=" << local.x() << ',' << local.y() << " window=" << window->width()
                              << 'x' << window->height() << " input=" << focus->width() << 'x'
                              << focus->height() << '\n';
                    return 1;
                }
                std::cerr << "focused AI prompt did not accept typed text\n";
                return 1;
            }
        }
        QQuickItem* focus = window->activeFocusItem();
        std::cerr << "AI prompt did not receive focus after expansion; active item="
                  << (focus == nullptr ? "<none>" : focus->objectName().toStdString()) << '\n';
        return 1;
    }
}

int main(int argc, char* argv[])
{
    return run_tests(argc, argv);
}
