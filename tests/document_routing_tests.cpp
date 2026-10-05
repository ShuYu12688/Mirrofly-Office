#include "bridge.hpp"
#include "document_routing.hpp"
#include "presentation_bridge.hpp"
#include "spreadsheet_bridge.hpp"
#include "text_bridge.hpp"
#include "word_bridge.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

#include <iostream>

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

    bool wait_for_routes(mirrorfly::TextEditorBridge& text, mirrorfly::PresentationBridge& slides,
        mirrorfly::SpreadsheetBridge* sheets = nullptr)
    {
        if (!text.busy() && !slides.busy() && (!sheets || !sheets->busy()))
        {
            return true;
        }
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        const auto finish = [&]()
        {
            if (!text.busy() && !slides.busy() && (!sheets || !sheets->busy()))
            {
                QTimer::singleShot(0, &loop, &QEventLoop::quit);
            }
        };
        QObject::connect(&text, &mirrorfly::TextEditorBridge::stateChanged, &loop, finish);
        QObject::connect(&slides, &mirrorfly::PresentationBridge::stateChanged, &loop, finish);
        if (sheets)
        {
            QObject::connect(sheets, &mirrorfly::SpreadsheetBridge::stateChanged, &loop, finish);
        }
        timeout.start(10000);
        loop.exec();
        return check(!text.busy() && !slides.busy() && (!sheets || !sheets->busy()),
            "cross-document operation completes");
    }

    bool test_document_routes()
    {
        QTemporaryDir directory;
        mirrorfly::InterfaceBridge interface_bridge({});
        mirrorfly::TextEditorBridge text;
        mirrorfly::PresentationBridge slides;
        mirrorfly::SpreadsheetBridge sheets;
        mirrorfly::WordBridge word;
        mirrorfly::connect_document_routes(interface_bridge, text, slides, sheets, word);
        int slide_confirmations = 0;
        int text_confirmations = 0;
        QObject::connect(&slides, &mirrorfly::PresentationBridge::confirmUnsavedRequested, &slides, [&]()
        {
            ++slide_confirmations;
        });
        QObject::connect(&text, &mirrorfly::TextEditorBridge::confirmUnsavedRequested, &text, [&]()
        {
            ++text_confirmations;
        });

        interface_bridge.requestCreate(QStringLiteral("slides"));
        bool passed = check(slides.active() && slides.editable() && !text.active(),
            "new presentation route activates the editor");
        slides.applyEdit(QStringLiteral("addText"), {});
        interface_bridge.requestCreate(QStringLiteral("markdown"));
        passed = check(slide_confirmations == 1 && slides.active() && !text.active() && slides.locked(),
                     "new Markdown waits for unsaved presentation confirmation") &&
            passed;
        slides.resolveUnsaved(QStringLiteral("cancel"));
        passed = check(slides.modified() && slides.active() && !text.active() && !slides.locked(),
                     "cancel leaves the presentation intact") &&
            passed;
        interface_bridge.requestCreate(QStringLiteral("markdown"));
        slides.resolveUnsaved(QStringLiteral("discard"));
        passed = check(!slides.active() && text.active() && text.markdown() && !text.locked(),
                     "confirmed presentation-to-Markdown handoff completes synchronously") &&
            passed;

        text.updateText(QStringLiteral("# retained draft"));
        interface_bridge.requestCreate(QStringLiteral("slides"));
        passed = check(text_confirmations == 1 && text.active() && !slides.active(),
                     "new presentation waits for unsaved text confirmation") &&
            passed;
        text.resolveUnsaved(QStringLiteral("save"));
        text.cancelSaveDialog();
        passed = check(text.active() && text.modified() && !text.locked() && !slides.active(),
                     "canceling save also cancels the mode change") &&
            passed;
        interface_bridge.requestCreate(QStringLiteral("slides"));
        text.resolveUnsaved(QStringLiteral("discard"));
        passed = check(slides.active() && slides.editable() && !text.active() && !text.locked(),
                     "confirmed text-to-presentation handoff releases the text document") &&
            passed;

        slides.applyEdit(QStringLiteral("addText"), {});
        const auto original = slides.document().value<mirrorfly::RenderPresentationPtr>();
        interface_bridge.inspectFile(directory.filePath(QStringLiteral("missing.txt")));
        slides.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for_routes(text, slides) && passed;
        passed = check(slides.active() && slides.modified() && !text.active() &&
                         slides.document().value<mirrorfly::RenderPresentationPtr>() == original &&
                         !text.message().isEmpty(),
                     "failed text load preserves the modified presentation after discard choice") &&
            passed;

        const QString text_path = directory.filePath(QStringLiteral("notes.txt"));
        QFile text_file(text_path);
        if (!text_file.open(QIODevice::WriteOnly) || text_file.write("reference notes") != 15)
        {
            return false;
        }
        text_file.close();
        interface_bridge.inspectFile(text_path);
        slides.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for_routes(text, slides) && passed;
        passed =
            check(text.active() && !slides.active() && text.content() == QStringLiteral("reference notes"),
                "successful text load releases the previous presentation") &&
            passed;

        text.updateText(QStringLiteral("unsaved reference notes"));
        interface_bridge.inspectFile(directory.filePath(QStringLiteral("missing.pptx")));
        text.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for_routes(text, slides) && passed;
        passed = check(text.active() && text.modified() && !slides.active() &&
                         text.content() == QStringLiteral("unsaved reference notes"),
                     "failed presentation load preserves the modified text document") &&
            passed;
        const QString fixture = QDir(QString::fromUtf8(MIRRORFLY_TEST_FIXTURE_DIRECTORY))
                                    .filePath(QStringLiteral("presentation-deflate.pptx"));
        interface_bridge.inspectFile(fixture);
        text.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for_routes(text, slides) && passed;
        passed = check(slides.active() && !text.active() && !text.locked() && !slides.locked(),
                     "successful presentation load releases the previous text document") &&
            passed;
        interface_bridge.requestCreate(QStringLiteral("sheets"));
        passed = check(sheets.active() && !slides.active() && !text.active(),
                     "presentation to new workbook completes synchronously") &&
            passed;
        sheets.setCellValue(0, 0, QStringLiteral("workbook draft"), QStringLiteral("text"));
        interface_bridge.requestCreate(QStringLiteral("writer"));
        passed =
            check(sheets.locked() && !text.active(), "workbook handoff waits for confirmation") && passed;
        sheets.resolveUnsaved(QStringLiteral("cancel"));
        passed =
            check(sheets.active() && sheets.modified() && !sheets.locked(), "cancel preserves workbook") &&
            passed;
        interface_bridge.requestCreate(QStringLiteral("writer"));
        sheets.resolveUnsaved(QStringLiteral("discard"));
        passed = check(text.active() && !sheets.active() && !slides.active(),
                     "workbook to new text handoff completes") &&
            passed;
        text.updateText(QStringLiteral("text before workbook"));
        interface_bridge.inspectFile(directory.filePath(QStringLiteral("missing.xlsx")));
        text.resolveUnsaved(QStringLiteral("discard"));
        slides.openCompleted(true);
        sheets.documentActivated();
        passed = check(text.active() && text.busy(),
                     "unrelated and wrong-kind completions do not release the source") &&
            passed;
        passed = wait_for_routes(text, slides, &sheets) && passed;
        passed = check(text.active() && text.modified() && !text.locked() && !sheets.active(),
                     "failed spreadsheet load retains source text after discard") &&
            passed;
        interface_bridge.requestCreate(QStringLiteral("sheets"));
        text.resolveUnsaved(QStringLiteral("discard"));
        passed = check(sheets.active() && !text.active(), "text to new workbook handoff completes") && passed;
        sheets.setCellValue(0, 0, QStringLiteral("saved workbook"), QStringLiteral("text"));
        const auto sheet_path = directory.filePath(QStringLiteral("routes.xlsx"));
        sheets.saveAs();
        sheets.selectSaveFile(QUrl::fromLocalFile(sheet_path));
        passed = wait_for_routes(text, slides, &sheets) && passed;
        interface_bridge.requestCreate(QStringLiteral("slides"));
        passed =
            check(slides.active() && !sheets.active(), "saved workbook to presentation handoff completes") &&
            passed;
        slides.applyEdit(QStringLiteral("addText"), {});
        interface_bridge.inspectFile(sheet_path);
        slides.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for_routes(text, slides, &sheets) && passed;
        passed = check(sheets.active() && !slides.active() && !text.active() &&
                         sheets.cellInfo().value("inputText") == "saved workbook",
                     "successful workbook load releases source presentation") &&
            passed;
        sheets.setCellValue(0, 0, QStringLiteral("retained workbook"), QStringLiteral("text"));
        interface_bridge.inspectFile(directory.filePath(QStringLiteral("missing.txt")));
        sheets.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for_routes(text, slides, &sheets) && passed;
        passed = check(sheets.active() && sheets.modified() && !sheets.locked() && !text.active(),
                     "failed text replacement preserves workbook") &&
            passed;
        interface_bridge.inspectFile(directory.filePath(QStringLiteral("missing.pptx")));
        sheets.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_for_routes(text, slides, &sheets) && passed;
        passed = check(sheets.active() && sheets.modified() && !sheets.locked() && !slides.active(),
                     "failed presentation replacement preserves workbook") &&
            passed;
        interface_bridge.requestCreate(QStringLiteral("word"));
        passed =
            check(sheets.locked() && !word.active(), "Word creation respects the existing workbook draft") &&
            passed;
        sheets.resolveUnsaved(QStringLiteral("cancel"));
        passed = check(sheets.active() && sheets.modified() && !word.active(),
                     "cancel Word handoff keeps the workbook") &&
            passed;
        interface_bridge.requestCreate(QStringLiteral("word"));
        sheets.resolveUnsaved(QStringLiteral("discard"));
        passed = check(word.active() && !sheets.active() && !text.active() && !slides.active(),
                     "accepted Word creation releases the previous endpoint") &&
            passed;
        interface_bridge.requestCreate(QStringLiteral("markdown"));
        passed = check(text.active() && text.markdown() && !word.active(),
                     "Word routes back to Markdown through the same handoff contract") &&
            passed;
        return passed;
    }
}

int run_document_routing_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mirrorfly Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Document Routing"));
    const bool passed = test_document_routes();
    QThreadPool::globalInstance()->waitForDone();
    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_document_routing_tests(argc, argv);
}
