#include <mirrorfly/build_version.hpp>
#include <mirrorfly/ui.hpp>

#include "ai_island_host.hpp"
#include "automation_bridge.hpp"
#include "bridge.hpp"
#include "canvas_bridge.hpp"
#include "desktop_resident.hpp"
#include "document_routing.hpp"
#include "editor_tools.hpp"
#include "office_ai_agent.hpp"
#include "office_ai_settings.hpp"
#include "pdf_export_bridge.hpp"
#include "presentation_bridge.hpp"
#include "presentation_image_export_bridge.hpp"
#include "spreadsheet_bridge.hpp"
#include "startup_request.hpp"
#include "text_bridge.hpp"
#include "theme.hpp"
#include "window_decoration.hpp"
#include "word_bridge.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

void qml_register_types_Mirrorfly_Native();

namespace
{
    Q_LOGGING_CATEGORY(pptxRuntimeLatency, "mirrorfly.pptx.latency", QtInfoMsg)

    QMutex latency_log_mutex;
    QFile latency_log;
    QtMessageHandler previous_message_handler = nullptr;

    void latency_message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        const QByteArray line = qFormatLogMessage(type, context, message).toUtf8() + '\n';
        {
            QMutexLocker lock(&latency_log_mutex);
            if (latency_log.isOpen())
            {
                latency_log.write(line);
            }
        }
        if (previous_message_handler)
        {
            previous_message_handler(type, context, message);
        }
    }

    void configure_pptx_trace(QGuiApplication& application)
    {
        if (!application.arguments().contains("--pptx-trace"))
        {
            return;
        }
        const QDir app_data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        const QString directory = app_data.filePath("diagnostics");
        if (!QDir().mkpath(directory))
        {
            return;
        }
        latency_log.setFileName(QDir(directory).filePath(QStringLiteral("pptx-trace-%1.log")
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss"))));
        if (!latency_log.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return;
        }
        qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss.zzz} [thread:%{threadid}] %{category}: %{message}");
        previous_message_handler = qInstallMessageHandler(latency_message_handler);
        QString rules = QStringLiteral("mirrorfly.pptx.latency.debug=true\n");
        rules += QStringLiteral("qt.scenegraph.time.renderloop.debug=true\n");
        rules += QStringLiteral("qt.scenegraph.general.debug=true");
        QLoggingCategory::setFilterRules(rules);
        qCDebug(pptxRuntimeLatency) << "trace.begin" << MIRRORFLY_VERSION_STRING << latency_log.fileName();

        auto* heartbeat = new QTimer(&application);
        heartbeat->setTimerType(Qt::PreciseTimer);
        heartbeat->setInterval(20);
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        auto last = std::make_shared<qint64>(clock->elapsed());
        QObject::connect(heartbeat, &QTimer::timeout, &application, [clock, last]()
        {
            const qint64 now = clock->elapsed();
            const qint64 gap = now - *last;
            *last = now;
            if (gap > 100)
            {
                qCDebug(pptxRuntimeLatency) << "gui.event-gap.ms" << gap;
            }
        });
        heartbeat->start();
    }

    void trace_pptx_window(QQuickWindow* window, QGuiApplication& application)
    {
        if (!pptxRuntimeLatency().isDebugEnabled())
        {
            return;
        }
        auto sync = std::make_shared<QElapsedTimer>();
        QObject::connect(window, &QQuickWindow::beforeSynchronizing, &application, [sync]()
        {
            sync->start();
        }, Qt::DirectConnection);
        QObject::connect(window, &QQuickWindow::afterSynchronizing, &application, [sync]()
        {
            if (sync->isValid() && sync->elapsed() > 25)
            {
                qCDebug(pptxRuntimeLatency) << "scene.sync.ms" << sync->elapsed();
            }
        }, Qt::DirectConnection);
        auto rendering = std::make_shared<QElapsedTimer>();
        QObject::connect(window, &QQuickWindow::beforeRendering, &application, [rendering]()
        {
            rendering->start();
        }, Qt::DirectConnection);
        QObject::connect(window, &QQuickWindow::afterRendering, &application, [rendering]()
        {
            if (rendering->isValid() && rendering->elapsed() > 50)
            {
                qCDebug(pptxRuntimeLatency) << "scene.render.ms" << rendering->elapsed();
            }
        }, Qt::DirectConnection);
    }

    struct RuntimeReport
    {
        QElapsedTimer timer;
        qint64 ready_ms = -1;
        qint64 first_frame_ms = -1;
        QStringList warnings;
        QString directory;
        int captures = 0;
    };

    void write_report(const RuntimeReport& report)
    {
        if (report.directory.isEmpty())
        {
            return;
        }
        const QJsonObject metrics{{"ready_ms", report.ready_ms}, {"first_frame_ms", report.first_frame_ms},
            {"captures", report.captures}, {"qml_warnings", QJsonArray::fromStringList(report.warnings)},
            {"measurement", "In-process initialization; not OS cold-start latency"}};
        QFile file(QDir(report.directory).filePath("runtime-report.json"));
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(QJsonDocument(metrics).toJson());
        }
    }

    void capture_frame(QQuickWindow* window, RuntimeReport& report, const QString& filename)
    {
        const auto frame = window->grabWindow();
        if (!frame.isNull() && frame.save(QDir(report.directory).filePath(filename)))
        {
            ++report.captures;
        }
    }

    void configure_capture(QQuickWindow* window, mirrorfly::InterfaceBridge& bridge,
        QGuiApplication& application, RuntimeReport& report)
    {
        const auto args = application.arguments();
        const int index = args.indexOf("--capture-dir");
        if (index < 0 || index + 1 >= args.size())
        {
            return;
        }
        report.directory = args.at(index + 1);
        QDir().mkpath(report.directory);
        QTimer::singleShot(650, &application, [window, &bridge, &report]()
        {
            capture_frame(window, report, "home.png");
            bridge.replayLoading();
        });
        QTimer::singleShot(1300, &application, [window, &report]()
        {
            capture_frame(window, report, "loading.png");
        });
        if (args.contains("--quit-after-capture"))
        {
            QTimer::singleShot(2850, &application, [&application, &report]()
            {
                application.exit(report.captures == 2 && report.warnings.isEmpty() ? 0 : 2);
            });
        }
    }
}

