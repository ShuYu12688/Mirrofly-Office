#include <mirrorfly/pdf.hpp>

#include <iostream>
#include <memory>

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

    mirrorfly::PdfDocument document()
    {
        using namespace mirrorfly;
        PdfDocument value;
        value.source_bytes =
            std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{'%', 'P', 'D', 'F'});
        value.source_page_count = 2;
        value.editable = true;
        PdfPage first;
        first.id = "page-1";
        first.source_index = 0;
        first.width_points = 200;
        first.height_points = 300;
        first.source_annotation_count = 1;
        first.original_annotations.push_back(
            {"page-1-annotation-1", PdfAnnotationKind::Highlight, {10, 20, 80, 12}, "原批注", 0});
        PdfPage second;
        second.id = "page-2";
        second.source_index = 1;
        second.width_points = 400;
        second.height_points = 250;
        value.pages = {first, second};
        return value;
    }

    void path_and_geometry_cases()
    {
        using namespace mirrorfly;
        check(is_pdf_path("研究报告.PDF") && is_pdf_path("C:/资料/a.pdf") && !is_pdf_path("a.pdf.tmp") &&
                !is_pdf_path("folder.pdf/file"),
            "PDF extension matching is case insensitive and limited to the final path component");
        auto value = document();
        check(pdf_page_display_width(value.pages[0]) == 200 && pdf_page_display_height(value.pages[0]) == 300,
            "unrotated page uses its source dimensions");
        value.pages[0].rotation = 1;
        check(pdf_page_display_width(value.pages[0]) == 300 && pdf_page_display_height(value.pages[0]) == 200,
            "quarter-turn swaps display dimensions without changing PDF coordinates");
    }

    void command_cases()
    {
        using namespace mirrorfly;
        auto value = document();
        PdfCommand command;
        command.kind = PdfCommandKind::RotatePage;
        command.page_id = "page-1";
        command.clockwise_quarter_turns = -1;
        auto result = apply_pdf_command(value, command);
        check(result.error == PdfError::None && result.changed && value.pages[0].rotation == 3,
            "rotation normalizes negative quarter turns");

        command.kind = PdfCommandKind::MovePage;
        command.page_id = "page-1";
        command.destination_index = 1;
        result = apply_pdf_command(value, command);
        check(result.changed && value.pages[0].id == "page-2" && value.pages[1].id == "page-1",
            "page moves use stable ids and a final destination index");

        command.kind = PdfCommandKind::AddAnnotation;
        command.page_id = "page-1";
        command.annotation = {"ignored", PdfAnnotationKind::Text, {25, 40, 24, 24}, u8"中文批注 🦋", 99};
        result = apply_pdf_command(value, command);
        check(result.changed && result.created_annotation_id == "added-1" &&
                value.pages[1].added_annotations[0].contents == u8"中文批注 🦋" &&
                value.pages[1].added_annotations[0].source_index == -1,
            "new annotations receive stable local ids and preserve Unicode content");

        const auto before = value;
        command.annotation.rect = {-1, 0, 10, 10};
        result = apply_pdf_command(value, command);
        check(result.error == PdfError::InvalidAnnotation &&
                value.pages[1].added_annotations.size() == before.pages[1].added_annotations.size() &&
                value.next_annotation_id == before.next_annotation_id,
            "invalid annotation commands leave the whole model unchanged");

        command.kind = PdfCommandKind::DeleteAnnotation;
        command.annotation_id = "added-1";
        result = apply_pdf_command(value, command);
        check(result.changed && value.pages[1].added_annotations.empty(),
            "deleting a newly added annotation removes its overlay");
        command.annotation_id = "page-1-annotation-1";
        result = apply_pdf_command(value, command);
        check(result.changed && value.pages[1].removed_annotation_ids.count(command.annotation_id) == 1,
            "deleting an original annotation records a stable removal overlay");

        command.kind = PdfCommandKind::DeletePage;
        command.page_id = "page-2";
        result = apply_pdf_command(value, command);
        check(result.changed && value.pages.size() == 1 && value.pages[0].id == "page-1",
            "page deletion preserves the remaining stable page");
        result = apply_pdf_command(value, command);
        check(result.error == PdfError::InvalidPage && value.pages.size() == 1,
            "a missing page cannot affect the model");
        command.page_id = "page-1";
        result = apply_pdf_command(value, command);
        check(result.error == PdfError::InvalidPage && value.pages.size() == 1,
            "the last PDF page cannot be removed");

        auto read_only = document();
        read_only.editable = false;
        read_only.read_only_reason = "权限限制";
        command.kind = PdfCommandKind::RotatePage;
        const auto original_rotation = read_only.pages[0].rotation;
        result = apply_pdf_command(read_only, command);
        check(result.error == PdfError::ReadOnly && result.message == "权限限制" &&
                read_only.pages[0].rotation == original_rotation,
            "read-only documents reject commands without partial mutation");
    }
}

int run_pdf_tests()
{
    path_and_geometry_cases();
    command_cases();
    return failures ? 1 : 0;
}

int main()
{
    return run_pdf_tests();
}
