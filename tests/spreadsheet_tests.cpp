#include <mirrorfly/spreadsheet.hpp>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    int failures = 0;

    void expect(bool condition, const std::string& description)
    {
        if (!condition)
        {
            std::cerr << description << '\n';
            ++failures;
        }
    }

    std::vector<mirrorfly::OfficePart> fixture(
        const std::string& workbook_extra = {}, const std::string& sheet_extra = {})
    {
        return {{"[Content_Types].xml",
                    "<Types xmlns='http://schemas.openxmlformats.org/package/2006/content-types'>"
                    "<Default Extension='rels' ContentType='application/vnd.openxmlformats-package."
                    "relationships+xml'/><Default Extension='xml' ContentType='application/xml'/>"
                    "<Override PartName='/xl/workbook.xml' ContentType='application/vnd.openxmlformats-"
                    "officedocument.spreadsheetml.sheet.main+xml'/>"
                    "<Override PartName='/xl/worksheets/sheet1.xml' ContentType='application/vnd."
                    "openxmlformats-officedocument.spreadsheetml.worksheet+xml'/>"
                    "<Override PartName='/xl/sharedStrings.xml' ContentType='application/vnd."
                    "openxmlformats-officedocument.spreadsheetml.sharedStrings+xml'/>"
                    "<Override PartName='/xl/calcChain.xml' ContentType='application/vnd."
                    "openxmlformats-officedocument.spreadsheetml.calcChain+xml'/></Types>"},
            {"_rels/.rels",
                "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
                "<Relationship Id='entry' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
                "relationships/officeDocument' Target='xl/workbook.xml'/></Relationships>"},
            {"xl/workbook.xml",
                "<workbook xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main' "
                "xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'>" +
                    workbook_extra +
                    "<sheets><sheet name='数据' sheetId='1' r:id='sheet'/></sheets>"
                    "<calcPr calcId='123' calcMode='manual'/></workbook>"},
            {"xl/_rels/workbook.xml.rels",
                "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
                "<Relationship Id='sheet' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
                "relationships/worksheet' Target='worksheets/sheet1.xml'/>"
                "<Relationship Id='strings' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
                "relationships/sharedStrings' Target='sharedStrings.xml'/>"
                "<Relationship Id='chain' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
                "relationships/calcChain' Target='calcChain.xml'/></Relationships>"},
            {"xl/sharedStrings.xml",
                "<sst xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'>"
                "<si><r><t>Hello</t></r><r><t xml:space='preserve'> world</t></r></si></sst>"},
            {"xl/calcChain.xml",
                "<calcChain xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'><c r='E1'/></"
                "calcChain>"},
            {"xl/worksheets/sheet1.xml",
                "<worksheet xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'>" +
                    sheet_extra +
                    "<sheetData><row r='1'>"
                    "<c r='A1' s='3' t='s'><v>0</v></c>"
                    "<c r='B1'><v>12345678901234567890.123</v></c>"
                    "<c r='C1' t='b'><v>1</v></c><c r='D1' t='e'><v>#DIV/0!</v></c>"
                    "<c r='E1'><f>SUBTOTAL(9,B1)</f><v>5</v></c>"
                    "<c r='F1' t='inlineStr'><is><r><t>Rich</t></r><r><t> text</t></r></is></c>"
                    "<c r='G1'><f t='shared' si='0' ref='G1:H2'>G2+1</f><v>2</v></c>"
                    "<c r='H1'><f t='shared' si='0'/><v>3</v></c>"
                    "<c r='K1'><f t='array' ref='K1:L2'>B1*2</f><v>4</v></c>"
                    "<c r='M1' t='inlineStr'><is><t>=literal</t></is></c></row>"
                    "<row r='2'><c r='B2' s='7' custom='keep'><v>42</v><extLst><ext uri='keep'/></"
                    "extLst></c></row></sheetData><mergeCells count='1'><mergeCell ref='A3:B3'/></"
                    "mergeCells></worksheet>"},
            {"custom/opaque.bin", "opaque-bytes"}};
    }

    const mirrorfly::OfficePart* find_part(
        const std::vector<mirrorfly::OfficePart>& parts, const std::string& path)
    {
        for (const auto& part : parts)
        {
            if (part.path == path)
            {
                return &part;
            }
        }
        return nullptr;
    }

    void check_addresses_and_values()
    {
        using namespace mirrorfly;
        expect(parse_spreadsheet_address("A1") && parse_spreadsheet_address("A1")->row == 0 &&
                parse_spreadsheet_address("XFD1048576")->column == maximum_spreadsheet_columns - 1,
            "spreadsheet addresses use zero-based bounded coordinates");
        expect(!parse_spreadsheet_address("A0") && !parse_spreadsheet_address("XFE1") &&
                spreadsheet_address({0, 0}) == "A1" &&
                spreadsheet_address({maximum_spreadsheet_rows - 1, maximum_spreadsheet_columns - 1}) ==
                    "XFD1048576",
            "spreadsheet address conversion rejects out-of-range values");
        expect(is_spreadsheet_path("报告.XLSX") && !is_spreadsheet_path("legacy.xls"),
            "only XLSX paths are accepted");

        auto parsed = parse_spreadsheet(fixture());
        expect(parsed.error == SpreadsheetError::None && parsed.document.sheets.size() == 1,
            "real workbook relationships and sheet order parse");
        if (parsed.error != SpreadsheetError::None)
        {
            return;
        }
        const auto& document = parsed.document;
        expect(spreadsheet_cell(document, 0, {0, 0}).value.text == "Hello world",
            "rich shared strings are concatenated");
        expect(spreadsheet_cell(document, 0, {0, 1}).value.kind == SpreadsheetValueKind::Number &&
                spreadsheet_cell(document, 0, {0, 1}).value.text == "12345678901234567890.123",
            "numeric values retain their original precision text");
        expect(spreadsheet_cell(document, 0, {0, 2}).value.kind == SpreadsheetValueKind::Boolean &&
                spreadsheet_cell(document, 0, {0, 3}).value.kind == SpreadsheetValueKind::Error &&
                spreadsheet_cell(document, 0, {0, 5}).value.text == "Rich text",
            "boolean, error, and inline rich-string kinds parse");
        const auto formula = spreadsheet_cell(document, 0, {0, 4});
        expect(formula.formula_cell && formula.formula == "SUBTOTAL(9,B1)" && formula.value.text == "5" &&
                !formula.editable,
            "formula text and cached value are exposed without making the formula editable");
        expect(!spreadsheet_cell(document, 0, {1, 7}).editable,
            "an empty shared-formula dependent cell is protected through the public guard");
        expect(!spreadsheet_cell(document, 0, {1, 11}).editable,
            "an empty array or spill dependent cell is protected through the public guard");
        expect(
            spreadsheet_cell(document, 0, {2, 0}).editable && !spreadsheet_cell(document, 0, {2, 1}).editable,
            "merge owner remains editable while covered cells are protected");
    }

    void check_edits_and_serialization()
    {
        using namespace mirrorfly;
        auto parsed = parse_spreadsheet(fixture());
        auto& document = parsed.document;
        SpreadsheetEditCommand batch_first;
        batch_first.address = {0, 0};
        batch_first.value = {SpreadsheetValueKind::Text, "batch"};
        auto batch_last = batch_first;
        batch_last.address = {0, 4};
        expect(apply_spreadsheet_edits(document, {batch_first, batch_last}).error != SpreadsheetError::None &&
                document.edits.empty() && !document.caches_stale,
            "a read-only last cell rejects the whole batch without dirtying the document");
        expect(
            apply_spreadsheet_edits(document, {batch_first, batch_first}).error != SpreadsheetError::None &&
                document.edits.empty(),
            "duplicate addresses reject the batch atomically");
        expect(apply_spreadsheet_edits(document,
                   std::vector<SpreadsheetEditCommand>(maximum_spreadsheet_batch_cells + 1, batch_first))
                        .error != SpreadsheetError::None &&
                document.edits.empty(),
            "oversized batches are rejected before mutation");
        const auto original_parts = document.original_parts;
        SpreadsheetEditCommand command;
        command.address = {1, 1};
        command.value = {SpreadsheetValueKind::Text, "updated"};
        auto edited = apply_spreadsheet_edit(document, command);
        expect(edited.error == SpreadsheetError::None && edited.changed && document.caches_stale &&
                document.original_parts == original_parts,
            "a sparse edit marks caches stale without copying or replacing original parts");
        command.address = {0, 1};
        command.value = {SpreadsheetValueKind::Number, "9007199254740993.0000000000001"};
        expect(apply_spreadsheet_edit(document, command).error == SpreadsheetError::None,
            "raw high-precision numeric edits are accepted without floating-point conversion");
        command.address = {0, 0};
        command.value = {};
        expect(apply_spreadsheet_edit(document, command).error == SpreadsheetError::None,
            "clearing a styled shared-string cell is recorded as a sparse edit");
        command.address = {3, 0};
        command.value = {SpreadsheetValueKind::Number, "=SUM(A1:A2)"};
        const auto edited_sheet_count = document.edits.size();
        const auto edited_cell_count = document.edits.at(0).size();
        expect(apply_spreadsheet_edit(document, command).error == SpreadsheetError::InvalidValue &&
                document.edits.size() == edited_sheet_count &&
                document.edits.at(0).size() == edited_cell_count &&
                spreadsheet_cell(document, 0, {1, 1}).value.text == "updated",
            "invalid typed input is rejected atomically");

        command.address = {0, 12};
        command.value = {SpreadsheetValueKind::Text, "changed"};
        expect(apply_spreadsheet_edit(document, command).error == SpreadsheetError::None,
            "an imported literal text cell remains editable");
        command.value = {SpreadsheetValueKind::Text, "=literal"};
        expect(apply_spreadsheet_edit(document, command).error == SpreadsheetError::None &&
                document.edits.at(0).count(command.address) == 0,
            "restoring explicit text beginning with equals removes the sparse edit");

        const auto package = serialize_spreadsheet(document);
        const auto* sheet = find_part(package.parts, "xl/worksheets/sheet1.xml");
        const auto* opaque = find_part(package.parts, "custom/opaque.bin");
        expect(package.error == SpreadsheetError::None && sheet && opaque && opaque->bytes == "opaque-bytes",
            "serialization preserves untouched opaque package parts");
        if (!sheet)
        {
            return;
        }
        expect(sheet->bytes.find("r=\"B2\"") != std::string::npos &&
                sheet->bytes.find("s=\"7\"") != std::string::npos &&
                sheet->bytes.find("custom=\"keep\"") != std::string::npos &&
                sheet->bytes.find("uri=\"keep\"") != std::string::npos &&
                sheet->bytes.find("inlineStr") != std::string::npos,
            "cell edits retain style, attributes, and unrelated child nodes while writing inline strings");
        expect(sheet->bytes.find("r=\"A1\" s=\"3\"") != std::string::npos,
            "clearing a cell retains its style and cell identity");
        expect(sheet->bytes.find("r=\"M1\" t=\"inlineStr\"") != std::string::npos &&
                sheet->bytes.find("<f>=literal</f>") == std::string::npos,
            "literal text beginning with equals remains an inline string and never becomes a formula");
        expect(sheet->bytes.find("<f>SUBTOTAL(9,B1)</f><v>") == std::string::npos &&
                sheet->bytes.find("<f>SUBTOTAL(9,B1)</f>") != std::string::npos &&
                !find_part(package.parts, "xl/calcChain.xml"),
            "edited workbooks preserve formulas, remove their caches, and drop calcChain");
        const auto* workbook = find_part(package.parts, "xl/workbook.xml");
        expect(workbook && workbook->bytes.find("calcMode=\"auto\"") != std::string::npos &&
                workbook->bytes.find("fullCalcOnLoad=\"1\"") != std::string::npos &&
                workbook->bytes.find("forceFullCalc=\"1\"") != std::string::npos,
            "edited workbooks request a full automatic recalculation");
        const auto reopened = parse_spreadsheet(package.parts);
        expect(reopened.error == SpreadsheetError::None &&
                spreadsheet_cell(reopened.document, 0, {1, 1}).value.text == "updated" &&
                spreadsheet_cell(reopened.document, 0, {0, 1}).value.text ==
                    "9007199254740993.0000000000001" &&
                spreadsheet_cell(reopened.document, 0, {0, 0}).value.kind == SpreadsheetValueKind::Empty &&
                spreadsheet_cell(reopened.document, 0, {0, 4}).formula == "SUBTOTAL(9,B1)" &&
                spreadsheet_cell(reopened.document, 0, {0, 4}).value.kind == SpreadsheetValueKind::Empty,
            "serialized sparse edits and stale formula state reparse correctly");

        auto blank = make_spreadsheet();
        command = {};
        command.address = {9, 4};
        command.value = {SpreadsheetValueKind::Text, "new cell"};
        expect(apply_spreadsheet_edit(blank, command).error == SpreadsheetError::None &&
                parse_spreadsheet(serialize_spreadsheet(blank).parts).error == SpreadsheetError::None,
            "new sparse workbooks serialize and reparse");
    }

    void check_protection_and_malformed_inputs()
    {
        using namespace mirrorfly;
        auto protected_book = parse_spreadsheet(fixture("<workbookProtection lockStructure='1'/>", {}));
        expect(protected_book.error == SpreadsheetError::None && protected_book.document.read_only &&
                !spreadsheet_cell(protected_book.document, 0, {0, 0}).editable,
            "protected workbook structures are exposed as read-only");
        auto unlocked_book = parse_spreadsheet(fixture("<workbookProtection/>", {}));
        expect(unlocked_book.error == SpreadsheetError::None && !unlocked_book.document.read_only &&
                spreadsheet_cell(unlocked_book.document, 0, {3, 3}).editable,
            "an empty workbook protection record does not make the workbook read-only");
        auto protected_sheet = parse_spreadsheet(fixture({}, "<sheetProtection sheet='1'/>"));
        expect(protected_sheet.error == SpreadsheetError::None &&
                !spreadsheet_cell(protected_sheet.document, 0, {3, 3}).editable,
            "protected sheets also guard empty cells");
        auto unlocked_sheet = parse_spreadsheet(fixture({}, "<sheetProtection sheet='0'/>"));
        expect(unlocked_sheet.error == SpreadsheetError::None &&
                spreadsheet_cell(unlocked_sheet.document, 0, {3, 3}).editable,
            "a disabled sheet protection record does not make the sheet read-only");

        auto injected = parse_spreadsheet(fixture());
        injected.document.edits[0][{0, 4}] = {SpreadsheetValueKind::Text, "replace formula"};
        expect(serialize_spreadsheet(injected.document).error == SpreadsheetError::ReadOnly,
            "serialization rejects edits injected into protected formula cells");

        auto parts = fixture();
        for (auto& part : parts)
        {
            if (part.path == "xl/worksheets/sheet1.xml")
            {
                const auto reference = part.bytes.find(" ref='G1:H2'");
                part.bytes.erase(reference, std::string(" ref='G1:H2'").size());
            }
        }
        auto incomplete_shared = parse_spreadsheet(std::move(parts));
        expect(incomplete_shared.error == SpreadsheetError::None &&
                !spreadsheet_cell(incomplete_shared.document, 0, {5, 5}).editable,
            "a shared formula without a declared master range makes the whole sheet read-only");

        parts = fixture();
        for (auto& part : parts)
        {
            if (part.path == "xl/worksheets/sheet1.xml")
            {
                const auto reference = part.bytes.find(" ref='K1:L2'");
                part.bytes.erase(reference, std::string(" ref='K1:L2'").size());
            }
        }
        auto incomplete_array = parse_spreadsheet(std::move(parts));
        expect(incomplete_array.error == SpreadsheetError::None &&
                !spreadsheet_cell(incomplete_array.document, 0, {5, 5}).editable,
            "an array formula without a declared spill range makes the whole sheet read-only");

        parts = fixture();
        for (auto& part : parts)
        {
            if (part.path == "xl/worksheets/sheet1.xml")
            {
                const auto cell = part.bytes.find("<c r='A1'");
                part.bytes.insert(cell + std::string("<c r='A1'").size(), " cm='1'");
            }
        }
        auto metadata = parse_spreadsheet(std::move(parts));
        expect(metadata.error == SpreadsheetError::None &&
                !spreadsheet_cell(metadata.document, 0, {5, 5}).editable,
            "unhandled dynamic-array cell metadata makes the whole sheet read-only");

        parts = fixture();
        parts.push_back(parts.front());
        expect(parse_spreadsheet(std::move(parts)).error == SpreadsheetError::InvalidPackage,
            "duplicate package paths are rejected");
        parts = fixture();
        parts.push_back({"../escape.xml", "<x/>"});
        expect(parse_spreadsheet(std::move(parts)).error == SpreadsheetError::InvalidPackage,
            "package traversal paths are rejected");
        parts = fixture();
        for (auto& part : parts)
        {
            if (part.path == "xl/workbook.xml")
            {
                part.bytes = "<!DOCTYPE x [<!ENTITY e SYSTEM 'file:///secret'>]><workbook/>";
            }
        }
        expect(parse_spreadsheet(std::move(parts)).error == SpreadsheetError::InvalidXml,
            "DTD and external entity declarations are rejected");
        parts = fixture();
        for (auto& part : parts)
        {
            if (part.path == "_rels/.rels")
            {
                part.bytes = "<Relationships><Relationship Id='entry' Type='x/officeDocument' "
                             "Target='https://example.invalid/book' TargetMode='External'/></Relationships>";
            }
        }
        expect(parse_spreadsheet(std::move(parts)).error == SpreadsheetError::InvalidPackage,
            "external workbook entry is never fetched");
        parts = fixture();
        for (auto& part : parts)
        {
            if (part.path == "xl/workbook.xml")
            {
                part.bytes.append(maximum_spreadsheet_xml_bytes, ' ');
            }
        }
        expect(parse_spreadsheet(std::move(parts)).error == SpreadsheetError::TooLarge,
            "oversized XML members are rejected before parsing");

        parts = fixture();
        std::string cells =
            "<worksheet xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'><sheetData>";
        cells.reserve(5 * 1024 * 1024);
        for (std::size_t index = 1; index <= maximum_spreadsheet_cells + 1; ++index)
        {
            cells += "<row r='" + std::to_string(index) + "'><c r='A" + std::to_string(index) + "'/></row>";
        }
        cells += "</sheetData></worksheet>";
        for (auto& part : parts)
        {
            if (part.path == "xl/worksheets/sheet1.xml")
            {
                part.bytes = std::move(cells);
            }
        }
        expect(parse_spreadsheet(std::move(parts)).error == SpreadsheetError::TooLarge,
            "actual sparse cell count is bounded independently of declared dimensions");

        parts = fixture();
        std::string nested = "<workbook>";
        for (std::size_t index = 0; index < maximum_spreadsheet_xml_depth + 1; ++index)
        {
            nested += "<x>";
        }
        for (std::size_t index = 0; index < maximum_spreadsheet_xml_depth + 1; ++index)
        {
            nested += "</x>";
        }
        nested += "</workbook>";
        for (auto& part : parts)
        {
            if (part.path == "xl/workbook.xml")
            {
                part.bytes = nested;
            }
        }
        expect(parse_spreadsheet(std::move(parts)).error == SpreadsheetError::TooLarge,
            "XML depth is bounded before workbook traversal");
    }
}

int run_spreadsheet_tests()
{
    check_addresses_and_values();
    check_edits_and_serialization();
    check_protection_and_malformed_inputs();
    return failures == 0 ? 0 : 1;
}

int main()
{
    return run_spreadsheet_tests();
}
