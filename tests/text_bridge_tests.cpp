#include "text_bridge.hpp"
#include <mirrorfly/markdown.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
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

    bool wait_until_idle(mirrorfly::TextEditorBridge& editor)
    {
        if (!editor.busy())
        {
            return true;
        }

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&editor, &mirrorfly::TextEditorBridge::stateChanged, &loop, [&editor, &loop]()
        {
            if (!editor.busy())
            {
                QTimer::singleShot(0, &loop, [&editor, &loop]()
                {
                    if (!editor.busy())
                    {
                        loop.quit();
                    }
                });
            }
        });
        timeout.start(5000);
        loop.exec();
        return check(!editor.busy(), "asynchronous file operation completes within five seconds");
    }

    QString fixture_path(const QString& directory, const char* name)
    {
        return QDir(directory).filePath(QString::fromLatin1(name));
    }

    bool write_fixture(const QString& path, const QByteArray& bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

    QByteArray read_fixture(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }

    bool test_find_and_guard()
    {
        mirrorfly::TextEditorBridge editor;
        bool passed = check(!editor.readContent({{"view", "find"}, {"text", "a"}}).value("ok").toBool(),
            "find requires an active document");
        editor.requestNewMarkdown();
        const auto source = QStringLiteral("🦋 中文 [.*] 中文 Abc abc");
        editor.replaceContent(source);
        const int revision = editor.revision();
        auto page = editor.readContent({{"view", "find"}, {"text", QStringLiteral("中文")}, {"limit", 1}});
        const auto first = page.value("items").toList().value(0).toMap();
        passed = check(page.value("ok").toBool() && first.value("start") == 3 && first.value("end") == 5 &&
                         page.value("unit") == "UTF-16" && page.value("nextOffset") == 5,
                     "find returns exact source UTF-16 positions after supplementary characters") &&
            passed;
        page = editor.readContent({{"view", "find"}, {"text", QStringLiteral("中文")}, {"offset", 5}});
        passed = check(page.value("items").toList().size() == 1 && page.value("nextOffset") == -1 &&
                         page.value("items").toList().first().toMap().value("start") ==
                             source.lastIndexOf(QStringLiteral("中文")),
                     "duplicate literal matches paginate by source offset without omission") &&
            passed;
        for (const auto& literal : {QStringLiteral("[.*]"), QStringLiteral("Abc"), QStringLiteral("🦋")})
        {
            const auto found = editor.readContent({{"view", "find"}, {"text", literal}});
            passed = check(found.value("items").toList().size() == 1 &&
                             found.value("items").toList().first().toMap().value("start") ==
                                 source.indexOf(literal),
                         "find is literal and case sensitive, including Unicode") &&
                passed;
        }
        for (const QVariantMap& bad : {QVariantMap{{"text", ""}}, QVariantMap{{"text", 42}},
                 QVariantMap{{"text", "x"}, {"offset", 1}}, QVariantMap{{"text", "x"}, {"offset", -1}},
                 QVariantMap{{"text", "x"}, {"limit", 0}}, QVariantMap{{"text", "x"}, {"id", "a"}},
                 QVariantMap{{"text", "x"}, {"unknown", true}}, QVariantMap{{"text", QString(1025, 'x')}}})
        {
            auto query = bad;
            query.insert("view", "find");
            passed = check(!editor.readContent(query).value("ok").toBool(), "invalid find queries reject") &&
                passed;
        }
        passed = check(editor.content() == source && editor.revision() == revision,
                     "reads cannot mutate source or revision") &&
            passed;
        passed = check(!editor.formatMarkdown(3, 5, "bold", {{"expectedText", "wrong"}}) &&
                         !editor.formatMarkdown(3, 5, "bold", {{"expectedText", true}}) &&
                         editor.content() == source && editor.revision() == revision,
                     "mismatched or mistyped source guard rejects without mutation") &&
            passed;
        passed = check(editor.formatMarkdown(3, 5, "bold", {{"expectedText", QStringLiteral("中文")}}) &&
                         editor.content().startsWith(QStringLiteral("🦋 **中文**")),
                     "found source range feeds the public guarded Markdown edit") &&
            passed;
        const auto changed = editor.content();
        passed = check(!editor.formatMarkdown(3, 5, "italic", {{"expectedText", QStringLiteral("中文")}}) &&
                         editor.content() == changed,
                     "old match cannot silently edit shifted source") &&
            passed;
        const QString large = QStringLiteral("🦋 中文 ").repeated(500);
        editor.replaceContent(large);
        int offset = 0;
        int matches = 0;
        do
        {
            page = editor.readContent(
                {{"view", "find"}, {"text", QStringLiteral("中文")}, {"offset", offset}, {"limit", 2000}});
            const auto items = page.value("items").toList();
            passed =
                check(page.value("ok").toBool() && !items.isEmpty(), "bounded find page makes progress") &&
                passed;
            if (!page.value("ok").toBool() || items.isEmpty())
                break;
            matches += static_cast<int>(items.size());
            const int next = page.value("nextOffset").toInt();
            if (next >= 0 && next <= offset)
                return check(false, "find pagination advances");
            offset = next;
        } while (offset >= 0);
        passed = check(matches == 500, "bounded find pages visit every match once") && passed;
        editor.replaceContent("> > quote\n");
        const int unchanged_revision = editor.revision();
        passed = check(editor.formatMarkdown(4, 9, "quoteSet", {{"quoteLevel", 2}}) &&
                         editor.revision() == unchanged_revision && editor.content() == "> > quote\n",
                     "already satisfied absolute style does not publish a new content revision") &&
            passed;
        return passed;
    }

    bool test_break_whitespace(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        editor.requestNewMarkdown();
        QJsonArray corpus;
        int index = 0;
        for (const auto& spaces : {QString(" "), QString("   "), QString("\t")})
            for (const auto& prefix : {QString{}, QString("> "), QString("- "), QString("- > ")})
            {
                const auto source = prefix + QStringLiteral("🦋左") + spaces + QStringLiteral("右\n");
                if (!editor.replaceContent(source))
                    return check(false, "prepare whitespace hard-break source");
                const int position = source.indexOf(QStringLiteral("右"));
                if (!editor.formatMarkdown(
                        position, position, "hardBreak", {{"expectedText", QStringLiteral("右")}}))
                    return check(false, "guarded source hard break preserves adjacent whitespace");
                const auto saved = editor.content();
                const auto continuation = prefix == "- " ? "  " : prefix == "- > " ? "  > " : prefix;
                const auto expected = source.left(position) + "\\\n" + continuation + QStringLiteral("右\n");
                if (!check(saved == expected, "hard break preserves visible whitespace and parent prefix"))
                    return false;
                const auto path =
                    QUrl::fromLocalFile(QDir(directory).filePath(QString("space-break-%1.md").arg(index++)));
                if (!editor.saveTo(path) || !wait_until_idle(editor))
                    return false;
                editor.requestHome();
                editor.requestOpen(path);
                if (!wait_until_idle(editor) ||
                    !check(editor.content() == saved, "space break saves and reopens exactly"))
                    return false;
                corpus.append(QJsonObject{{"initial", source}, {"source", saved}, {"expected", expected}});
            }
        const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
        if (!output.isEmpty())
        {
            QFile file(QDir(output).filePath("source-space-breaks.json"));
            const auto data = QJsonDocument(corpus).toJson(QJsonDocument::Compact);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        }
        return true;
    }

    bool test_cancel_and_discard()
    {
        mirrorfly::TextEditorBridge editor;
        int confirmations = 0;
        QObject::connect(&editor, &mirrorfly::TextEditorBridge::confirmUnsavedRequested, &editor,
            [&confirmations]()
        {
            ++confirmations;
        });
        editor.requestNew();
        const int initial_revision = editor.revision();
        bool passed = check(editor.active() && !editor.modified() && !editor.locked() &&
                editor.content().isEmpty() && editor.documentPath().isEmpty(),
            "a new document starts active and clean");
        editor.updateText(QStringLiteral("draft"));
        editor.requestNew();
        passed = check(confirmations == 1 && editor.modified() && editor.locked(),
                     "switching a dirty document requires a decision") &&
            passed;
        editor.updateText(QStringLiteral("must be ignored"));
        editor.requestHome();
        editor.resolveUnsaved(QStringLiteral("cancel"));
        passed = check(editor.active() && editor.modified() && !editor.locked() &&
                         editor.content() == QStringLiteral("draft") && editor.revision() == initial_revision,
                     "cancelling a switch preserves the original document and ignores locked edits") &&
            passed;
        editor.updateText(QString{});
        passed = check(!editor.modified(), "restoring the saved content clears the dirty state") && passed;
        editor.updateText(QStringLiteral("second draft"));
        editor.requestNew();
        editor.resolveUnsaved(QStringLiteral("discard"));
        passed = check(editor.active() && !editor.modified() && editor.content().isEmpty() &&
                         editor.revision() == initial_revision + 1,
                     "discarding for a new document clears the old text") &&
            passed;
        editor.updateText(QStringLiteral("third draft"));
        editor.requestHome();
        editor.resolveUnsaved(QStringLiteral("discard"));
        passed =
            check(!editor.active() && !editor.modified() && !editor.locked() && editor.content().isEmpty(),
                "discarding for home deactivates and clears the document") &&
            passed;
        return passed;
    }

    bool test_untitled_save_before_home(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        int save_dialogs = 0;
        int recorded_files = 0;
        QObject::connect(&editor, &mirrorfly::TextEditorBridge::saveDialogRequested, &editor,
            [&save_dialogs]()
        {
            ++save_dialogs;
        });
        QObject::connect(&editor, &mirrorfly::TextEditorBridge::fileRecorded, &editor,
            [&recorded_files](const QString&)
        {
            ++recorded_files;
        });
        editor.requestNew();
        editor.updateText(QString::fromUtf8(u8"草稿 🦋\n"));
        editor.requestHome();
        editor.resolveUnsaved(QStringLiteral("save"));
        bool passed = check(save_dialogs == 1 && editor.locked() && editor.modified() && editor.active(),
            "saving an untitled document during navigation requests a destination");
        editor.cancelSaveDialog();
        passed = check(!editor.locked() && editor.modified() && editor.active(),
                     "cancelling the save dialog cancels navigation and retains dirty content") &&
            passed;

        const QString first_path = fixture_path(directory, "cancelled-home.txt");
        editor.save();
        editor.selectSaveFile(QUrl::fromLocalFile(first_path));
        editor.updateText(QStringLiteral("must not enter a busy save"));
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.active() && !editor.modified() && editor.documentPath() == first_path &&
                         read_fixture(first_path) == QString::fromUtf8(u8"草稿 🦋\r\n").toUtf8(),
                     "a later manual save does not revive the cancelled home action") &&
            passed;

        editor.requestNew();
        editor.updateText(QStringLiteral("save before home\n"));
        editor.requestHome();
        editor.resolveUnsaved(QStringLiteral("save"));
        const QString second_path = fixture_path(directory, "save-before-home.text");
        editor.selectSaveFile(QUrl::fromLocalFile(second_path));
        passed = wait_until_idle(editor) && passed;
        passed =
            check(!editor.active() && !editor.modified() && !editor.locked() &&
                    read_fixture(second_path) == "save before home\r\n" && recorded_files == 2,
                "saving an untitled document completes the pending home action only after disk success") &&
            passed;
        return passed;
    }

    bool test_quit_decisions(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        int close_permissions = 0;
        QObject::connect(&editor, &mirrorfly::TextEditorBridge::windowCloseAllowed, &editor,
            [&close_permissions]()
        {
            ++close_permissions;
        });
        bool passed = check(editor.requestWindowClose(), "a clean inactive editor can close immediately");
        editor.requestNew();
        editor.updateText(QStringLiteral("save before quit"));
        passed = check(!editor.requestWindowClose() && editor.locked(),
                     "closing a dirty editor is deferred for a decision") &&
            passed;
        editor.resolveUnsaved(QStringLiteral("cancel"));
        passed = check(close_permissions == 0 && editor.active() && editor.modified() && !editor.locked(),
                     "cancelling close preserves the draft and emits no close permission") &&
            passed;
        editor.requestWindowClose();
        editor.resolveUnsaved(QStringLiteral("save"));
        const QString saved_path = fixture_path(directory, "save-before-quit.txt");
        editor.selectSaveFile(QUrl::fromLocalFile(saved_path));
        passed = wait_until_idle(editor) && passed;
        passed = check(close_permissions == 1 && !editor.modified() && editor.requestWindowClose() &&
                         read_fixture(saved_path) == "save before quit",
                     "successful untitled save grants close exactly once") &&
            passed;
        editor.updateText(QStringLiteral("discard before quit"));
        editor.requestWindowClose();
        editor.resolveUnsaved(QStringLiteral("discard"));
        passed = check(close_permissions == 2 && read_fixture(saved_path) == "save before quit",
                     "discard grants close without writing the modified buffer") &&
            passed;
        return passed;
    }

    bool test_failed_save_keeps_draft(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        editor.requestNew();
        editor.updateText(QStringLiteral("keep this draft"));
        editor.requestHome();
        editor.resolveUnsaved(QStringLiteral("save"));
        editor.selectSaveFile(QUrl::fromLocalFile(fixture_path(directory, "missing/failure.txt")));
        bool passed = wait_until_idle(editor);
        passed = check(editor.active() && editor.modified() && !editor.locked() &&
                         editor.content() == QStringLiteral("keep this draft") &&
                         editor.documentPath().isEmpty() && !editor.message().isEmpty(),
                     "a failed save keeps dirty content and cancels pending navigation") &&
            passed;
        editor.save();
        const QString recovered_path = fixture_path(directory, "recovered.txt");
        editor.selectSaveFile(QUrl::fromLocalFile(recovered_path));
        passed = wait_until_idle(editor) && passed;
        passed =
            check(editor.active() && !editor.modified() && read_fixture(recovered_path) == "keep this draft",
                "recovery save succeeds without resuming the failed home action") &&
            passed;

        const QString invalid_text(1, QChar(0xD800));
        editor.updateText(invalid_text);
        editor.save();
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.modified() && editor.content() == invalid_text && !editor.message().isEmpty() &&
                         read_fixture(recovered_path) == "keep this draft",
                     "an isolated UTF-16 surrogate is rejected before lossy QString-to-UTF-8 conversion") &&
            passed;
        return passed;
    }

    bool test_disk_conflict_and_save_as(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        const QString original_path = fixture_path(directory, "external-edit.txt");
        bool passed = check(write_fixture(original_path, "original\n"), "create external edit fixture");
        editor.requestOpen(QUrl::fromLocalFile(original_path));
        passed = wait_until_idle(editor) && passed;
        editor.updateText(QStringLiteral("local changes\n"));
        passed =
            check(write_fixture(original_path, "external changes\n"), "simulate another editor saving") &&
            passed;
        editor.save();
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.active() && editor.modified() &&
                         editor.content() == QStringLiteral("local changes\n") &&
                         read_fixture(original_path) == "external changes\n" && !editor.message().isEmpty(),
                     "revision conflicts keep both external disk content and local unsaved edits") &&
            passed;

        editor.saveAs();
        editor.selectSaveFile(QUrl::fromLocalFile(original_path));
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.modified() && read_fixture(original_path) == "external changes\n",
                     "save-as to the current path still respects its original revision") &&
            passed;

        const QString copy_path = fixture_path(directory, "external-edit-copy.text");
        editor.saveAs();
        editor.selectSaveFile(QUrl::fromLocalFile(copy_path));
        passed = wait_until_idle(editor) && passed;
        passed = check(!editor.modified() && editor.documentPath() == copy_path &&
                         read_fixture(copy_path) == "local changes\n" &&
                         read_fixture(original_path) == "external changes\n",
                     "save-as to a new path preserves the conflicting original and saves local changes") &&
            passed;
        return passed;
    }

    bool test_pending_open(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        const QString next_path = fixture_path(directory, "next.txt");
        bool passed = check(write_fixture(next_path, "next file\n"), "create pending-open fixture");
        editor.requestNew();
        editor.updateText(QStringLiteral("unsaved old text"));
        editor.requestOpen(QUrl::fromLocalFile(next_path));
        editor.resolveUnsaved(QStringLiteral("cancel"));
        passed = check(editor.documentPath().isEmpty() && editor.modified() &&
                         editor.content() == QStringLiteral("unsaved old text"),
                     "cancelling open leaves the original unsaved buffer intact") &&
            passed;

        editor.requestOpen(QUrl::fromLocalFile(fixture_path(directory, "does-not-exist.txt")));
        editor.resolveUnsaved(QStringLiteral("discard"));
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.active() && editor.modified() &&
                         editor.content() == QStringLiteral("unsaved old text"),
                     "a failed replacement load does not destroy the previous dirty document") &&
            passed;

        editor.requestOpen(QUrl::fromLocalFile(next_path));
        editor.resolveUnsaved(QStringLiteral("save"));
        const QString previous_path = fixture_path(directory, "previous.txt");
        editor.selectSaveFile(QUrl::fromLocalFile(previous_path));
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.active() && !editor.modified() && editor.documentPath() == next_path &&
                         editor.content() == QStringLiteral("next file\n") &&
                         read_fixture(previous_path) == "unsaved old text",
                     "save-then-open saves the old buffer before activating the new file") &&
            passed;
        return passed;
    }

    bool test_markdown_format_interface(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        editor.requestNewMarkdown();
        const auto original = QString::fromUtf8(u8"前🦋过期 后\n");
        bool passed = check(editor.replaceContent(original) && !editor.formatMarkdown(2, 5, "strike", {}) &&
                editor.content() == original,
            "public source formatting rejects split UTF-16 without modifying the draft");
        passed = check(editor.formatMarkdown(1, 5, "strike", {}) &&
                         editor.content() == QString::fromUtf8(u8"前~~🦋过期~~ 后\n"),
                     "public Markdown formatting uses source offsets and preserves surrounding text") &&
            passed;
        editor.replaceContent(QStringLiteral("before a`b after\n"));
        passed = check(editor.formatMarkdown(7, 10, "inlineCode", {}) &&
                         editor.content() == QStringLiteral("before ``a`b`` after\n"),
                     "public inline-code formatting protects literal backticks") &&
            passed;
        const QString table_source =
            QString::fromUtf8(u8"前🦋\n\n| 名称 | 数量 |\n| --- | --- |\n| 茶 | 2 |\n");
        editor.replaceContent(table_source);
        const int cell = table_source.indexOf(QString::fromUtf8(u8"茶"));
        passed =
            check(editor.formatMarkdown(cell, cell, "tableAlign", {{"column", 1}, {"alignment", "right"}}) &&
                    editor.content().contains("| --- | ---: |") &&
                    !editor.formatMarkdown(cell, cell, "tableAlign", {{"alignment", "center"}}),
                "public source column edits require an explicit index and preserve Unicode offsets") &&
            passed;
        const auto formatted = editor.content();
        for (const QVariant& column : {QVariant(0.5), QVariant(true), QVariant("0")})
            passed = check(!editor.formatMarkdown(
                               cell, cell, "tableAlign", {{"column", column}, {"alignment", "center"}}) &&
                             editor.content() == formatted,
                         "non-integer column values reject without source changes") &&
                passed;
        passed = check(!editor.formatMarkdown(0, 1, "unknown", {}) && editor.content() == formatted,
                     "undeclared format action is rejected") &&
            passed;
        const auto destination = QUrl::fromLocalFile(fixture_path(directory, "strike-interface.md"));
        passed =
            check(editor.saveTo(destination) && wait_until_idle(editor), "formatted draft saves") && passed;
        editor.requestHome();
        editor.requestOpen(destination);
        passed = check(wait_until_idle(editor) && editor.content() == formatted,
                     "formatted source survives actual save and reopen") &&
            passed;
        mirrorfly::TextEditorBridge plain;
        plain.requestNew();
        passed = check(plain.replaceContent("plain") && !plain.formatMarkdown(0, 5, "strike", {}),
                     "plain text rejects Markdown formatting") &&
            passed;
        return passed;
    }

    bool test_markdown_list_interface(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        editor.requestNewMarkdown();
        const auto source = QString::fromUtf8(u8"- 父🦋\r\n- [ ] 待办\r\n  - 子项\r\n- 末项\r\n");
        bool passed = check(editor.replaceContent(source), "prepare a public nested-list draft");
        const int task = editor.content().indexOf(QString::fromUtf8(u8"待办"));
        passed = check(editor.formatMarkdown(task, task, "listIndent", {}) &&
                         editor.content().contains(QString::fromUtf8(u8"  - [ ] 待办")) &&
                         editor.content().contains(QString::fromUtf8(u8"    - 子项")),
                     "public source list indentation preserves Unicode offsets and carries descendants") &&
            passed;
        const int nested = editor.content().indexOf(QString::fromUtf8(u8"待办"));
        const auto before = editor.content();
        for (const QVariant& value : {QVariant(1), QVariant("true"), QVariant{}})
            passed = check(!editor.formatMarkdown(nested, nested, "taskSet", {{"checked", value}}) &&
                             editor.content() == before,
                         "public explicit task state rejects non-boolean values") &&
                passed;
        passed = check(editor.formatMarkdown(nested, nested, "taskSet", {{"checked", true}}) &&
                         editor.content().contains(QString::fromUtf8(u8"  - [x] 待办")),
                     "public task state uses a typed explicit boolean") &&
            passed;
        const auto expected = editor.content();
        passed = check(editor.formatMarkdown(nested, nested, "taskSet", {{"checked", true}}) &&
                         editor.content() == expected,
                     "repeating a completion request does not toggle the task back") &&
            passed;
        const auto destination = QUrl::fromLocalFile(fixture_path(directory, "list-interface.md"));
        passed = check(editor.saveTo(destination) && wait_until_idle(editor),
                     "public list draft saves through the normal asynchronous file interface") &&
            passed;
        editor.requestHome();
        editor.requestOpen(destination);
        passed = check(wait_until_idle(editor) && editor.content() == expected,
                     "public list hierarchy and task state survive actual file save and reopen") &&
            passed;
        return passed;
    }

    bool test_markdown_link_interface(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        editor.requestNewMarkdown();
        const QString source = QString::fromUtf8(u8"前 你好🦋 后\n");
        bool passed = check(editor.replaceContent(source), "prepare a public Unicode link draft");
        const int start = source.indexOf(QString::fromUtf8(u8"你好"));
        const int end = source.indexOf(QString::fromUtf8(u8" 后"));
        const QVariantMap options{{"url", "../a (b)/x?one=1&two=2"}, {"title", "a \"title\""}};
        passed = check(editor.formatMarkdown(start, end, "link", options),
                     "public link creation uses source UTF-16 ranges and named typed options") &&
            passed;
        const auto linked = editor.content();
        for (const auto& invalid :
            {QVariantMap{{"url", 1}}, QVariantMap{{"url", ""}}, QVariantMap{{"url", "x"}, {"title", false}}})
            passed = check(!editor.formatMarkdown(start + 1, start + 1, "link", invalid) &&
                             editor.content() == linked,
                         "public link validation rejects untyped parameters without editing") &&
                passed;
        const int butterfly = linked.indexOf(QString::fromUtf8(u8"🦋"));
        passed = check(!editor.formatMarkdown(butterfly + 1, butterfly + 1, "unlink", {}) &&
                         editor.content() == linked,
                     "public unlink cannot split a UTF-16 surrogate pair") &&
            passed;
        const auto destination = QUrl::fromLocalFile(fixture_path(directory, "link-interface.md"));
        passed =
            check(editor.saveTo(destination) && wait_until_idle(editor), "save public link parameters") &&
            passed;
        editor.requestHome();
        editor.requestOpen(destination);
        passed =
            check(wait_until_idle(editor) && editor.content() == linked &&
                    editor.formatMarkdown(start + 1, start + 1, "unlink", {}) && editor.content() == source,
                "link source saves and reopens exactly, and unlink restores the original label") &&
            passed;
        editor.requestNewMarkdown();
        editor.resolveUnsaved(QStringLiteral("discard"));
        const QString reference =
            QString::fromUtf8(u8"🦋前 [**甲**][ref] [ref]\n\n[ref]: ../original \"提示\"\n");
        const int label = reference.indexOf(QString::fromUtf8(u8"甲"));
        passed = check(editor.replaceContent(reference) &&
                         editor.formatMarkdown(label, label, "link", {{"url", "../new"}}) &&
                         editor.content().contains(QString::fromUtf8(u8"[**甲**](../new) [ref]")) &&
                         editor.content().endsWith(QString::fromUtf8(u8"[ref]: ../original \"提示\"\n")),
                     "public UTF-16 reference updates preserve shared definitions, other uses and markup") &&
            passed;
        const auto resolved = editor.content();
        const auto resolved_destination =
            QUrl::fromLocalFile(fixture_path(directory, "reference-interface.md"));
        passed = check(editor.saveTo(resolved_destination) && wait_until_idle(editor),
                     "save public reference transaction") &&
            passed;
        editor.requestHome();
        editor.requestOpen(resolved_destination);
        passed = check(wait_until_idle(editor) && editor.content() == resolved,
                     "reference transaction survives actual public storage save-reopen") &&
            passed;
        const QString automatic = QString::fromUtf8(u8"🦋 <a@example.com> tail\n");
        const int email = automatic.indexOf("a@example.com");
        passed =
            check(editor.replaceContent(automatic) && editor.formatMarkdown(email, email, "unlink", {}) &&
                    mirrorfly::markdown_links(editor.content().toUtf8().toStdString()).empty() &&
                    editor.content().startsWith(QString::fromUtf8(u8"🦋 a\\@example\\.com")),
                "public automatic-link removal preserves literal text without creating another anchor") &&
            passed;
        const QString adjacent = QString::fromUtf8(u8"🦋 [甲](../same)[乙](../same)\n");
        const int adjacent_label = adjacent.indexOf(QString::fromUtf8(u8"甲"));
        passed = check(editor.replaceContent(adjacent) &&
                         editor.formatMarkdown(adjacent_label, adjacent_label, "link", {{"url", "../one"}}) &&
                         editor.content() == QString::fromUtf8(u8"🦋 [甲](../one)[乙](../same)\n"),
                     "source UTF-16 editing keeps adjacent same-target occurrences independent") &&
            passed;
        const QString break_source = QString::fromUtf8(u8"🦋前后\n");
        passed = check(editor.replaceContent(break_source) && editor.formatMarkdown(3, 3, "hardBreak", {}) &&
                         editor.content() == QString::fromUtf8(u8"🦋前  \n后\n"),
                     "public source hardBreak uses UTF-16 without splitting the preceding emoji") &&
            passed;
        const auto break_saved = editor.content();
        const auto break_destination =
            QUrl::fromLocalFile(fixture_path(directory, "hard-break-interface.md"));
        passed = check(editor.saveTo(break_destination) && wait_until_idle(editor),
                     "save public source hardBreak through real storage") &&
            passed;
        editor.requestHome();
        editor.requestOpen(break_destination);
        passed = check(wait_until_idle(editor) && editor.content() == break_saved,
                     "source hardBreak survives actual public storage save-reopen") &&
            passed;
        const auto rule_original = editor.content();
        passed = check(editor.formatMarkdown(3, 3, "thematicBreak", {}) &&
                         editor.content().startsWith(rule_original) &&
                         mirrorfly::markdown_thematic_break_count(editor.content().toStdString()) == 1,
                     "public source separator preserves a Unicode multi-line paragraph") &&
            passed;
        const auto rule_saved = editor.content();
        const auto rule_destination = QUrl::fromLocalFile(fixture_path(directory, "rule-interface.md"));
        passed = check(editor.saveTo(rule_destination) && wait_until_idle(editor),
                     "save source separator through actual file storage") &&
            passed;
        editor.requestHome();
        editor.requestOpen(rule_destination);
        passed = check(wait_until_idle(editor) && editor.content() == rule_saved,
                     "source separator survives real file save-reopen") &&
            passed;
        passed = check(editor.replaceContent(QString::fromUtf8(u8"🦋a**b**c\n")) &&
                         editor.formatMarkdown(5, 6, "italic", {}),
                     "public source crossing styles use UTF-16 positions after a supplementary character") &&
            passed;
        const auto styled_source = editor.content();
        const auto styled_destination =
            QUrl::fromLocalFile(fixture_path(directory, "body-style-interface.md"));
        passed = check(editor.saveTo(styled_destination) && wait_until_idle(editor),
                     "save source body styles through real public storage") &&
            passed;
        editor.requestHome();
        editor.requestOpen(styled_destination);
        passed = check(wait_until_idle(editor) && editor.content() == styled_source,
                     "source body crossing styles survive real file save-reopen") &&
            passed;
        const auto styled_paragraphs =
            mirrorfly::markdown_paragraphs(editor.content().toUtf8().toStdString());
        bool mixed = false;
        if (styled_paragraphs.size() == 1)
            for (const auto& run : styled_paragraphs[0].runs)
                mixed = mixed || (run.style.text == "b" && run.style.bold && run.style.italic);
        passed = check(mixed, "public paragraph metadata exposes saved crossing styles without Qt types") &&
            passed;
        const QString nested_source =
            QString::fromUtf8(u8"100) 🦋parent\n\n     > quote\n     >\n     > ```cpp\n     > x\n"
                              "     > ```\n     >\n     > | A |\n     > | :---: |\n     > | a |\n"
                              "     >\n     > ---\n\n     after\n\n101) tail\n");
        const int quote_start = nested_source.indexOf("quote");
        passed = check(editor.replaceContent(nested_source) &&
                         editor.formatMarkdown(quote_start, quote_start + 5, "italic", {}),
                     "public source style edit preserves a quoted continuation after Unicode") &&
            passed;
        const auto nested_saved = editor.content();
        const auto nested_destination =
            QUrl::fromLocalFile(fixture_path(directory, "container-interface.md"));
        passed = check(editor.saveTo(nested_destination) && wait_until_idle(editor),
                     "save nested continuation through real public storage") &&
            passed;
        editor.requestHome();
        editor.requestOpen(nested_destination);
        const bool nested_reopened = wait_until_idle(editor);
        const auto nested = mirrorfly::markdown_paragraphs(editor.content().toUtf8().toStdString());
        const auto leaves = mirrorfly::markdown_blocks(editor.content().toUtf8().toStdString());
        passed = check(nested_reopened && editor.content() == nested_saved && nested.size() == 4 &&
                         leaves.size() == 3 && leaves[0].kind == "code" && leaves[0].text == "x\n" &&
                         leaves[0].language == "cpp" && leaves[1].kind == "table" &&
                         leaves[2].kind == "thematicBreak" && leaves[0].containers.size() == 2 &&
                         leaves[0].containers[0].identity == nested[0].containers[0].identity &&
                         nested[1].containers.size() == 2 && nested[1].containers[0].kind == "listItem" &&
                         nested[1].containers[1].kind == "quote" &&
                         nested[0].containers[0].identity == nested[2].containers[0].identity &&
                         nested[0].containers[0].ordinal == 100 && nested[0].containers[0].delimiter == ')',
                     "public storage save-reopen retains typed container order and shared parent identity") &&
            passed;
        editor.requestNewMarkdown();
        const QString moving_source =
            QString::fromUtf8(u8"- first🦋\n- parent\n\n  > ```cpp\n  > x\n  > ```\n  >\n"
                              "  > | A |\n  > | :---: |\n  > | a |\n  >\n  > ---\n\n- tail\n");
        const int parent_start = moving_source.indexOf("parent");
        passed = check(editor.replaceContent(moving_source) &&
                         editor.formatMarkdown(parent_start, parent_start, "listIndent", {}),
                     "public UTF-16 parent move follows a supplementary Unicode character") &&
            passed;
        const auto moved_source = editor.content();
        const auto move_destination = QUrl::fromLocalFile(fixture_path(directory, "mixed-move-interface.md"));
        passed =
            check(editor.saveTo(move_destination) && wait_until_idle(editor), "save mixed source move") &&
            passed;
        editor.requestHome();
        editor.requestOpen(move_destination);
        const bool move_reopened = wait_until_idle(editor);
        const auto moved_leaves = mirrorfly::markdown_blocks(editor.content().toUtf8().toStdString());
        passed = check(move_reopened && editor.content() == moved_source && moved_leaves.size() == 3 &&
                         moved_leaves[0].text == "x\n" && moved_leaves[0].containers.size() == 3 &&
                         moved_leaves[0].containers[0].kind == "listItem" &&
                         moved_leaves[0].containers[1].kind == "listItem" &&
                         moved_leaves[0].containers[2].kind == "quote",
                     "actual storage save-reopen retains mixed subtree after a public source move") &&
            passed;
        editor.requestNewMarkdown();
        const QString heading_source =
            QString::fromUtf8(u8"🦋前\n\n> - **bold** [link](../x)\n>   ===\n> - tail\n");
        passed =
            check(editor.replaceContent(heading_source), "prepare UTF-16 container heading source") && passed;
        for (int level = 1; level <= 6; ++level)
        {
            const int position = editor.content().indexOf("bold");
            passed = check(editor.formatMarkdown(position, position, "heading", {{"headingLevel", level}}),
                         "public source heading accepts six levels after supplementary Unicode") &&
                passed;
        }
        const auto heading_destination = QUrl::fromLocalFile(fixture_path(directory, "heading-interface.md"));
        const auto expected_heading = editor.content();
        passed =
            check(editor.saveTo(heading_destination) && wait_until_idle(editor), "save source heading") &&
            passed;
        editor.requestHome();
        editor.requestOpen(heading_destination);
        passed = check(wait_until_idle(editor) && editor.content() == expected_heading,
                     "source heading storage save-reopen retains complete UTF-16 transaction") &&
            passed;
        const int heading_position = editor.content().indexOf("bold");
        passed = check(editor.formatMarkdown(heading_position, heading_position, "paragraph", {}),
                     "public source paragraph restores body without removing containers") &&
            passed;
        const auto body_metadata = mirrorfly::markdown_paragraphs(editor.content().toUtf8().toStdString());
        passed = check(body_metadata.size() == 3 && body_metadata[1].heading_level == 0 &&
                         body_metadata[1].containers.size() == 2 && body_metadata[1].runs[0].style.bold,
                     "public paragraph metadata retains parents and explicit styles") &&
            passed;
        const auto expected_body = editor.content();
        editor.save();
        passed =
            check(wait_until_idle(editor) && !editor.modified(), "save restored source paragraph") && passed;
        editor.requestHome();
        editor.requestOpen(heading_destination);
        passed = check(wait_until_idle(editor) && editor.content() == expected_body,
                     "paragraph storage save-reopen keeps styles and source container ownership") &&
            passed;
        editor.replaceContent(QString::fromUtf8(u8"🦋前\n\n> - ##\n> - tail\n"));
        const int empty_position = editor.content().indexOf("#");
        passed =
            check(editor.formatMarkdown(empty_position, empty_position, "heading", {{"headingLevel", 6}}),
                "source UTF-16 empty heading changes level") &&
            passed;
        const auto empty_destination =
            QUrl::fromLocalFile(fixture_path(directory, "empty-heading-interface.md"));
        const auto empty_source = editor.content();
        passed =
            check(editor.saveTo(empty_destination) && wait_until_idle(editor), "save empty heading source") &&
            passed;
        editor.requestHome();
        editor.requestOpen(empty_destination);
        passed = check(wait_until_idle(editor) && editor.content() == empty_source,
                     "empty heading reopens without placeholder") &&
            passed;
        const int reset_empty = editor.content().indexOf("#");
        passed = check(editor.formatMarkdown(reset_empty, reset_empty, "paragraph", {}),
                     "reset existing empty heading") &&
            passed;
        const auto empty_body = editor.content();
        editor.save();
        passed = check(wait_until_idle(editor) && !editor.modified(), "save reset empty heading") && passed;
        editor.requestHome();
        editor.requestOpen(empty_destination);
        passed = check(wait_until_idle(editor) && editor.content() == empty_body &&
                         editor.content().contains("> - \n> - tail"),
                     "empty heading reset retains empty list owner in real storage") &&
            passed;
        const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
        if (!output.isEmpty())
        {
            QFile file(QDir(output).filePath("source-reference.md"));
            const auto bytes = resolved.toUtf8();
            passed = check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                         "export an actual public reference transaction for independent verification") &&
                passed;
        }

        editor.requestNewMarkdown();
        const QString code_source = QString::fromUtf8(u8"🦋前\n\n> - a\r\n>   b after\r\n> - tail\r\n");
        passed = check(editor.replaceContent(code_source), "prepare normalized UTF-16 code source") && passed;
        passed = check(editor.formatMarkdown(editor.content().indexOf("> - ") + 4,
                           editor.content().indexOf(" after"), "inlineCode", {}),
                     "public UTF-16 code creation preserves container prefixes") &&
            passed;
        const auto code_destination = QUrl::fromLocalFile(fixture_path(directory, "code-span-interface.md"));
        const auto expected_code = editor.content();
        passed = check(editor.saveTo(code_destination) && wait_until_idle(editor), "save multiline code") &&
            passed;
        editor.requestHome();
        editor.requestOpen(code_destination);
        passed = check(wait_until_idle(editor) && editor.content() == expected_code,
                     "multiline code source survives actual file storage") &&
            passed;
        const int code_caret = editor.content().indexOf("`a") + 1;
        passed = check(editor.formatMarkdown(code_caret, code_caret, "removeInlineCode", {}),
                     "public UTF-16 caret removes complete code span") &&
            passed;
        const auto expected_plain_code = editor.content();
        editor.save();
        passed = check(wait_until_idle(editor) && !editor.modified(), "save unformatted code text") && passed;
        editor.requestHome();
        editor.requestOpen(code_destination);
        passed = check(wait_until_idle(editor) && editor.content() == expected_plain_code &&
                         editor.content().contains("> - a b after") &&
                         mirrorfly::markdown_code_spans(editor.content().toUtf8().toStdString()).empty(),
                     "code removal storage retains literal text and parent hierarchy") &&
            passed;
        for (const auto& action :
            {QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("strike")})
        {
            editor.requestNewMarkdown();
            passed = check(editor.replaceContent(QString::fromUtf8(u8"> - 🦋L \t R\n> - tail\n")),
                         "prepare UTF-16 whitespace selection") &&
                passed;
            const int whitespace_start = editor.content().indexOf("L") + 1;
            passed = check(editor.formatMarkdown(whitespace_start, whitespace_start + 3, action, {}),
                         "public UTF-16 whitespace style action applies") &&
                passed;
            const auto whitespace_saved = editor.content();
            const auto whitespace_path =
                QUrl::fromLocalFile(QDir(directory).filePath("whitespace-" + action + ".md"));
            passed =
                check(editor.saveTo(whitespace_path) && wait_until_idle(editor), "save whitespace styles") &&
                passed;
            editor.requestHome();
            editor.requestOpen(whitespace_path);
            passed = check(wait_until_idle(editor) && editor.content() == whitespace_saved,
                         "real file preserves expanded character-reference offsets and whitespace styles") &&
                passed;
        }
        const QString image_source = QString::fromUtf8(u8"🦋前\n\n> - caption\n");
        passed = check(editor.replaceContent(image_source), "prepare source image fixture") && passed;
        const int image_position = image_source.indexOf("caption");
        const QVariantMap image_options{
            {"url", "../photo.png"}, {"alt", QString::fromUtf8(u8"照片🦋")}, {"title", "caption"}};
        passed = check(editor.formatMarkdown(image_position, image_position + 7, "image", image_options),
                     "public source image uses UTF-16 offsets after a supplementary character") &&
            passed;
        const auto image_saved = editor.content();
        passed = check(!editor.formatMarkdown(image_position, image_position, "image", {{"alt", false}}) &&
                         editor.content() == image_saved,
                     "untyped image metadata rejects without mutating source") &&
            passed;
        const auto image_destination = QUrl::fromLocalFile(fixture_path(directory, "image-interface.md"));
        passed = check(editor.saveTo(image_destination) && wait_until_idle(editor),
                     "save image source through real storage") &&
            passed;
        editor.requestHome();
        editor.requestOpen(image_destination);
        passed = check(wait_until_idle(editor) && editor.content() == image_saved &&
                         mirrorfly::markdown_images(editor.content().toUtf8().toStdString()).size() == 1,
                     "source image metadata survives real file save/reopen") &&
            passed;
        return passed;
    }

    bool test_markdown_quote_interface(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        editor.requestNewMarkdown();
        const auto source = QString::fromUtf8(u8"> 父🦋\r\n> > 子项\r\n正文\r\n");
        bool passed = check(editor.replaceContent(source), "prepare a public quote draft");
        const auto normalized = editor.content();
        const int position = source.indexOf(QString::fromUtf8(u8"父"));
        for (const QVariant& value : {QVariant{}, QVariant(true), QVariant("2"), QVariant(1.5), QVariant(9)})
            passed = check(!editor.formatMarkdown(position, position, "quoteSet", {{"quoteLevel", value}}) &&
                             editor.content() == normalized,
                         "public quote levels reject missing, untyped and out-of-range values") &&
                passed;
        passed = check(editor.formatMarkdown(position, position, "quoteSet", {{"quoteLevel", 2}}) &&
                         editor.content() == QString::fromUtf8(u8"> > 父🦋\n> > > 子项\n正文\n"),
                     "public source quotes carry descendants and preserve UTF-16 Unicode boundaries") &&
            passed;
        const auto expected = editor.content();
        const auto destination = QUrl::fromLocalFile(fixture_path(directory, "quote-interface.md"));
        passed = check(editor.saveTo(destination) && wait_until_idle(editor), "save explicit quote levels") &&
            passed;
        editor.requestHome();
        editor.requestOpen(destination);
        passed = check(wait_until_idle(editor) && editor.content() == expected,
                     "quote levels survive actual file save and reopen through the public bridge") &&
            passed;
        return passed;
    }

    bool test_markdown_document(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        editor.requestNewMarkdown();
        bool passed = check(editor.markdown() && editor.documentName() == QStringLiteral("未命名.md"),
            "new Markdown document retains its mode before choosing a path");
        const QString source = QStringLiteral("# 学习笔记\n\n```cpp\nint value = 7;\n```\n");
        editor.updateText(source);
        editor.requestNew();
        editor.resolveUnsaved(QStringLiteral("cancel"));
        passed = check(editor.markdown() && editor.content() == source && editor.modified(),
                     "cancelled document switch preserves Markdown mode and draft") &&
            passed;
        editor.save();
        const QString path = fixture_path(directory, "notes.MD");
        editor.selectSaveFile(QUrl::fromLocalFile(path));
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.markdown() && !editor.modified() && editor.content() == source,
                     "saving Markdown leaves the exact source buffer intact") &&
            passed;
        editor.requestHome();
        editor.requestOpen(QUrl::fromLocalFile(path));
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.markdown() && editor.content() == source,
                     "reopening Markdown preserves fenced code and Unicode") &&
            passed;
        editor.saveAs();
        editor.selectSaveFile(QUrl::fromLocalFile(fixture_path(directory, "notes-as-text.txt")));
        passed = wait_until_idle(editor) && passed;
        passed = check(!editor.markdown() && editor.content() == source && !editor.modified(),
                     "successful save-as changes presentation mode without rewriting source") &&
            passed;
        editor.requestNewMarkdown();
        editor.updateText(source);
        editor.requestNew();
        editor.resolveUnsaved(QStringLiteral("save"));
        editor.cancelSaveDialog();
        passed = check(editor.markdown() && editor.modified() && editor.content() == source,
                     "cancelled save-before-new retains the original Markdown draft") &&
            passed;
        return passed;
    }

    bool test_document_handoff(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        const auto destination = QUrl::fromLocalFile(fixture_path(directory, "lesson.pptx"));
        int handoffs = 0;
        QUrl received;
        QObject::connect(&editor, &mirrorfly::TextEditorBridge::handoffRequested, &editor,
            [&handoffs, &received](const QUrl& url)
        {
            ++handoffs;
            received = url;
        });
        editor.requestNewMarkdown();
        editor.updateText(QStringLiteral("# Keep my notes\n"));
        const int revision = editor.revision();
        editor.requestHandoff(destination);
        editor.resolveUnsaved(QStringLiteral("cancel"));
        bool passed = check(handoffs == 0 && editor.modified() && !editor.locked(),
            "cancelled external open never starts another document loader");
        editor.requestHandoff(destination);
        editor.resolveUnsaved(QStringLiteral("discard"));
        passed = check(handoffs == 1 && received == destination && editor.locked() && editor.active(),
                     "approved handoff locks but retains the existing buffer until success") &&
            passed;
        editor.requestNew();
        editor.updateText(QStringLiteral("ignored"));
        editor.finishHandoff(false);
        passed =
            check(editor.modified() && editor.markdown() && !editor.locked() &&
                    editor.content() == QStringLiteral("# Keep my notes\n") && editor.revision() == revision,
                "failed handoff preserves Markdown mode, dirty data and document identity") &&
            passed;
        editor.requestHandoff(destination);
        editor.resolveUnsaved(QStringLiteral("save"));
        editor.cancelSaveDialog();
        passed = check(handoffs == 1 && editor.modified() && !editor.locked(),
                     "cancelling save before handoff cancels the external open") &&
            passed;
        editor.requestHandoff(destination);
        editor.resolveUnsaved(QStringLiteral("save"));
        const auto saved_path = fixture_path(directory, "handoff-notes.md");
        editor.selectSaveFile(QUrl::fromLocalFile(saved_path));
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&editor, &mirrorfly::TextEditorBridge::handoffRequested, &loop, &QEventLoop::quit);
        timeout.start(5000);
        loop.exec();
        passed = check(handoffs == 2 && editor.locked() && !editor.modified() &&
                         read_fixture(saved_path) == "# Keep my notes\r\n",
                     "saved content reaches disk before starting external load") &&
            passed;
        editor.finishHandoff(true);
        passed =
            check(!editor.active() && !editor.modified() && !editor.locked() && editor.content().isEmpty(),
                "only successful external load releases the previous document") &&
            passed;
        editor.requestNew();
        editor.updateText(QStringLiteral("new draft"));
        editor.finishHandoff(true);
        passed = check(editor.active() && editor.modified(),
                     "stale handoff completion cannot clear a newer draft") &&
            passed;
        return passed;
    }

    bool test_editing_failure(const QString& directory)
    {
        mirrorfly::TextEditorBridge editor;
        const auto path = fixture_path(directory, "serialization.md");
        bool passed = write_fixture(path, "# Original\n");
        editor.requestOpen(QUrl::fromLocalFile(path));
        passed = wait_until_idle(editor) && passed;
        editor.setEditingError(QStringLiteral("The visual document could not be serialized."));
        passed = check(editor.modified() && !editor.locked(),
                     "unserialized visual edits are marked dirty and remain editable for recovery") &&
            passed;
        editor.save();
        passed = check(!editor.busy() && read_fixture(path) == "# Original\n",
                     "serialization failure never overwrites the original file") &&
            passed;
        editor.requestHome();
        editor.resolveUnsaved(QStringLiteral("save"));
        passed = check(editor.active() && editor.modified() && !editor.locked(),
                     "save-before-navigation cannot bypass serialization failure") &&
            passed;
        editor.setEditingError({});
        editor.updateText(QStringLiteral("# Recovered\n"));
        editor.save();
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.active() && !editor.modified() && read_fixture(path) == "# Recovered\n",
                     "recovery saves valid text without reviving cancelled navigation") &&
            passed;
        editor.requestNewMarkdown();
        editor.setEditingError(QStringLiteral("Invalid visual content"));
        int save_dialogs = 0;
        QObject::connect(&editor, &mirrorfly::TextEditorBridge::saveDialogRequested, &editor,
            [&save_dialogs]()
        {
            ++save_dialogs;
        });
        editor.save();
        editor.saveAs();
        passed = check(save_dialogs == 0 && editor.modified(),
                     "invalid untitled visual content is blocked before asking for a destination") &&
            passed;
        editor.requestHome();
        editor.resolveUnsaved(QStringLiteral("discard"));
        passed = check(!editor.active() && !editor.modified(),
                     "explicit discard may leave an unserializable document") &&
            passed;
        return passed;
    }

    bool test_worker_lifetime(const QString& directory)
    {
        const QString path = fixture_path(directory, "lifetime.txt");
        bool passed =
            check(write_fixture(path, QByteArray(512 * 1024, 'x')), "create worker lifetime fixture");
        auto* editor = new mirrorfly::TextEditorBridge;
        QPointer<mirrorfly::TextEditorBridge> guard(editor);
        editor->requestOpen(QUrl::fromLocalFile(path));
        passed = check(editor->busy() && !editor->requestWindowClose(),
                     "normal close is blocked while a file operation is running") &&
            passed;
        delete editor;
        passed = check(guard.isNull(), "the bridge can be destroyed without retaining its QObject") && passed;
        passed = check(QThreadPool::globalInstance()->waitForDone(5000),
                     "detached value-captured worker finishes without accessing a destroyed bridge") &&
            passed;
        QCoreApplication::processEvents();
        return passed;
    }

}

int run_text_editor_state_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;

    if (!check(directory.isValid(), "create an isolated bridge-test directory"))
    {
        return 1;
    }

    bool passed = test_cancel_and_discard();
    passed = test_find_and_guard() && passed;
    passed = test_break_whitespace(directory.path()) && passed;
    passed = test_untitled_save_before_home(directory.path()) && passed;
    passed = test_quit_decisions(directory.path()) && passed;
    passed = test_failed_save_keeps_draft(directory.path()) && passed;
    passed = test_disk_conflict_and_save_as(directory.path()) && passed;
    passed = test_pending_open(directory.path()) && passed;
    passed = test_worker_lifetime(directory.path()) && passed;
    passed = test_markdown_document(directory.path()) && passed;
    passed = test_markdown_format_interface(directory.path()) && passed;
    passed = test_markdown_list_interface(directory.path()) && passed;
    passed = test_markdown_quote_interface(directory.path()) && passed;
    passed = test_markdown_link_interface(directory.path()) && passed;
    passed = test_document_handoff(directory.path()) && passed;
    passed = test_editing_failure(directory.path()) && passed;

    if (passed)
    {
        std::cout << "Text bridge state tests passed.\n";
    }

    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_text_editor_state_tests(argc, argv);
}
