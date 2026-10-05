#include "automation_bridge.hpp"
#include "office_ai_agent.hpp"
#include "office_ai_contract_adapter.hpp"
#include "office_ai_file_policy.hpp"
#include "office_ai_test_transport.hpp"
#include "spreadsheet_bridge.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/spreadsheet_storage.hpp>

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QThread>

#include <filesystem>
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

    QByteArray bytes(const QString& path)
    {
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "read isolated file");
        return file.readAll();
    }

    void wait_until(const std::function<bool()>& pending)
    {
        QElapsedTimer clock;
        clock.start();
        while (pending() && clock.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        check(!pending(), "isolated asynchronous operation completes");
    }

    class Root final : public QObject
    {
        Q_OBJECT
    public:
        Q_INVOKABLE bool automationReady() const
        {
            return true;
        }
        Q_INVOKABLE QVariantMap automationState() const
        {
            return {{"module", "sheets"}, {"pendingInput", false}};
        }
    };

    void test_selection_defaults()
    {
        using namespace mirrorfly;
        SpreadsheetBridge sheets;
        sheets.requestNew();
        Root root;
        AutomationBridge automation;
        automation.registerModule("sheets", &sheets);
        automation.setUiRoot(&root);
        const auto snapshot = []()
        {
            return QJsonDocument::fromJson(QByteArray::fromStdString(office_runtime_snapshot())).object();
        };
        const auto catalog =
            QJsonDocument::fromJson(QByteArray::fromStdString(office_action_catalog())).object();
        const auto signature = office_ai_action_signature(catalog, "sheets", "selectCell");
        check(signature.value("parameters").toArray().last().toObject().value("default") == false,
            "public schema describes the native selection default");
        for (const QJsonValue& args :
            {QJsonValue(QJsonArray{1, 2}), QJsonValue(QJsonObject{{"row", 1}, {"column", 2}})})
        {
            const auto normalized = office_ai_normalize_action(
                {{"module", "sheets"}, {"action", "selectCell"}, {"args", args}}, signature);
            check(normalized.value("ok").toBool() &&
                    normalized.value("step").toObject().value("args").toArray() == QJsonArray{1, 2, false},
                "named and ordered arguments share the same declared default");
        }
        const auto direct = QJsonDocument::fromJson(
            QByteArray::fromStdString(office_execute(QJsonDocument(
                QJsonObject{{"module", "sheets"}, {"action", "selectCell"}, {"args", QJsonArray{1, 2}},
                    {"expectedRevision", snapshot().value("revision")}})
                    .toJson(QJsonDocument::Compact)
                    .toStdString())))
                                .object();
        check(direct.value("ok").toBool() && sheets.cellInfo().value("row") == 1 &&
                sheets.cellInfo().value("column") == 2,
            "public execution accepts the same native optional default");
        for (const auto& args : {QJsonArray{1}, QJsonArray{1, 2, QJsonValue::Null}, QJsonArray{1, 2, "true"}})
            check(!office_ai_normalize_action(
                      {{"module", "sheets"}, {"action", "selectCell"}, {"args", args}}, signature)
                      .value("ok")
                      .toBool(),
                "required fields and explicit invalid booleans are not replaced by defaults");
    }

    void test_policy(const QString& path, const QString& copy)
    {
        using mirrorfly::OfficeAiFilePolicy;
        const QJsonObject document{
            {"active", true}, {"documentPath", path}, {"saveUrl", QUrl::fromLocalFile(path).toString()}};
        const QJsonObject runtime{{"modules", QJsonObject{{"sheets", document}}}};
        for (const auto& request : QStringList{QStringLiteral("保留原件"), QStringLiteral("另存为副本"),
                 QStringLiteral("不要覆盖原文件"), QStringLiteral("请不要改动源文件"),
                 QStringLiteral("不要动源文件"), QStringLiteral("源文件保持不变"),
                 QStringLiteral("禁止修改原稿"), "Don't touch the source file", "Do not modify the original",
                 "Keep the original unchanged", "save as new.xlsx"})
        {
            OfficeAiFilePolicy policy;
            policy.update(request, runtime);
            check(policy.check("sheets", "save", {}, document).value("error") == "source_file_protected",
                "current save obeys user file preservation intent");
            for (const auto* action : {"saveTo", "createEditableCopyTo"})
                check(!policy.check("word", action, {QUrl::fromLocalFile(path).toString()}, {})
                          .value("ok")
                          .toBool(),
                    "URL and cross-module writes cannot bypass protected paths");
            check(!policy.check("export", "start", {"sheets", path, QJsonObject{}}, {}).value("ok").toBool(),
                "PDF export shares source path protection");
            policy.saved(path);
            policy.update("continue", runtime);
            check(!policy.check("sheets", "saveTo", {path}, {}).value("ok").toBool(),
                "save receipt and resume never unprotect the source");
            policy.saved(copy);
            policy.opened(copy);
            check(policy.check("sheets", "save", {}, {{"documentPath", copy}}).value("ok").toBool(),
                "task-created copy can be reopened and saved normally");
            const QJsonObject copy_document{{"active", true}, {"documentPath", copy}};
            const QJsonObject copy_runtime{{"modules", QJsonObject{{"sheets", copy_document}}}};
            policy.beginTask(copy_runtime);
            policy.update(QStringLiteral("继续修改并保存当前副本"), copy_runtime);
            check(policy.check("sheets", "save", {}, copy_document).value("ok").toBool(),
                "active copy provenance survives a new task");
            policy.opened(path);
            check(!policy.check("sheets", "saveTo", {path}, {}).value("ok").toBool(),
                "new task still protects an opened original");
            policy.beginTask(copy_runtime);
            policy.update(QStringLiteral("保留当前副本不变，另存另一份"), copy_runtime);
            check(!policy.check("sheets", "save", {}, copy_document).value("ok").toBool() &&
                    policy.check("sheets", "saveTo", {copy + ".new"}, {}).value("ok").toBool(),
                "explicit new task preservation can protect a previously created copy");
            policy.beginTask(runtime);
            policy.update(QStringLiteral("保留原件"), runtime);
            policy.opened(copy);
            check(!policy.check("sheets", "save", {}, copy_document).value("ok").toBool(),
                "inactive copy history is dropped at task boundary");
        }
        OfficeAiFilePolicy policy;
        policy.update(QStringLiteral("修改后保存回原文件"), runtime);
        check(policy.check("sheets", "save", {}, document).value("ok").toBool(),
            "explicit in-place edit is not a preserve-original request");
        policy.update(QStringLiteral("继续，但保留原件"), {});
        check(!policy.check("sheets", "saveTo", {path}, {}).value("ok").toBool(),
            "later user constraint also protects previously opened originals");
        OfficeAiFilePolicy later;
        later.update(QStringLiteral("保留原件"), {});
        later.opened(QUrl::fromLocalFile(path).toString());
        check(!later.check("sheets", "saveTo", {path}, {}).value("ok").toBool(),
            "files opened after task start are protected");
#ifdef Q_OS_WIN
        check(!later.check("sheets", "saveTo", {path.toUpper()}, {}).value("ok").toBool(),
            "Windows case differences do not bypass protection");
#endif
        const auto link = path + ".hardlink";
        std::error_code error;
        std::filesystem::create_hard_link(std::filesystem::u8path(path.toUtf8().toStdString()),
            std::filesystem::u8path(link.toUtf8().toStdString()), error);
        check(!error, "create hardlink for alias protection test");
        if (!error)
            check(!later.check("sheets", "saveTo", {link}, {}).value("ok").toBool(),
                "hardlinked source aliases are protected");
        later = {};
        check(later.check("sheets", "saveTo", {path}, {}).value("ok").toBool(),
            "new task resets file intent without persistent memory");
    }

    void test_agent(const QString& directory)
    {
        using namespace mirrorfly;
        using namespace office_ai_test;
        const QString original = directory + "/original.xlsx";
        const QString copy = directory + "/copy.xlsx";
        SpreadsheetBridge sheets;
        sheets.requestNew();
        check(sheets.setCellValue(0, 0, "Original", "text"), "create isolated source cell");
        check(sheets.saveTo(QUrl::fromLocalFile(original)), "create isolated original");
        wait_until([&]()
        {
            return sheets.busy();
        });
        const auto before = bytes(original);
        check(sheets.setCellValue(0, 0, "Edited", "text"), "prepare unsaved edits");
        Root root;
        AutomationBridge automation;
        automation.registerModule("app", &root);
        automation.registerModule("sheets", &sheets);
        automation.setUiRoot(&root);
        Transport network(check);
        network.respond = [&](int turn, const QJsonObject& body) -> QJsonArray
        {
            if (turn == 0)
                return {call("office_save", {{"current", true}})};
            if (turn == 1 || turn == 2)
            {
                check(json(body).contains("source_file_protected") && bytes(original) == before,
                    "real tool loop rejects source writes before touching bytes");
                if (turn == 1)
                    return {call("office_batch",
                        {{"steps",
                            QJsonArray{QJsonObject{{"op", "sheets.saveTo"},
                                {"args", QJsonArray{QUrl::fromLocalFile(original).toString()}}}}}})};
                return {call("office_action", {{"op", "sheets.saveTo"}, {"args", QJsonArray{copy}}})};
            }
            if (turn == 3)
            {
                check(QFile::exists(copy) && !sheets.modified(), "save distinct copy completes on disk");
                return {call("office_action",
                    {{"op", "sheets.setCellValue"}, {"args", QJsonArray{0, 0, "Continued", "text"}}})};
            }
            if (turn == 4)
            {
                return {call("office_action", {{"op", "sheets.saveTo"}, {"args", QJsonArray{copy}}})};
            }
            if (turn == 6)
                return {call("office_action",
                    {{"op", "sheets.setCellValue"}, {"args", QJsonArray{0, 0, "Followup", "text"}}})};
            if (turn == 7)
                return {call("office_save", {{"current", true}})};
            return {};
        };
        OfficeAiAgent agent(nullptr, &network, directory + "/diagnostics.jsonl", directory);
        agent.configure("https://api.deepseek.com", "offline-model", "offline-key", "none");
        agent.start(QStringLiteral("保留原件，将修改保存到副本。"));
        wait_until([&]()
        {
            return agent.busy();
        });
        check(network.requests.size() == 6 && !agent.resumable() && !sheets.modified(),
            "blocked current and batched saves recover to a writable copy");
        check(bytes(original) == before, "source bytes stay identical after the complete model loop");
        const auto reopened = load_spreadsheet_file(copy.toStdString());
        check(reopened.error == SpreadsheetError::None && bytes(copy) != before,
            "copy reopens as a real modified workbook");
        const auto previous_copy = bytes(copy);
        agent.start(QStringLiteral("继续修改并保存当前副本，保留原件。"));
        wait_until([&]()
        {
            return agent.busy();
        });
        check(network.requests.size() == 9 && !agent.resumable() && !sheets.modified() &&
                bytes(copy) != previous_copy && bytes(original) == before,
            "same agent accepts and saves a subsequent task on its active copy");
        test_policy(original, copy);
    }
}

int run_office_ai_file_policy_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "isolated file policy test directory");
    test_selection_defaults();
    test_agent(directory.path());
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_office_ai_file_policy_tests(argc, argv);
}

#include "office_ai_file_policy_tests.moc"
