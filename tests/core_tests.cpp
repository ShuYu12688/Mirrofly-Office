#include "mirrorfly/core.hpp"

#include <iostream>
#include <string>
#include <vector>

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

    mirrorfly::RecentFile make_file(const std::string& path, const std::string& name, bool starred = false)
    {
        return {path, name, "2026-09-12 14:00", "1 KB", mirrorfly::classify_document(path), starred};
    }

    bool test_classification()
    {
        using mirrorfly::DocumentKind;
        bool passed = true;
        passed = check(mirrorfly::classify_document("REPORT.DOCX") == DocumentKind::Writer,
                     "uppercase word extension") &&
            passed;
        passed = check(mirrorfly::classify_document("C:\\Data\\Budget.XlSx") == DocumentKind::Sheets,
                     "mixed case spreadsheet extension") &&
            passed;
        passed = check(mirrorfly::classify_document("/files/deck.odp") == DocumentKind::Slides,
                     "OpenDocument presentation extension") &&
            passed;
        passed = check(mirrorfly::classify_document("/files.docx/README") == DocumentKind::Other,
                     "directory extension must not classify extensionless file") &&
            passed;
        passed = check(mirrorfly::classify_document("C:\\files.xlsx\\README") == DocumentKind::Other,
                     "Windows directory extension must not classify file") &&
            passed;
        passed = check(mirrorfly::classify_document(u8"/文档/计划.docx") == DocumentKind::Writer,
                     "UTF-8 document path") &&
            passed;
        passed = check(mirrorfly::classify_document("image.png") == DocumentKind::Other,
                     "unsupported extension remains other") &&
            passed;
        passed = check(mirrorfly::classify_document("") == DocumentKind::Other, "empty path remains other") &&
            passed;
        passed = check(mirrorfly::document_kind_key(DocumentKind::Writer) == "writer" &&
                         mirrorfly::document_kind_key(DocumentKind::Sheets) == "sheets" &&
                         mirrorfly::document_kind_key(DocumentKind::Slides) == "slides" &&
                         mirrorfly::document_kind_key(DocumentKind::Other) == "other",
                     "stable public document kind keys") &&
            passed;
        return passed;
    }

    bool test_filtering()
    {
        const std::vector<mirrorfly::RecentFile> files = {
            make_file("/Work/Plan.docx", "Quarterly PLAN", true),
            make_file("/Work/Budget.xlsx", "Annual Budget"),
            make_file(u8"/资料/项目计划.pptx", u8"项目计划", true)};
        bool passed = true;
        passed =
            check(mirrorfly::filter_files(files, "", "all").size() == 3, "all files retain original count") &&
            passed;
        passed = check(mirrorfly::filter_files(files, "plan", "writer").size() == 1,
                     "query and category combine") &&
            passed;
        passed = check(mirrorfly::filter_files(files, "PLAN", "sheets").empty(),
                     "category excludes unmatched kinds") &&
            passed;
        passed = check(mirrorfly::filter_files(files, "/wOrK/", "ALL").size() == 2,
                     "path and category search are ASCII case insensitive") &&
            passed;
        const auto starred = mirrorfly::filter_files(files, "", "starred");
        passed = check(starred.size() == 2 && starred.front().path == files.front().path &&
                         starred.back().path == files.back().path,
                     "starred filter preserves order") &&
            passed;
        const auto unicode = mirrorfly::filter_files(files, u8"项目", "slides");
        passed = check(unicode.size() == 1 && unicode.front().name == u8"项目计划",
                     "UTF-8 query and metadata remain intact") &&
            passed;
        passed = check(mirrorfly::filter_files(files, "", "unknown").empty(),
                     "unknown category does not silently broaden results") &&
            passed;
        passed = check(mirrorfly::filter_files({}, "anything", "all").empty(), "empty source is supported") &&
            passed;
        return passed;
    }

    bool test_remembering()
    {
        const std::vector<mirrorfly::RecentFile> original = {make_file("/a.docx", "A", true),
            make_file("/b.xlsx", "B"), make_file("/a.docx", "Old A"), make_file("/b.xlsx", "Old B", true),
            make_file(u8"/中文.pptx", u8"中文")};
        mirrorfly::RecentFile newest = make_file("/a.docx", "Updated A");
        newest.modified = "2026-09-12 15:20";
        const auto updated = mirrorfly::remember_file(original, newest);
        bool passed = true;
        passed = check(updated.size() == 3 && updated.front().path == "/a.docx" &&
                         updated.front().name == "Updated A" && updated.front().modified == newest.modified,
                     "newest metadata takes priority and duplicate paths are removed") &&
            passed;
        passed = check(updated.size() == 3 && updated[0].starred && updated[1].starred,
                     "deduplication preserves stars for every path") &&
            passed;
        passed = check(updated.size() == 3 && updated.back().name == u8"中文",
                     "remembering preserves UTF-8 metadata") &&
            passed;
        passed = check(original.size() == 5 && original.front().name == "A",
                     "remembering does not mutate input") &&
            passed;
        const auto limited = mirrorfly::remember_file(original, make_file("/new.txt", "New"), 2);
        passed = check(limited.size() == 2 && limited[0].path == "/new.txt" && limited[1].path == "/a.docx",
                     "history limit retains newest entries") &&
            passed;
        passed = check(mirrorfly::remember_file(original, newest, 0).empty(),
                     "zero limit produces empty history") &&
            passed;
        const auto first = mirrorfly::remember_file({}, newest, 1);
        passed = check(first.size() == 1 && first.front().path == newest.path,
                     "empty history accepts first file") &&
            passed;
        return passed;
    }

    bool test_stars()
    {
        const std::vector<mirrorfly::RecentFile> files = {
            make_file("/a.docx", "A", true), make_file(u8"/中文.xlsx", u8"中文")};
        const auto added = mirrorfly::toggle_star(files, u8"/中文.xlsx");
        const auto removed = mirrorfly::toggle_star(added, u8"/中文.xlsx");
        const auto missing = mirrorfly::toggle_star(files, "/missing.docx");
        bool passed = true;
        passed = check(added.size() == 2 && added[0].starred && added[1].starred,
                     "toggle stars matching UTF-8 path only") &&
            passed;
        passed = check(!removed[1].starred && !files[1].starred,
                     "toggle is reversible and leaves input unchanged") &&
            passed;
        passed = check(missing.size() == files.size() && missing[0].starred && !missing[1].starred,
                     "missing path leaves stars unchanged") &&
            passed;
        return passed;
    }

}

int run_core_tests()
{
    bool passed = test_classification();
    passed = test_filtering() && passed;
    passed = test_remembering() && passed;
    passed = test_stars() && passed;

    if (passed)
    {
        std::cout << "Core tests passed.\n";
    }

    return passed ? 0 : 1;
}

int main()
{
    return run_core_tests();
}
