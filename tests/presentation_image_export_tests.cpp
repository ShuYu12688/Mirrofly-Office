#include "presentation_image_export.hpp"
#include "presentation_image_export_bridge.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

namespace
{
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }

    bool wait(mirrorfly::PresentationImageExportBridge& bridge)
    {
        QElapsedTimer timer;
        timer.start();
        while (bridge.busy() && timer.elapsed() < 20000)
        {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        return !bridge.busy();
    }
}

int run_presentation_image_export_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    using namespace mirrorfly;
    auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
    scene->width = 320;
    scene->height = 180;
    scene->slides[0].background.color = "#FF0000";
    scene->slides.emplace_back();
    scene->slides[1].hidden = true;
    scene->slides[1].background.color = "#00FF00";
    scene->slides.emplace_back();
    scene->slides[2].background.color = "#0000FF";
    const auto document = prepare_presentation(scene);
    QTemporaryDir directory;
    check(directory.isValid(), "temporary output parent exists");
    const QVariantMap theme{{"fontFamily", "Arial"}, {"fontSize", 14}};
    PresentationImageExportOptions options;
    options.long_edge = 1280;
    PresentationImageExportProgress progress;
    const auto png = render_presentation_images(
        document, 0, theme, directory.path(), QStringLiteral("Course.pptx"), options, progress);
    const QDir png_folder(png.path);
    check(png.success && progress.total == 2 && progress.completed == 2 &&
            png_folder.exists(QStringLiteral("slide-001.png")) &&
            !png_folder.exists(QStringLiteral("slide-002.png")) &&
            png_folder.exists(QStringLiteral("slide-003.png")),
        "all-slide PNG export skips hidden pages and uses original page numbers");
    const QImage first(png_folder.filePath(QStringLiteral("slide-001.png")));
    const QImage third(png_folder.filePath(QStringLiteral("slide-003.png")));
    check(first.size() == QSize(1280, 720) && third.size() == QSize(1280, 720) &&
            first.pixelColor(640, 360) == QColor("#FF0000") &&
            third.pixelColor(640, 360) == QColor("#0000FF"),
        "PNG files read back at the selected aspect ratio and retain page pixels");

    options.format = "jpg";
    options.scope = "current";
    PresentationImageExportProgress jpg_progress;
    const auto jpg = render_presentation_images(
        document, 1, theme, directory.path(), QStringLiteral("Course.pptx"), options, jpg_progress);
    const QImage hidden(QDir(jpg.path).filePath(QStringLiteral("slide-002.jpg")));
    check(jpg.success && jpg.pages == 1 && hidden.size() == QSize(1280, 720) &&
            hidden.pixelColor(640, 360).green() > 230,
        "current-page JPG export includes a selected hidden slide and reads back");

    PresentationImageExportProgress cancelled;
    cancelled.cancelled.store(true);
    const auto cancelled_result = render_presentation_images(
        document, 0, theme, directory.path(), QStringLiteral("Course.pptx"), options, cancelled);
    check(!cancelled_result.success && cancelled_result.path.isEmpty() &&
            QDir(directory.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == 2,
        "cancelled export removes staging data and never publishes a result folder");

    PresentationImageExportBridge bridge(theme);
    bridge.registerSource([&]()
    {
        return PresentationImageExportSource{document, 0, QStringLiteral("Course.pptx"), {}};
    });
    check(!bridge.start(QUrl::fromLocalFile(directory.path()),
              {{"format", "png"}, {"scope", "all"}, {"longEdge", 999}}) &&
            !bridge.busy(),
        "unsupported dimensions reject before scheduling any file work");
    check(bridge.start(QUrl::fromLocalFile(directory.path()),
              {{"format", "png"}, {"scope", "all"}, {"longEdge", 1280}}) &&
            wait(bridge) && bridge.snapshot().value("success").toBool() &&
            bridge.snapshot().value("completed").toInt() == 2 &&
            QDir(bridge.snapshot().value("path").toString()).exists(QStringLiteral("slide-003.png")),
        "bridge reports asynchronous progress and publishes a separate final folder");
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_presentation_image_export_tests(argc, argv);
}
