#include "word_distribution.hpp"
#include "word_document.hpp"
#include "word_editor_document.hpp"

#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextTable>

#include <iostream>
#include <limits>

namespace
{
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    std::string text(const mirrorfly::WordParagraph& paragraph)
    {
        std::string value;
        for (const auto& run : paragraph.runs)
            value += run.text;
        return value;
    }

    void paragraph_geometry_cases()
    {
        using namespace mirrorfly;
        WordDocument source;
        source.paragraphs.resize(3);
        for (int index = 0; index < 3; ++index)
        {
            auto& paragraph = source.paragraphs[index];
            paragraph.runs = {{"Paragraph geometry"}};
            paragraph.left_indent = 36.75;
            paragraph.right_indent = 12.75;
            paragraph.first_line_indent = -9.75;
            paragraph.space_before = 6.75;
            paragraph.space_after = 3.75;
            paragraph.line_spacing = 1.25;
            paragraph.line_spacing_rule = index;
            paragraph.line_spacing_points = index ? 15.75 : 0;
        }
        const auto serialized = serialize_word(source);
        const auto imported = parse_word(serialized.parts);
        check(serialized.success && imported.success, "fractional paragraph geometry fixture imports");
        auto document = create_word_document(imported.document);
        const auto format = document->begin().blockFormat();
        check(format.leftMargin() == 49 && format.rightMargin() == 17 && format.textIndent() == -13 &&
                format.topMargin() == 9 && format.bottomMargin() == 5,
            "paragraph points convert to 96-dpi document pixels exactly once");
        auto block = document->begin();
        for (int rule = 0; rule < 3; ++rule, block = block.next())
        {
            const auto state = inspect_word_document(*document, block.position() + 1);
            check(state.value("leftIndent").toDouble() == 36.75 &&
                    state.value("rightIndent").toDouble() == 12.75 &&
                    state.value("firstLineIndent").toDouble() == -9.75 &&
                    state.value("spaceBefore").toDouble() == 6.75 &&
                    state.value("spaceAfter").toDouble() == 3.75 &&
                    state.value("lineSpacingRule").toInt() == rule &&
                    (rule ? state.value("lineSpacingPoints").toDouble() == 15.75
                          : state.value("spacing").toDouble() == 1.25),
                "inspection reports model points and each imported line-spacing rule");
        }
        auto extracted = extract_word_document(*document);
        auto saved = serialize_word(extracted.document);
        check(extracted.success && saved.success && saved.parts.back().bytes == serialized.parts.back().bytes,
            "unchanged fractional paragraph geometry preserves original XML byte-for-byte");
        check(
            format_word_document(*document, 0, 3, "lineSpacing", QVariantMap{{"rule", 1}, {"value", 18.75}}),
            "exact point line spacing is editable");
        check(document->begin().blockFormat().lineHeight() == 25 &&
                document->begin().blockFormat().lineHeightType() == QTextBlockFormat::FixedHeight,
            "exact line spacing uses point-to-pixel conversion");
        document->undo();
        check(inspect_word_document(*document, 1).value("spacing").toDouble() == 1.25,
            "one undo restores both the original spacing rule and amount");
        document->redo();
        check(inspect_word_document(*document, 1).value("lineSpacingPoints").toDouble() == 18.75,
            "redo restores exact point line spacing");
        check(format_word_document(
                  *document, 0, 3, "lineSpacing", QVariantMap{{"rule", 2}, {"value", 21.75}}) &&
                document->begin().blockFormat().lineHeightType() == QTextBlockFormat::MinimumHeight &&
                document->begin().blockFormat().lineHeight() == 29,
            "minimum point line spacing is editable");
        check(format_word_document(*document, 0, 3, "rightIndent", 24.75), "right indentation is editable");
        const auto before_rejections = document->begin().blockFormat();
        for (const auto& settings : {QVariantMap{{"rule", 1.5}, {"value", 12}},
                 QVariantMap{{"rule", 3}, {"value", 12}}, QVariantMap{{"rule", 0}, {"value", 2.01}},
                 QVariantMap{{"rule", 1}, {"value", 145}}, QVariantMap{{"rule", 1}, {"value", "invalid"}},
                 QVariantMap{{"rule", 1}, {"value", std::numeric_limits<double>::quiet_NaN()}},
                 QVariantMap{{"rule", 1}}})
            check(!format_word_document(*document, 0, 3, "lineSpacing", settings),
                "invalid line-spacing maps are rejected before mutation");
        for (const auto* action :
            {"leftIndent", "rightIndent", "firstLineIndent", "spaceBefore", "spaceAfter"})
            check(!format_word_document(*document, 0, 3, action, "invalid"),
                "non-numeric paragraph geometry is rejected instead of silently becoming zero");
        check(!format_word_document(*document, 0, 3, "rightIndent", 505) &&
                !format_word_document(*document, 0, 3, "firstLineIndent", -37) &&
                document->begin().blockFormat() == before_rejections,
            "invalid geometry leaves every paragraph property unchanged");
        extracted = extract_word_document(*document);
        saved = serialize_word(extracted.document);
        const auto reopened = parse_word(saved.parts);
        check(extracted.success && saved.success && reopened.success &&
                reopened.document.paragraphs[0].right_indent == 24.75 &&
                reopened.document.paragraphs[0].line_spacing_rule == 2 &&
                reopened.document.paragraphs[0].line_spacing_points == 21.75 &&
                reopened.document.paragraphs[1].line_spacing_points == 15.75,
            "edited point geometry survives preserved-package save and reopen without altering other "
            "paragraphs");
        check(
            format_word_document(*document, 0, 3, "lineSpacing", QVariantMap{{"rule", 0}, {"value", 1.75}}) &&
                inspect_word_document(*document, 1).value("spacing").toDouble() == 1.75,
            "line spacing can return from point rules to a fractional multiplier");
    }

