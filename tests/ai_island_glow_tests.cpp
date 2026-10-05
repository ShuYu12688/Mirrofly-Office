#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>

#include <iostream>
#include <memory>

namespace
{
    int run_tests(int argc, char* argv[])
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QSG_RHI_BACKEND", "software");
        QGuiApplication application(argc, argv);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MIRRORFLY_GLOW_QML)));
        if (component.isError())
        {
            std::cerr << component.errorString().toStdString() << '\n';
            return 1;
        }

        const QVariantMap theme{{"islandGlowDepth", 100}, {"islandGlowOpacity", 0.68},
            {"islandGlowFalloff", 2.2}, {"islandBlue", "#62C8FF"}, {"islandPurple", "#A28BFF"},
            {"islandPink", "#FF80BD"}, {"islandAmber", "#FFD18B"}, {"motionEnabled", false}};
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{"screenGeometry", QVariantMap{{"x", 0}, {"y", 0}, {"width", 480}, {"height", 360}}},
                {"theme", theme}, {"glowing", true}}));
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        if (window == nullptr)
        {
            std::cerr << component.errorString().toStdString() << '\n';
            return 1;
        }

        QImage frame;
        QElapsedTimer timer;
        timer.start();
        while (frame.isNull() && timer.elapsed() < 3000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            frame = window->grabWindow();
        }
        if (frame.isNull())
        {
            std::cerr << "offscreen glow did not render\n";
            return 1;
        }

        const int depth = 100;
        const int mid_x = frame.width() / 2;
        const QColor edge_color = frame.pixelColor(mid_x, 2);
        const QColor middle_color = frame.pixelColor(mid_x, depth / 2);
        const QColor interior_color = frame.pixelColor(mid_x, depth + 12);
        const QColor square_color = frame.pixelColor(2, 2);
        const QColor rounded_color = frame.pixelColor(depth - 28, depth - 72);
        const int edge = qGray(edge_color.rgb());
        const int middle = qGray(middle_color.rgb());
        const int interior = qGray(interior_color.rgb());
        const int square_corner = qGray(square_color.rgb());
        const int rounded_corner = qGray(rounded_color.rgb());
        const bool passed =
            edge > middle && middle > interior && square_corner > rounded_corner && rounded_corner > interior;
        bool smooth = true;
        for (int y = 3; y < depth; ++y)
        {
            const int previous = qGray(frame.pixelColor(mid_x, y - 1).rgb());
            const int current = qGray(frame.pixelColor(mid_x, y).rgb());
            smooth = smooth && current <= previous + 1 && previous - current <= 5;
        }
        bool corners_filled = true;
        for (const QPoint& point : {QPoint(1, 1), QPoint(frame.width() - 2, 1), QPoint(1, frame.height() - 2),
                 QPoint(frame.width() - 2, frame.height() - 2)})
            corners_filled = corners_filled && qGray(frame.pixelColor(point).rgb()) > 20;
        if (!passed)
        {
            std::cerr << "glow pixels: edge=" << edge << " middle=" << middle << " interior=" << interior
                      << " square_corner=" << square_corner << " rounded_corner=" << rounded_corner << '\n';
            std::cerr << "rgb: edge=" << edge_color.name().toStdString()
                      << " middle=" << middle_color.name().toStdString()
                      << " interior=" << interior_color.name().toStdString()
                      << " square=" << square_color.name().toStdString()
                      << " rounded=" << rounded_color.name().toStdString() << '\n';
        }
        if (!smooth || !corners_filled)
            std::cerr << "glow falloff must be smooth and all outer corners filled\n";
        return passed && smooth && corners_filled ? 0 : 1;
    }
}

int main(int argc, char* argv[])
{
    return run_tests(argc, argv);
}