namespace mirrorfly
{
    void configure_interface()
    {
        QQuickStyle::setStyle("Basic");
        QCoreApplication::setOrganizationName("Mirrorfly");
        QCoreApplication::setApplicationName("Mirrorfly Office");
        QCoreApplication::setApplicationVersion(MIRRORFLY_VERSION_STRING);
    }

    int run_interface(int argc, char* argv[])
    {
        RuntimeReport report;
        report.timer.start();
        QApplication application(argc, argv);
        configure_pptx_trace(application);
        if (application.arguments().contains("--test-session"))
        {
            QStandardPaths::setTestModeEnabled(true);
            QCoreApplication::setApplicationName("Mirrorfly Office Interface Test");
        }
        QGuiApplication::setWindowIcon(QIcon(":/mirrorfly/assets/mirrorfly-mark.svg"));
        const auto startup = startup_request(application.arguments());
        const QString data_directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        const bool test_session = application.arguments().contains("--test-session");
        DesktopResident resident(data_directory);
        if (!test_session)
        {
            const auto state = resident.prepare(startup.file.toLocalFile());
            if (state != DesktopResident::Startup::Primary)
                return state == DesktopResident::Startup::Forwarded ? 0 : 1;
        }
        qml_register_types_Mirrorfly_Native();

        const auto theme = load_theme(QCoreApplication::applicationDirPath());
        InterfaceBridge bridge(theme.values);
        TextEditorBridge text_editor;
        PresentationBridge presentation;
        SpreadsheetBridge spreadsheet;
        WordBridge word;
        CanvasBridge pdf(true);
        CanvasBridge mindmap(false);
        EditorTools editor_tools;
        PdfExportBridge pdf_exporter(theme.values);
        PresentationImageExportBridge image_exporter(theme.values);
        image_exporter.registerSource([&presentation]()
        {
            PresentationImageExportSource result;
            if (!presentation.active() || presentation.locked() || presentation.syncing())
            {
                result.error = QStringLiteral("演示文稿尚未准备好导出。");
                return result;
            }
            result.document = presentation.document().value<RenderPresentationPtr>();
            result.current_slide = presentation.currentSlide();
            result.document_name = presentation.documentName();
            return result;
        });
        pdf_exporter.registerSource("text", [&text_editor]()
        {
            return text_editor.pdfSource();
        });
        pdf_exporter.registerSource("word", [&word]()
        {
            return word.pdfSource();
        });
        pdf_exporter.registerSource("sheets", [&spreadsheet]()
        {
            return spreadsheet.pdfSource();
        });
        pdf_exporter.registerSource("slides", [&presentation]()
        {
            return presentation.pdfSource();
        });
        pdf_exporter.registerSource("pdf", [&pdf]()
        {
            return pdf.pdfSource();
        });
        pdf_exporter.registerSource("mindmap", [&mindmap]()
        {
            return mindmap.pdfSource();
        });
        AutomationBridge automation;
        OfficeAiSettings ai_settings(QDir(data_directory).filePath("ai-model.dat"));
        OfficeAiAgent ai_agent(nullptr, nullptr, {}, {}, test_session ? nullptr : &ai_settings);
        AiIslandHost ai_island(ai_agent);
        automation.registerModule("app", &bridge);
        automation.registerModule("text", &text_editor);
        automation.registerModule("word", &word);
        automation.registerModule("sheets", &spreadsheet);
        automation.registerModule("slides", &presentation);
        automation.registerModule("pdf", &pdf);
        automation.registerModule("mindmap", &mindmap);
        automation.registerModule("export", &pdf_exporter);
        automation.registerModule("images", &image_exporter);
        connect_document_routes(bridge, text_editor, presentation, spreadsheet, word, &pdf, &mindmap);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appBridge", &bridge);
        engine.rootContext()->setContextProperty("textEditor", &text_editor);
        engine.rootContext()->setContextProperty("editorTools", &editor_tools);
        engine.rootContext()->setContextProperty("presentation", &presentation);
        engine.rootContext()->setContextProperty("spreadsheet", &spreadsheet);
        engine.rootContext()->setContextProperty("wordEditor", &word);
        engine.rootContext()->setContextProperty("pdfEditor", &pdf);
        engine.rootContext()->setContextProperty("mindmapEditor", &mindmap);
        engine.rootContext()->setContextProperty("pdfExporter", &pdf_exporter);
        engine.rootContext()->setContextProperty("imageExporter", &image_exporter);
        engine.rootContext()->setContextProperty("aiAgent", &ai_agent);
        engine.rootContext()->setContextProperty("aiIsland", &ai_island);
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &application,
            [&report](const QList<QQmlError>& warnings)
        {
            for (const auto& warning : warnings)
            {
                report.warnings.append(warning.toString());
            }
        });
        engine.load(QUrl("qrc:/mirrorfly/ui/Main.qml"));
        if (engine.rootObjects().isEmpty())
        {
            return 1;
        }
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (window == nullptr)
        {
            return 1;
        }
        configure_transparent_window_frame(*window);
        bridge.setResidentEnabled(resident.attach(window, !test_session));
        QObject::connect(&bridge, &InterfaceBridge::hideMainRequested, &resident, &DesktopResident::hideMain);
        QObject::connect(
            &bridge, &InterfaceBridge::restoreMainRequested, &resident, &DesktopResident::restoreMain);
        QObject::connect(&bridge, &InterfaceBridge::quitApproved, &resident, &DesktopResident::completeExit);
        QObject::connect(&resident, &DesktopResident::exitRequested, &bridge, &InterfaceBridge::requestQuit);
        QObject::connect(&resident, &DesktopResident::islandRequested, &ai_island, [&]()
        {
            if (ai_agent.configured())
                ai_island.open();
            else
                resident.restoreMain();
        });
        QObject::connect(&resident, &DesktopResident::fileRequested, &bridge, [&bridge](const QString& path)
        {
            bridge.selectFile(QUrl::fromLocalFile(path));
        });
        trace_pptx_window(window, application);
        automation.setUiRoot(window);
        if (application.arguments().contains("--compact"))
        {
            window->resize(window->minimumSize());
        }
        QObject::connect(window, &QQuickWindow::frameSwapped, &application, [&report]()
        {
            if (report.first_frame_ms < 0)
            {
                report.first_frame_ms = report.timer.elapsed();
            }
        });
        QTimer::singleShot(0, &bridge, [&bridge, &report, &theme, startup]()
        {
            bridge.initialize();
            report.ready_ms = report.timer.elapsed();
            if (!startup.error.isEmpty())
                bridge.setNotice(startup.error);
            else if (!startup.file.isEmpty())
                bridge.selectFile(startup.file);
            if (!theme.warning.isEmpty())
            {
                bridge.setNotice(theme.warning);
            }
        });
        configure_capture(window, bridge, application, report);
        const int exit_code = application.exec();
        write_report(report);
        return exit_code;
    }
}