    void local_format_layout_case()
    {
        using namespace mirrorfly;
        WordDocument source;
        source.paragraphs.resize(300);
        for (auto& paragraph : source.paragraphs)
        {
            paragraph.runs = {{"Local format test"}};
            paragraph.alignment = 4;
        }
        auto document = create_word_document(source, 360);
        document->documentLayout()->documentSize();
        QCoreApplication::processEvents();
        auto* remote_layout = document->lastBlock().layout();
        const auto remote_formats = remote_layout->formats();
        check(
            format_word_document(*document, 0, 3, "bold", true) && remote_layout->formats() == remote_formats,
            "local formatting retains unrelated paragraph display formats");
        const auto end = document->begin().next().position();
        refresh_word_distribution(*document, 0, end);
        document->documentLayout()->documentSize();
        check(document->begin().layout()->lineAt(0).cursorToX(17) > 358 &&
                remote_layout->formats() == remote_formats,
            "bounded distribution refresh preserves current geometry and untouched layouts");
        document->undo();
        QCoreApplication::processEvents();
        check(!inspect_word_document(*document, 1).value("bold").toBool() &&
                remote_layout->formats() == remote_formats,
            "undo restores local text style and retains unrelated paragraph geometry");
    }

    void table_gap_cases()
    {
        using namespace mirrorfly;
        auto parts = serialize_word(WordDocument{}).parts;
        parts.back().bytes =
            R"xml(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>
<w:p><w:r><w:t>Body</w:t></w:r></w:p>
<w:tbl><w:tblGrid><w:gridCol w:w="1000"/><w:gridCol w:w="1000"/><w:gridCol w:w="1000"/></w:tblGrid>
<w:tr><w:trPr><w:gridBefore w:val="1"/></w:trPr><w:tc><w:tcPr><w:gridSpan w:val="2"/></w:tcPr><w:p><w:r><w:t>First</w:t></w:r></w:p></w:tc></w:tr>
<w:tr><w:trPr><w:gridAfter w:val="1"/></w:trPr><w:tc><w:tcPr><w:gridSpan w:val="2"/></w:tcPr><w:p><w:r><w:t>Last</w:t></w:r></w:p></w:tc></w:tr>
</w:tbl><w:p><w:r><w:t>Tail</w:t></w:r></w:p></w:body></w:document>)xml";
        const auto source = parse_word(parts);
        check(source.success, "table grid gap fixture loads");
        auto editor = create_word_document(source.document);
        auto extracted = extract_word_document(*editor);
        auto saved = serialize_word(extracted.document);
        check(extracted.success && saved.success && saved.parts.back().bytes == parts.back().bytes &&
                extracted.document.paragraphs.size() == 4,
            "empty grid gaps do not create orphan paragraphs or change untouched XML");
        auto* table = qobject_cast<QTextTable*>(editor->rootFrame()->childFrames().value(0));
        check(table != nullptr, "table gaps retain the source table");
        if (!table)
            return;
        for (const auto& coordinate : {std::pair{0, 0}, std::pair{1, 2}})
        {
            auto cursor = table->cellAt(coordinate.first, coordinate.second).firstCursorPosition();
            const auto info = inspect_word_document(*editor, cursor.position());
            check(info.value("tableGap").toBool() && !info.value("readOnlyReason").toString().isEmpty() &&
                    !format_word_document(*editor, cursor.position(), cursor.position(), "bold", true),
                "GUI and AI format inspection identify non-cell gaps and reject formatting them");
            cursor.insertText("Must not disappear");
            check(!extract_word_document(*editor).success,
                "text placed in a synthetic gap must fail instead of being silently discarded");
            editor->undo();
        }
        auto cursor = table->cellAt(1, 0).firstCursorPosition();
        check(!inspect_word_document(*editor, cursor.position()).value("tableGap").toBool(),
            "real merged cells beside gaps remain editable");
        cursor.insertText("Edited ");
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        const auto reopened = parse_word(saved.parts);
        check(saved.success && reopened.success && text(reopened.document.paragraphs[2]) == "Edited Last" &&
                reopened.document.tables[0].cells.size() == 2,
            "editing a real cell next to a gap saves and reopens without creating a fake cell");
        editor->undo();
        check(
            serialize_word(extract_word_document(*editor).document).parts.back().bytes == parts.back().bytes,
            "undo restores the original table with gridBefore and gridAfter");
    }

    void structure_cases()
    {
        using namespace mirrorfly;
        auto parts = serialize_word(WordDocument{}).parts;
        parts.back().bytes =
            R"xml(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml"><w:body>
<w:p w14:paraId="12345678"><w:pPr><w:tabs><w:tab w:val="left" w:pos="300"/></w:tabs></w:pPr><w:r><w:rPr><w:lang w:val="zh-CN"/></w:rPr><w:t>Alpha</w:t></w:r></w:p>
<w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Beta</w:t></w:r></w:p>
<w:tbl><w:tblGrid><w:gridCol w:w="3000"/></w:tblGrid><w:tr><w:tc><w:tcPr><w:vMerge w:val="restart"/></w:tcPr><w:p><w:r><w:t>Cell</w:t></w:r></w:p></w:tc></w:tr><w:tr><w:tc><w:tcPr><w:vMerge/></w:tcPr><w:p/></w:tc></w:tr></w:tbl>
<w:p><w:r><w:t>Tail</w:t></w:r></w:p><w:sectPr><w:pgSz w:w="11906" w:h="16838"/></w:sectPr></w:body></w:document>)xml";
        const auto source = parse_word(parts);
        check(source.success && source.document.paragraphs.size() == 5, "structural fixture loads");
        auto editor = create_word_document(source.document);
        auto extracted = extract_word_document(*editor);
        auto saved = serialize_word(extracted.document);
        check(extracted.success && saved.success && saved.parts.back().bytes == parts.back().bytes,
            "table frame separators and hidden merge continuations roundtrip unchanged");
        QTextCursor cursor(editor.get());
        cursor.setPosition(2);
        cursor.insertBlock();
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        if (!extracted.success || !saved.success)
            std::cerr << extracted.error << ' ' << saved.error << '\n';
        auto reopened = parse_word(saved.parts);
        check(extracted.success && saved.success && reopened.success &&
                reopened.document.paragraphs.size() == 6 && text(reopened.document.paragraphs[0]) == "Al" &&
                text(reopened.document.paragraphs[1]) == "pha" && reopened.document.tables.size() == 1 &&
                reopened.document.tables[0].cells[0].row_span == 2 &&
                reopened.document.sections[0].paragraph_count == 6,
            "Enter splits a source paragraph and retains table, hidden paragraphs and section ranges");
        editor->undo();
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        check(saved.success && saved.parts.back().bytes == parts.back().bytes,
            "undo of structural editing restores byte-identical original package");
        cursor.setPosition(editor->begin().next().position());
        cursor.deletePreviousChar();
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        reopened = parse_word(saved.parts);
        check(extracted.success && saved.success && reopened.success &&
                reopened.document.paragraphs.size() == 4 &&
                text(reopened.document.paragraphs[0]) == "AlphaBeta" &&
                reopened.document.paragraphs[0].runs.back().bold,
            "Backspace merges different source runs without losing text or bold formatting");
        editor->undo();
        QTextTable* table = nullptr;
        for (auto* frame : editor->rootFrame()->childFrames())
            if (auto* found = qobject_cast<QTextTable*>(frame))
                table = found;
        check(table != nullptr, "source table has an editable Qt table");
        if (table)
        {
            const auto start = table->cellAt(0, 0).firstCursorPosition().position();
            check(inspect_word_document(*editor, start).value("inTable").toBool(),
                "table toolbar inspects the current cell through the public adapter");
            check(!format_word_document(*editor, 0, 0, "cellFill", "#123456") &&
                    !format_word_document(*editor, start, editor->characterCount() - 1, "cellAlign", 1) &&
                    !format_word_document(*editor, start, start, "cellTop", -1),
                "cell formatting rejects body cursors, cross-cell selections and invalid padding");
            for (const auto& item : QVariantMap{{"cellFill", "#123456"}, {"cellAlign", 2}, {"cellLeft", 9.25},
                     {"cellTop", 12.5}, {"cellRight", 15.75}, {"cellBottom", 18.0}}
                     .asKeyValueRange())
                check(format_word_document(*editor, start, start, item.first, item.second),
                    "cell style actions are editable and undoable");
            extracted = extract_word_document(*editor);
            saved = serialize_word(extracted.document);
            reopened = parse_word(saved.parts);
            check(extracted.success && saved.success && reopened.success &&
                    reopened.document.tables[0].cells[0].background == "#123456" &&
                    reopened.document.tables[0].cells[0].vertical_alignment == 2 &&
                    reopened.document.tables[0].cells[0].margins ==
                        std::array<double, 4>{9.25, 12.5, 15.75, 18},
                "GUI cell styles survive extraction and OOXML reopen without changing the merged structure");
            auto restored = create_word_document(reopened.document);
            const auto restored_style = inspect_word_document(*restored, start);
            check(restored_style.value("cellFill").toString() == "#123456" &&
                    restored_style.value("cellAlign").toInt() == 2 &&
                    restored_style.value("cellLeft").toDouble() == 9.25,
                "rebuilding the editor retains cell styles after inserting character formats");
            for (int step = 0; step < 6; ++step)
                editor->undo();
            extracted = extract_word_document(*editor);
            saved = serialize_word(extracted.document);
            check(saved.success && saved.parts.back().bytes == parts.back().bytes,
                "undo of cell styles restores the source package exactly");
            cursor = table->cellAt(0, 0).firstCursorPosition();
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 2);
            cursor.insertBlock();
            cursor.insertText("NEW");
            extracted = extract_word_document(*editor);
            saved = serialize_word(extracted.document);
            reopened = parse_word(saved.parts);
            if (!extracted.success || !saved.success)
                std::cerr << extracted.error << ' ' << saved.error << '\n';
            check(extracted.success && saved.success && reopened.success &&
                    reopened.document.tables[0].cells[0].blocks.size() == 2 &&
                    reopened.document.tables[0].cells[0].row_span == 2 &&
                    text(reopened.document.paragraphs[2]) == "Ce" &&
                    text(reopened.document.paragraphs[3]) == "NEWll" &&
                    text(reopened.document.paragraphs.back()) == "Tail",
                "splitting inside merged table cell writes two paragraphs inside that cell only");
            editor->undo();
            editor->undo();
        }
        editor = create_word_document(source.document);
        cursor = QTextCursor(editor.get());
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock(QTextBlockFormat{}, QTextCharFormat{});
        cursor.insertText("New paragraph");
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        reopened = parse_word(saved.parts);
        check(extracted.success && saved.success && reopened.success &&
                text(reopened.document.paragraphs.back()) == "New paragraph",
            "new paragraphs and runs without source IDs write before the final section properties");
        WordDocument foreign;
        foreign.paragraphs[0].runs = {{"Foreign"}};
        foreign.paragraphs[0].runs[0].italic = true;
        auto foreign_source = parse_word(serialize_word(foreign).parts);
        auto clipboard = create_word_document(foreign_source.document);
        clear_word_package_identity(*clipboard);
        editor = create_word_document(source.document);
        cursor = QTextCursor(editor.get());
        cursor.setPosition(2);
        cursor.insertFragment(QTextDocumentFragment(clipboard.get()));
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        reopened = parse_word(saved.parts);
        check(extracted.success && saved.success && reopened.success &&
                text(reopened.document.paragraphs[0]) == "AlForeignpha" &&
                extracted.document.paragraphs[0].runs[1].source_id == 0 &&
                reopened.document.paragraphs[0].runs[1].italic,
            "foreign clipboard formatting survives without aliasing source run IDs");
    }

    void cases()
    {
        using namespace mirrorfly;
        {
            auto package = serialize_word(WordDocument{}).parts;
            package.back().bytes =
                R"xml(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>
<w:p><w:pPr><w:spacing w:line="320" w:lineRule="exact"/><w:ind w:right="240"/><w:keepNext/></w:pPr>
<w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText>REF bookmark</w:instrText></w:r><w:r><w:fldChar w:fldCharType="separate"/></w:r>
<w:r><w:rPr><w:color w:val="AABBCC"/><w:shd w:fill="DDEEFF"/><w:sz w:val="21"/></w:rPr><w:t>Text</w:t></w:r>
<w:r><w:fldChar w:fldCharType="end"/></w:r><w:r><w:footnoteReference w:id="1"/></w:r></w:p>
<w:p><w:r><w:t>Second</w:t></w:r></w:p></w:body></w:document>)xml";
            const auto source = parse_word(package);
            check(source.success, "protected field and uppercase-color fixture loads");
            auto editor = create_word_document(source.document);
            const auto read = extract_word_document(*editor);
            const auto saved = serialize_word(read.document);
            check(read.success && saved.success && saved.parts.back().bytes == package.back().bytes,
                "unchanged editor preserves fields, zero-width references, exact spacing and color spelling");
            std::unique_ptr<QTextDocument> clone(editor->clone());
            QTextCursor(clone.get()).insertText("Added ");
            const auto cloned = extract_word_document(*clone, editor.get());
            check(cloned.success && serialize_word(cloned.document).success,
                "preflight clones preserve the same package context as the live editor");
            check(format_word_document(*editor, 0, 4, "clear", true), "clear imported text formatting");
            const auto cleared = extract_word_document(*editor);
            check(cleared.success && serialize_word(cleared.document).success &&
                    cleared.document.paragraphs[0].runs[3].source_id ==
                        source.document.paragraphs[0].runs[3].source_id,
                "clear formatting retains source identity and protected neighboring runs");
            editor->undo();
            check(format_word_document(*editor, 0, 4, "style", "quote"), "apply imported paragraph style");
            const auto styled = extract_word_document(*editor);
            check(styled.success && serialize_word(styled.document).success &&
                    styled.document.paragraphs[0].source_id == source.document.paragraphs[0].source_id,
                "paragraph style retains package paragraph and run identities");
            editor->undo();
            QTextCursor split(editor.get());
            split.setPosition(2);
            split.insertBlock();
            const auto unsafe_split = extract_word_document(*editor);
            check(!unsafe_split.success || !serialize_word(unsafe_split.document).success,
                "splitting protected fields never overwrites their source paragraph");
            editor->undo();
            QTextCursor deleted(editor.get());
            deleted.setPosition(0);
            deleted.setPosition(editor->lastBlock().position(), QTextCursor::KeepAnchor);
            deleted.removeSelectedText();
            const auto unsafe_delete = extract_word_document(*editor);
            check(!unsafe_delete.success || !serialize_word(unsafe_delete.document).success,
                "deleting protected field paragraphs cannot silently discard their structure");
        }
        {
            WordDocument spread;
            spread.paragraphs[0].runs = {{"镜蝶办公"}};
            spread.paragraphs[0].alignment = 4;
            auto distributed = create_word_document(spread);
            distributed->setTextWidth(320);
            refresh_word_distribution(*distributed);
            distributed->documentLayout()->documentSize();
            auto line = distributed->begin().layout()->lineAt(0);
            check(line.isValid() && line.cursorToX(4) > 318 && line.cursorToX(4) <= 320,
                "distributed paragraph expands its last line across available width");
            const auto extracted = extract_word_document(*distributed);
            check(extracted.success && extracted.document.paragraphs[0].alignment == 4 &&
                    extracted.document.paragraphs[0].runs[0].character_spacing == 0,
                "visual distribution does not contaminate authored character spacing");
            check(!distributed->isModified() && !distributed->isUndoAvailable(),
                "distribution layout does not modify document history");
            check(
                format_word_document(*distributed, 0, 4, "align", 3), "switch from distributed to justified");
            distributed->documentLayout()->documentSize();
            check(distributed->begin().layout()->lineAt(0).cursorToX(4) < 160,
                "normal justification leaves final line at natural width");
            distributed->undo();
            QCoreApplication::processEvents();
            distributed->documentLayout()->documentSize();
            check(inspect_word_document(*distributed, 1).value("align") == 4 &&
                    distributed->begin().layout()->lineAt(0).cursorToX(4) > 318,
                "undo restores distributed geometry");
            distributed->setTextWidth(240);
            QCoreApplication::processEvents();
            distributed->documentLayout()->documentSize();
            check(distributed->begin().layout()->lineAt(0).cursorToX(4) > 238 &&
                    distributed->begin().layout()->lineAt(0).cursorToX(4) <= 240,
                "distributed text follows document width changes");
            const auto saved = serialize_word(extracted.document);
            const auto reopened = parse_word(saved.parts);
            check(saved.success && reopened.success && reopened.document.paragraphs[0].alignment == 4,
                "distributed paragraph is saved as an OOXML alignment");
            check(format_word_document(*distributed, 0, 4, "rubyAuto", true), "annotate distributed text");
            distributed->documentLayout()->documentSize();
            const auto annotations = word_paragraph_decorations(*distributed);
            check(annotations.size() == 4 && annotations.front().toMap().value("width").toDouble() < 30 &&
                    annotations.front().toMap().value("x").toDouble() < 10,
                "distributed pinyin stays above the glyph rather than the expanded gap");
            const auto margin = distributed->rootFrame()->frameFormat().topMargin();
            check(format_word_document(*distributed, 0, 4, "size", 32) &&
                    distributed->rootFrame()->frameFormat().topMargin() > margin,
                "enlarging phonetic text refreshes first-line annotation clearance");
            for (const auto& text :
                QStringList{QStringLiteral("镜蝶办公帮助整理思路轻松完成日常工作与学习任务"),
                    QStringLiteral("One two three four five six seven eight nine ten"),
                    QString::fromUtf8("A😀中B")})
            {
                WordDocument mixed;
                mixed.paragraphs[0].alignment = 4;
                mixed.paragraphs[0].runs = {
                    {text.left(text.size() - 1).toStdString()}, {text.right(1).toStdString()}};
                mixed.paragraphs[0].runs.back().size = 20;
                mixed.paragraphs[0].runs.back().bold = true;
                auto layout = create_word_document(mixed);
                layout->setTextWidth(220);
                refresh_word_distribution(*layout);
                layout->documentLayout()->documentSize();
                const auto* lines = layout->begin().layout();
                for (int index = 0; index < lines->lineCount(); ++index)
                {
                    const auto current = lines->lineAt(index);
                    auto end = current.textStart() + current.textLength();
                    while (end > current.textStart() && text[end - 1].isSpace())
                        --end;
                    if (end - current.textStart() >= 2)
                        check(current.cursorToX(end) > 216 && current.cursorToX(end) <= 220.1,
                            "distributed mixed-font, wrapped and emoji lines fill the available width");
                }
                check(extract_word_document(*layout).document.paragraphs[0].runs.back().size == 20,
                    "last grapheme retains its authored font size");
            }
        }
        {
            WordDocument source;
            source.paragraphs = {WordParagraph{{WordRun{"keep before"}}}, WordParagraph{{WordRun{"10"}}},
                WordParagraph{{WordRun{"-2"}}}, WordParagraph{{WordRun{"3.5"}}},
                WordParagraph{{WordRun{"keep after"}}}};
            source.paragraphs[2].runs[0].bold = true;
            source.paragraphs[2].background = "#AABBCC";
            source.paragraphs[2].list = WordListKind::Bullet;
            auto sorted = create_word_document(source);
            const auto first = sorted->findBlockByNumber(1).position();
            const auto end = sorted->findBlockByNumber(4).position();
            check(format_word_document(*sorted, first + 1, end, "sort", "numberAscending"),
                "paragraph numeric sort commits");
            auto result = extract_word_document(*sorted);
            check(result.success && sorted->toPlainText() == "keep before\n-2\n3.5\n10\nkeep after" &&
                    result.document.paragraphs[1].runs[0].bold &&
                    result.document.paragraphs[1].background == "#aabbcc" &&
                    result.document.paragraphs[1].list == WordListKind::Bullet,
                "sort moves complete paragraph formatting and leaves boundary paragraphs intact");
            sorted->undo();
            check(
                sorted->toPlainText() == "keep before\n10\n-2\n3.5\nkeep after" && !sorted->isUndoAvailable(),
                "paragraph sort is one undoable transaction");
            sorted->redo();
            check(sorted->toPlainText() == "keep before\n-2\n3.5\n10\nkeep after", "paragraph sort redo");
            const auto before = sorted->toPlainText();
            check(!format_word_document(*sorted, 0, 0, "sort", "numberAscending") &&
                    sorted->toPlainText() == before,
                "invalid numeric paragraph sort does not modify content");
            WordDocument words;
            words.paragraphs = {WordParagraph{{WordRun{"item10"}}}, WordParagraph{{WordRun{"item2"}}},
                WordParagraph{{WordRun{"item2"}}}};
            words.paragraphs[1].runs[0].bold = true;
            auto natural = create_word_document(words);
            check(format_word_document(*natural, 0, 0, "sort", "textAscending"),
                "natural paragraph sort commits");
            result = extract_word_document(*natural);
            check(natural->toPlainText() == "item2\nitem2\nitem10" &&
                    result.document.paragraphs[0].runs[0].bold && !result.document.paragraphs[1].runs[0].bold,
                "natural sort keeps equal paragraphs stable");
        }
        WordDocument source;
        source.paragraphs[0].runs = {{"第一项"}};
        source.paragraphs[0].list = WordListKind::Bullet;
        source.paragraphs[0].left_indent = 36;
        source.paragraphs[0].first_line_indent = -18;
        source.paragraphs[0].space_before = 6;
        source.paragraphs[0].space_after = 9;
        WordParagraph second;
        second.runs = {{"第二项"}};
        second.list = WordListKind::Bullet;
        source.paragraphs.push_back(second);
        auto document = create_word_document(source);
        auto extracted = extract_word_document(*document);
        check(extracted.success && extracted.document.paragraphs.size() == 2 &&
                extracted.document.paragraphs[0].list == WordListKind::Bullet &&
                extracted.document.paragraphs[1].list == WordListKind::Bullet &&
                extracted.document.paragraphs[0].left_indent == 36 &&
                extracted.document.paragraphs[0].first_line_indent == -18 &&
                extracted.document.paragraphs[0].space_before == 6 &&
                extracted.document.paragraphs[0].space_after == 9,
            "Qt document preserves automatic lists, indentation and paragraph spacing");

        check(format_word_document(*document, 0, 3, "list", 2) &&
                format_word_document(*document, 0, 3, "listLevel", 2) &&
                format_word_document(*document, 0, 3, "spaceAfter", 12),
            "public document formatting applies numbering, level and paragraph spacing");
        const auto inspected = inspect_word_document(*document, 1);
        check(inspected.value("list").toInt() == 2 && inspected.value("listLevel").toInt() == 2 &&
                inspected.value("spaceAfter").toInt() == 12,
            "public inspection reports list and spacing state");
        document->setTextWidth(150);
        check(format_word_document(*document, 0, 3, "characterBorder", "#123456") &&
                format_word_document(*document, 0, 3, "characterSpacing", 1.5) &&
                format_word_document(*document, 0, 3, "rtl", true),
            "character border, spacing and direction apply through public formatting");
        extracted = extract_word_document(*document);
        check(extracted.success && extracted.document.paragraphs[0].right_to_left &&
                extracted.document.paragraphs[0].runs[0].character_spacing == 1.5 &&
                extracted.document.paragraphs[0].runs[0].border_color == "#123456" &&
                !word_paragraph_decorations(*document).isEmpty(),
            "new Word fields have drawable geometry and survive Qt extraction");
        document->undo();
        check(!inspect_word_document(*document, 1).value("rtl").toBool(),
            "direction changes form one undo step");
        check(!format_word_document(*document, 0, 3, "characterSpacing", 100),
            "invalid spacing cannot corrupt model");

        check(format_word_document(*document, 0, 3, "color", "#2468AC") &&
                format_word_document(*document, 0, 2, "outline", true) &&
                format_word_document(*document, 0, 3, "color", "#123456"),
            "outline and color handle mixed selections");
        extracted = extract_word_document(*document);
        check(extracted.success && extracted.document.paragraphs[0].runs[0].outline &&
                extracted.document.paragraphs[0].runs[0].color == "#123456" &&
                !extracted.document.paragraphs[0].runs.back().outline,
            "outline retains logical text color while normal runs stay filled");
        check(format_word_document(*document, 0, 2, "outline", false) &&
                inspect_word_document(*document, 1).value("color") == "#123456",
            "removing outline restores text fill");
        WordDocument annotated;
        annotated.paragraphs[0].runs = {{"中文"}};
        auto ruby = create_word_document(annotated);
        ruby->setTextWidth(120);
        check(format_word_document(*ruby, 0, 2, "rubyAuto", true),
            "offline phonetic guide annotates selection");
        auto extracted_ruby = extract_word_document(*ruby);
        check(extracted_ruby.success && ruby->toPlainText() == "中文" &&
                extracted_ruby.document.paragraphs[0].runs[0].ruby == "zhōng" &&
                extracted_ruby.document.paragraphs[0].runs[1].ruby == "wén",
            "annotations remain separate from document text");
        const auto decorations = word_paragraph_decorations(*ruby);
        check(decorations.size() == 2, "one phonetic label per character");
        for (const auto& item : decorations)
        {
            const auto rectangle = item.toMap();
            check(rectangle.value("y").toDouble() >= 0 && rectangle.value("height").toDouble() > 0,
                "phonetic guide stays inside exportable document bounds");
            if (rectangle.value("y").toDouble() < 0)
                std::cerr << "Ruby y: " << rectangle.value("y").toDouble() << '\n';
        }
        const auto saved_ruby = parse_word(serialize_word(extracted_ruby.document).parts);
        check(saved_ruby.success && saved_ruby.document.paragraphs[0].runs.size() == 2 &&
                saved_ruby.document.paragraphs[0].runs[0].text == "中" &&
                saved_ruby.document.paragraphs[0].runs[0].ruby == "zhōng",
            "DOCX ruby structure roundtrips without duplicating phonetic text in body");
        check(format_word_document(*ruby, 0, 1, "ruby", QStringLiteral("zhòng")),
            "manual polyphonic correction");
        ruby->undo();
        check(inspect_word_document(*ruby, 1).value("ruby") == QStringLiteral("zhōng"),
            "phonetic correction undo");
        QTextCursor typed(ruby.get());
        typed.setPosition(2);
        typed.insertText("a");
        extracted_ruby = extract_word_document(*ruby);
        check(extracted_ruby.success && extracted_ruby.document.paragraphs[0].runs.back().text == "a" &&
                extracted_ruby.document.paragraphs[0].runs.back().ruby.empty(),
            "new typed characters do not inherit an unrelated reading");
        check(format_word_document(*ruby, 0, 2, "rubyClear", true) &&
                word_paragraph_decorations(*ruby).isEmpty(),
            "clear phonetic guide keeps body text");
        WordDocument candidate;
        const auto inserted = insert_word_template(candidate, 0, WordTemplateKind::WeeklyReport);
        candidate.paragraphs.pop_back();
        auto empty = create_word_document(WordDocument{});
        check(inserted.success && insert_word_paragraphs(*empty, 0, candidate.paragraphs),
            "insert a validated weekly report structure into the live document");
        extracted = extract_word_document(*empty);
        check(extracted.success && extracted.document.paragraphs.size() == inserted.inserted_paragraphs &&
                extracted.document.paragraphs.back().list == WordListKind::Numbered,
            "inserted template remains a true numbered structure");
        empty->undo();
        check(empty->toPlainText().isEmpty(), "template insertion is one QTextDocument undo step");
        empty->redo();
        check(empty->toPlainText().contains(QStringLiteral("周报")), "template insertion can be redone");
    }

    void decoration_index_cases()
    {
        using namespace mirrorfly;
        WordDocument source;
        source.paragraphs.resize(1200);
        for (auto& paragraph : source.paragraphs)
            paragraph.runs = {{"中文 paragraph"}};
        auto document = create_word_document(source, 500);
        document->documentLayout()->documentSize();
        check(word_decoration_blocks(*document).isEmpty(),
            "plain documents have no decoration candidates regardless of paragraph count");
        const auto matches_scan = [&]()
        {
            document->documentLayout()->documentSize();
            std::unique_ptr<QTextDocument> reference(document->clone());
            reference->documentLayout()->documentSize();
            const auto expected = word_paragraph_decorations(*reference);
            return word_paragraph_decorations(*document) == expected;
        };
        const int middle = document->findBlockByNumber(600).position();
        check(format_word_document(*document, middle, middle + 2, "rubyAuto", true) &&
                format_word_document(*document, middle, middle + 4, "characterBorder", "#123456") &&
                format_word_document(*document, 0, 1, "paragraphBorder", "#654321") &&
                word_decoration_blocks(*document).size() == 2 && matches_scan(),
            "ruby and both border kinds update the candidate index and match a full scan");
        QTextCursor cursor(document.get());
        cursor.insertText(QStringLiteral("prefix\n"));
        check(matches_scan(), "paragraph split shifts indexed decoration positions correctly");
        cursor.setPosition(document->findBlockByNumber(600).position());
        cursor.setPosition(document->findBlockByNumber(602).position(), QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        check(matches_scan(), "deleting decorated paragraphs removes invalid block identities");
        document->undo();
        check(matches_scan(), "undo restores decorated paragraph identities");
        document->redo();
        check(matches_scan(), "redo removes restored decoration identities");
        document->clear();
        check(word_decoration_blocks(*document).isEmpty() && word_paragraph_decorations(*document).isEmpty(),
            "document reset does not retain decoration blocks from the previous content");
    }

    void style_cases()
    {
        using namespace mirrorfly;
        auto parts = serialize_word(WordDocument{}).parts;
        parts.back().bytes =
            "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:body>"
            "<w:p><w:r><w:t>Style text</w:t></w:r></w:p></w:body></w:document>";
        for (auto& part : parts)
            if (part.path == "word/_rels/document.xml.rels")
                part.bytes.insert(part.bytes.find("</Relationships>"),
                    "<Relationship Id='style' "
                    "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles' "
                    "Target='styles.xml'/>");
        parts.push_back({"word/styles.xml",
            "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
            "<w:style w:type='paragraph' w:styleId='Normal' w:default='1'><w:pPr><w:jc w:val='center'/>"
            "<w:spacing w:line='270' w:after='160'/><w:shd w:fill='FFEEDD'/></w:pPr><w:rPr><w:b/><w:sz "
            "w:val='21'/>"
            "<w:rFonts w:ascii='Arial' w:eastAsia='SimSun'/></w:rPr></w:style></w:styles>"});
        const auto loaded = parse_word(parts);
        check(loaded.success, "UI inherited style fixture loads");
        if (!loaded.success)
            return;
        auto editor = create_word_document(loaded.document);
        auto extracted = extract_word_document(*editor);
        auto saved = serialize_word(extracted.document);
        bool identical = extracted.success && saved.success && saved.parts.size() == parts.size();
        for (std::size_t index = 0; identical && index < parts.size(); ++index)
            identical = parts[index].path == saved.parts[index].path &&
                parts[index].bytes == saved.parts[index].bytes;
        check(identical, "resolved default styles cross the editor without flattening the source package");
        const auto state = inspect_word_document(*editor, 4);
        check(state.value("bold").toBool() && state.value("size").toDouble() == 10.5,
            "toolbar selection reads inherited bold and fractional font size");
        if (QFontDatabase::families().contains(QStringLiteral("Arial")))
        {
            QTextCursor cursor(editor.get());
            cursor.setPosition(4);
            QTextLayout layout(QStringLiteral("Style text"), cursor.charFormat().font());
            layout.setCacheEnabled(true);
            layout.beginLayout();
            layout.createLine().setLineWidth(400);
            layout.endLayout();
            QImage image(400, 100, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            layout.draw(&painter, QPointF{});
            painter.end();
            const auto glyphs = layout.glyphRuns();
            check(!glyphs.isEmpty() && glyphs.front().rawFont().familyName() == QStringLiteral("Arial"),
                "Latin text uses its declared Latin font, not the East Asian fallback");
        }
        check(format_word_document(*editor, 0, 5, "bold", false), "toolbar clears inherited bold");
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        const auto reopened = parse_word(saved.parts);
        check(saved.success && reopened.success && !reopened.document.paragraphs[0].runs.front().bold &&
                reopened.document.paragraphs[0].runs.back().bold,
            "selected style changes save while the unselected text retains inherited bold");
        editor->undo();
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        identical = extracted.success && saved.success && saved.parts.size() == parts.size();
        for (std::size_t index = 0; identical && index < parts.size(); ++index)
            identical = parts[index].bytes == saved.parts[index].bytes;
        check(identical, "undoing inherited style edits restores the original package bytes");
        check(format_word_document(*editor, 0, 5, "paragraphFill", QString{}),
            "toolbar clears inherited paragraph fill");
        extracted = extract_word_document(*editor);
        saved = serialize_word(extracted.document);
        check(saved.success && parse_word(saved.parts).document.paragraphs[0].background.empty(),
            "cleared paragraph fill is extracted as empty and stays cleared after reopening");
    }
}

int run_word_document_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    table_gap_cases();
    structure_cases();
    paragraph_geometry_cases();
    local_format_layout_case();
    decoration_index_cases();
    style_cases();
    cases();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_document_tests(argc, argv);
}
