#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
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
            std::cerr << message << '\n';
        return condition;
    }

    QVariant invoke(QObject* object, const char* method, const QVariant& value)
    {
        QVariant result;
        QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, value));
        return result;
    }

    QQuickItem* visual_child(QQuickItem* parent, const QString& name)
    {
        if (parent->objectName() == name)
            return parent;
        for (auto* child : parent->childItems())
        {
            if (auto* result = visual_child(child, name))
                return result;
        }
        return nullptr;
    }
}

int run_home_workspace_tests(int argc, char* argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication application(argc, argv);
    QQmlEngine engine;
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& errors)
    {
        for (const auto& error : errors)
            warnings.append(error.toString());
    });
    const QDir source(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
    QFile theme_file(source.filePath("config/theme.json"));
    if (!theme_file.open(QIODevice::ReadOnly))
        return 1;
    const auto theme = QJsonDocument::fromJson(theme_file.readAll()).object().toVariantMap();
    QQmlComponent agent_component(&engine);
    agent_component.setData(R"qml(import QtQml
QtObject {
    property bool configured: false
    property bool busy: false
    property string modelAddress: "https://api.deepseek.com"
    property string modelName: "deepseek-flash"
    property string thinkingEffort: "none"
    property string status: "尚未配置"
    property var usageStats: ({ input: 1200, output: 300, cached: 400, total: 1500 })
    function configure(address, model, key, effort) { return true; }
    function testConnection() {}
})qml",
        QUrl{});
    std::unique_ptr<QObject> agent(agent_component.create());
    if (!agent)
        return 1;
    QQmlComponent component(&engine, QUrl::fromLocalFile(source.filePath("ui/HomePage.qml")));
    std::unique_ptr<QObject> page(component.createWithInitialProperties(
        {{"theme", theme}, {"agent", QVariant::fromValue(agent.get())}, {"width", 1420}, {"height", 900}}));
    if (!page)
    {
        std::cerr << component.errorString().toStdString();
        return 1;
    }
    QQuickWindow window;
    window.resize(1420, 900);
    qobject_cast<QQuickItem*>(page.get())->setParentItem(window.contentItem());
    window.show();
    QTest::qWait(70);
    auto* dashboard = page->findChild<QObject*>("homeDashboard");
    auto* workspace = page->findChild<QQuickItem*>("createWorkspace");
    auto* timer = page->findChild<QObject*>("pomodoroCard");
    bool passed =
        check(dashboard && workspace && timer, "home composition contains isolated dashboard and tools");
    if (!passed)
        return 1;
    auto* clock_panel = page->findChild<QQuickItem*>("dashboardClockPanel");
    auto* note_panel = page->findChild<QQuickItem*>("dashboardNotePanel");
    auto* focus_panel = qobject_cast<QQuickItem*>(timer);
    passed = check(clock_panel && note_panel && focus_panel &&
                     focus_panel->x() >= clock_panel->x() + clock_panel->width() &&
                     note_panel->y() >= clock_panel->y() + clock_panel->height(),
                 "the asymmetric home layout separates clock, writing space and tall focus panel") &&
        passed;
    auto* clock = visual_child(qobject_cast<QQuickItem*>(page.get()), "homeAnalogClock");
    if (!check(clock != nullptr, "the home page has a primary analog clock"))
        return 1;
    const QDateTime fixed_time(QDate(2026, 10, 2), QTime(3, 15, 30));
    dashboard->setProperty("now", fixed_time);
    passed = check(qAbs(clock->property("hourAngle").toDouble() - 97.75) < 0.001 &&
                     qAbs(clock->property("minuteAngle").toDouble() - 93) < 0.001 &&
                     qAbs(clock->property("secondAngle").toDouble() - 180) < 0.001,
                 "analog hands agree with the real time including partial minutes and hours") &&
        passed;
    auto* digital = page->findChild<QObject*>("dashboardClock");
    passed = check(digital && digital->property("text").toString() == "03:15",
                 "digital time remains a short auxiliary label") &&
        passed;
    auto* frame = visual_child(qobject_cast<QQuickItem*>(page.get()), "homeSpectrumFrame");
    passed = check(!frame, "the home page has no outer rainbow frame") && passed;
    for (const auto& sample :
        {std::pair<int, QString>{2, "夜深了"}, {9, "早上好"}, {12, "中午好"}, {16, "下午好"}, {21, "晚上好"}})
        passed = check(invoke(dashboard, "greetingAt", sample.first).toString().startsWith(sample.second),
                     "time-of-day greetings cover all periods") &&
            passed;
    auto* total = page->findChild<QObject*>("dashboardTokenTotal");
    passed = check(total && total->property("text").toString().contains("1,500"),
                 "the dashboard renders structured provider token statistics") &&
        passed;
    invoke(timer, "selectMode", false);
    QMetaObject::invokeMethod(timer, "toggleTimer");
    passed = check(timer->property("running").toBool(), "the focus timer starts") && passed;
    invoke(timer, "tick", timer->property("deadline").toDouble() + 1);
    passed = check(!timer->property("running").toBool() && timer->property("resting").toBool() &&
                     timer->property("completed").toInt() == 1 && timer->property("remaining").toInt() == 300,
                 "elapsed focus time completes once and prepares a five-minute break") &&
        passed;
    invoke(timer, "selectMode", false);
    QMetaObject::invokeMethod(timer, "toggleTimer");
    QMetaObject::invokeMethod(timer, "toggleTimer");
    passed = check(!timer->property("running").toBool() && timer->property("remaining").toInt() > 1498,
                 "pause preserves the remaining time") &&
        passed;
    invoke(page.get(), "navigate", "create");
    QTest::qWait(70);
    QSignalSpy creates(page.get(), SIGNAL(createRequested(QString)));
    auto* word = visual_child(workspace, "createTile_word");
    auto* slides = visual_child(workspace, "createTile_slides");
    auto* sheets = visual_child(workspace, "createTile_sheets");
    passed = check(workspace->property("columns").toInt() == 4 && word && slides && sheets &&
                     word->height() > sheets->height() && slides->width() > sheets->width(),
                 "the desktop creation view uses genuinely different tile sizes") &&
        passed;
    for (const auto& kind : {"word", "slides", "sheets", "markdown", "pdf", "mindmap", "writer"})
    {
        auto* tile = visual_child(workspace, QStringLiteral("createTile_") + kind);
        if (tile)
            QMetaObject::invokeMethod(tile, "clicked");
    }
    passed =
        check(creates.size() == 7, "all seven tiles route through the existing public creation signal") &&
        passed;
    page->setProperty("width", 960);
    QTest::qWait(50);
    passed = check(workspace->property("columns").toInt() == 2 && word && word->width() > 0,
                 "medium windows reflow the creation view into two columns") &&
        passed;
    page->setProperty("width", 740);
    QTest::qWait(50);
    passed = check(workspace->property("columns").toInt() == 1 && sheets && sheets->width() > 0,
                 "narrow content reflows without losing a feature entry") &&
        passed;
    page->setProperty("files",
        QVariantList{QVariantMap{{"name", "sample.docx"}, {"path", "C:/sample.docx"}, {"kind", "word"},
            {"modified", "今天"}, {"sizeText", "2 KB"}, {"starred", false}}});
    invoke(page.get(), "navigate", "recent");
    QTest::qWait(50);
    auto* row = visual_child(qobject_cast<QQuickItem*>(page.get()), "recentFileRow");
    QSignalSpy opens(page.get(), SIGNAL(fileRequested(QString)));
    if (row)
        QMetaObject::invokeMethod(row, "clicked");
    passed = check(opens.size() == 1 && opens.first().first() == "C:/sample.docx",
                 "redesigned recent rows preserve the actual file-opening route") &&
        passed;
    invoke(page.get(), "navigate", "ai");
    auto* address = page->findChild<QObject*>("aiAddressField");
    auto* key = page->findChild<QObject*>("aiKeyField");
    passed = check(address && key && address->property("text") == "https://api.deepseek.com" &&
                     key->property("text").toString().isEmpty(),
                 "AI configuration retains the default address and never exposes a saved key") &&
        passed;
    window.hide();
    for (const auto& warning : warnings)
        std::cerr << warning.toStdString() << '\n';
    return passed && warnings.isEmpty() ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_home_workspace_tests(argc, argv);
}
