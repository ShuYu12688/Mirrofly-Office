#include <mirrorfly/pdf_storage.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <future>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

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

    bool write_file(const QString& path, const std::string& bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) &&
            file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size());
    }

    std::string read_file(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        const auto bytes = file.readAll();
        return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }

    std::string assemble_pdf(const std::vector<std::string>& objects)
    {
        std::string output = "%PDF-1.4\n%Mirrorfly\n";
        std::vector<std::size_t> offsets(1, 0);
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            offsets.push_back(output.size());
            output += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
        }
        const auto xref = output.size();
        output += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
        output += "0000000000 65535 f \n";
        char offset[24]{};
        for (std::size_t index = 1; index < offsets.size(); ++index)
        {
            std::snprintf(offset, sizeof(offset), "%010zu 00000 n \n", offsets[index]);
            output += offset;
        }
        output += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\n";
        output += "startxref\n" + std::to_string(xref) + "\n%%EOF\n";
        return output;
    }

    std::string pdf_fixture()
    {
        const std::string first_stream = "BT /F1 12 Tf 20 220 Td (Page one vector text) Tj ET\n";
        const std::string second_stream = "BT /F1 12 Tf 20 100 Td (Page two remains vector text) Tj ET\n";
        return assemble_pdf(
            {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] "
                "/Resources << /Font << /F1 4 0 R >> >> /Contents 6 0 R >>",
                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 240 180] "
                "/Resources << /Font << /F1 4 0 R >> >> /Contents 7 0 R >>",
                "<< /Length " + std::to_string(first_stream.size()) + " >>\nstream\n" + first_stream +
                    "endstream",
                "<< /Length " + std::to_string(second_stream.size()) + " >>\nstream\n" + second_stream +
                    "endstream"});
    }

    std::string signed_pdf_fixture()
    {
        const std::string stream = "BT /F1 12 Tf 20 100 Td (Signed source text) Tj ET\n";
        return assemble_pdf(
            {"<< /Type /Catalog /Pages 2 0 R /AcroForm 8 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Annots [9 0 R] "
                "/Resources << /Font << /F1 4 0 R >> >> /Contents 6 0 R >>",
                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>", "<< >>",
                "<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" + stream + "endstream",
                "<< >>", "<< /Fields [9 0 R] /SigFlags 3 >>",
                "<< /Type /Annot /Subtype /Widget /FT /Sig /T (Approval) /Rect [0 0 0 0] "
                "/V 10 0 R /P 3 0 R >>",
                "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached "
                "/ByteRange [0 0 0 0] /Contents <> >>"});
    }

    void round_trip_cases(const QString& directory)
    {
        using namespace mirrorfly;
        const auto source_path = QDir(directory).filePath(QStringLiteral("原始资料.pdf"));
        const auto saved_path = QDir(directory).filePath(QStringLiteral("批注副本.pdf"));
        const auto second_path = QDir(directory).filePath(QStringLiteral("删除批注.pdf"));
        const auto fixture = pdf_fixture();
        check(write_file(source_path, fixture), "create an isolated vector PDF fixture");

        auto loaded = load_pdf_file(source_path.toUtf8().toStdString());
        check(loaded.error == PdfError::None && loaded.revision.size() == 64 && loaded.document.editable &&
                loaded.document.pages.size() == 2,
            "load a bounded PDF into an editable standard C++ model");
        check(loaded.document.pages[0].text.find("Page one vector text") != std::string::npos &&
                loaded.document.pages[1].text.find("Page two remains vector text") != std::string::npos,
            "extract bounded page text without exposing PDFium handles");
        check(loaded.document.source_bytes &&
                std::string(loaded.document.source_bytes->begin(), loaded.document.source_bytes->end()) ==
                    fixture,
            "retain immutable original file bytes in the document model");

        auto rendered = render_pdf_page(loaded.document, loaded.document.pages[0].id, 200, 300);
        check(rendered.error == PdfError::None && rendered.width == 200 && rendered.height == 300 &&
                rendered.rgba.size() == 200 * 300 * 4 &&
                std::any_of(rendered.rgba.begin(), rendered.rgba.end(),
                    [](std::uint8_t value)
        {
            return value != 255;
        }),
            "render a page to a populated RGBA8 buffer");
        check(render_pdf_page(loaded.document, loaded.document.pages[0].id, 4096, 4096).error ==
                PdfError::TooLarge,
            "reject render buffers above the pixel budget");
        std::vector<std::future<PdfRenderResult>> renders;
        for (int index = 0; index < 4; ++index)
        {
            renders.push_back(std::async(std::launch::async, [&loaded, index]()
            {
                return render_pdf_page(loaded.document, loaded.document.pages[index % 2].id, 80, 60);
            }));
        }
        bool concurrent_ok = true;
        for (auto& future : renders)
        {
            const auto result = future.get();
            concurrent_ok =
                concurrent_ok && result.error == PdfError::None && result.rgba.size() == 80 * 60 * 4;
        }
        check(concurrent_ok, "serialize concurrent PDFium work behind the platform boundary");

        const std::string first_id = loaded.document.pages[0].id;
        const std::string second_id = loaded.document.pages[1].id;
        PdfCommand command;
        command.kind = PdfCommandKind::RotatePage;
        command.page_id = first_id;
        check(apply_pdf_command(loaded.document, command).changed, "rotate the source page overlay");
        const auto rotation_only = render_pdf_page(loaded.document, first_id, 300, 200);
        command.kind = PdfCommandKind::AddAnnotation;
        command.annotation = {"", PdfAnnotationKind::Text, {12, 240, 24, 24}, u8"中文备注 🦋", -1};
        const auto note = apply_pdf_command(loaded.document, command);
        check(note.changed && !note.created_annotation_id.empty(), "add a Unicode text annotation overlay");
        command.annotation = {"", PdfAnnotationKind::Highlight, {18, 210, 120, 16}, u8"重点内容", -1};
        check(apply_pdf_command(loaded.document, command).changed, "add a rectangle highlight overlay");
        const auto with_annotations = render_pdf_page(loaded.document, first_id, 300, 200);
        check(rotation_only.error == PdfError::None && with_annotations.error == PdfError::None &&
                rotation_only.rgba != with_annotations.rgba,
            "text notes and highlights produce a visible PDF annotation appearance");
        command.kind = PdfCommandKind::MovePage;
        command.page_id = second_id;
        command.destination_index = 0;
        check(apply_pdf_command(loaded.document, command).changed, "reorder pages before persistence");

        const auto saved = save_pdf_file(saved_path.toUtf8().toStdString(), loaded.document, {});
        check(saved.error == PdfError::None && saved.revision.size() == 64 && QFile::exists(saved_path),
            "atomically save an edited PDF to a new copy");
        check(read_file(source_path) == fixture && loaded.document.source_bytes &&
                std::string(loaded.document.source_bytes->begin(), loaded.document.source_bytes->end()) ==
                    fixture,
            "saving a copy leaves both source disk bytes and immutable model bytes unchanged");

        auto reloaded = load_pdf_file(saved_path.toUtf8().toStdString());
        check(reloaded.error == PdfError::None && reloaded.document.pages.size() == 2 &&
                reloaded.document.pages[0].text.find("Page two remains vector text") != std::string::npos &&
                reloaded.document.pages[1].text.find("Page one vector text") != std::string::npos,
            "reload reordered output with original vector text still extractable");
        check(reloaded.document.pages[1].rotation == 1 &&
                reloaded.document.pages[1].original_annotations.size() == 2 &&
                std::any_of(reloaded.document.pages[1].original_annotations.begin(),
                    reloaded.document.pages[1].original_annotations.end(),
                    [](const PdfAnnotation& annotation)
        {
            return annotation.kind == PdfAnnotationKind::Text && annotation.contents == u8"中文备注 🦋";
        }),
            "reload page rotation and exact Unicode annotation content");
        auto duplicate_ids = reloaded.document;
        duplicate_ids.pages[1].id = duplicate_ids.pages[0].id;
        auto invalid_rect = reloaded.document;
        invalid_rect.pages[1].original_annotations[0].rect.x = (std::numeric_limits<double>::infinity)();
        rendered = render_pdf_page(reloaded.document, reloaded.document.pages[1].id, 300, 200);
        check(rendered.error == PdfError::None && rendered.rgba.size() == 300 * 200 * 4,
            "render the saved rotated and annotated page");

        auto text_annotation = std::find_if(reloaded.document.pages[1].original_annotations.begin(),
            reloaded.document.pages[1].original_annotations.end(), [](const PdfAnnotation& annotation)
        {
            return annotation.kind == PdfAnnotationKind::Text;
        });
        check(text_annotation != reloaded.document.pages[1].original_annotations.end(),
            "find the persisted original text annotation by public metadata");
        if (text_annotation != reloaded.document.pages[1].original_annotations.end())
        {
            command = {};
            command.kind = PdfCommandKind::DeleteAnnotation;
            command.page_id = reloaded.document.pages[1].id;
            command.annotation_id = text_annotation->id;
            check(apply_pdf_command(reloaded.document, command).changed,
                "record deletion of a persisted text annotation");
            command = {};
            command.kind = PdfCommandKind::DeletePage;
            command.page_id = reloaded.document.pages[0].id;
            check(apply_pdf_command(reloaded.document, command).changed,
                "record deletion of a persisted PDF page");
            const auto second = save_pdf_file(second_path.toUtf8().toStdString(), reloaded.document, {});
            const auto without_note = load_pdf_file(second_path.toUtf8().toStdString());
            check(second.error == PdfError::None && without_note.error == PdfError::None &&
                    without_note.document.pages.size() == 1 &&
                    without_note.document.pages[0].original_annotations.size() == 1 &&
                    without_note.document.pages[0].original_annotations[0].kind ==
                        PdfAnnotationKind::Highlight,
                "save page and original-annotation deletions while retaining the highlight");
        }

        const auto invalid_path = QDir(directory).filePath(QStringLiteral("invalid-state.pdf"));
        const auto rejected = save_pdf_file(invalid_path.toUtf8().toStdString(), duplicate_ids, {});
        check(rejected.error == PdfError::InvalidDocument && !QFile::exists(invalid_path),
            "reject duplicate stable page ids without creating a partial destination");
        const auto invalid_rect_path = QDir(directory).filePath(QStringLiteral("invalid-rect.pdf"));
        const auto rejected_rect = save_pdf_file(invalid_rect_path.toUtf8().toStdString(), invalid_rect, {});
        check(rejected_rect.error == PdfError::InvalidDocument && !QFile::exists(invalid_rect_path),
            "reject non-finite public annotation geometry without a partial destination");

        check(write_file(saved_path, "external change"), "replace the output to simulate a disk conflict");
        const auto conflict =
            save_pdf_file(saved_path.toUtf8().toStdString(), reloaded.document, reloaded.revision);
        check(conflict.error == PdfError::ChangedOnDisk && read_file(saved_path) == "external change",
            "disk revision conflict preserves external bytes and in-memory edits");
    }

    void rejection_cases(const QString& directory)
    {
        using namespace mirrorfly;
        const auto invalid_path = QDir(directory).filePath(QStringLiteral("broken.pdf"));
        check(write_file(invalid_path, "%PDF-broken"), "create invalid PDF fixture");
        check(load_pdf_file(invalid_path.toUtf8().toStdString()).error == PdfError::InvalidDocument,
            "reject a damaged PDF without replacing any active model");
        check(load_pdf_file(QDir(directory).filePath("wrong.txt").toStdString()).error ==
                PdfError::UnsupportedType,
            "reject non-PDF extensions before reading");

        const auto signed_path = QDir(directory).filePath(QStringLiteral("signed.pdf"));
        check(write_file(signed_path, signed_pdf_fixture()), "create a signed PDF fixture");
        auto signed_document = load_pdf_file(signed_path.toUtf8().toStdString());
        check(signed_document.error == PdfError::None && !signed_document.document.editable,
            "load a signed PDF for reading while marking it read-only");
        signed_document.document.editable = true;
        PdfCommand command;
        command.kind = PdfCommandKind::RotatePage;
        command.page_id = signed_document.document.pages[0].id;
        check(apply_pdf_command(signed_document.document, command).changed,
            "simulate a caller tampering with the public editable flag");
        const auto signed_copy = QDir(directory).filePath(QStringLiteral("signed-tamper-copy.pdf"));
        const auto signed_rejected =
            save_pdf_file(signed_copy.toUtf8().toStdString(), signed_document.document, {});
        check(signed_rejected.error == PdfError::SignedDocument && !QFile::exists(signed_copy),
            "recheck source signatures at save time so public state cannot bypass protection");

        const auto oversized_path = QDir(directory).filePath(QStringLiteral("oversized.pdf"));
        QFile oversized(oversized_path);
        check(oversized.open(QIODevice::WriteOnly) &&
                oversized.resize(static_cast<qint64>(maximum_pdf_bytes) + 1),
            "create a sparse oversized PDF fixture");
        oversized.close();
        check(load_pdf_file(oversized_path.toUtf8().toStdString()).error == PdfError::TooLarge,
            "reject source files above the byte budget");
    }
}

int run_pdf_storage_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "create isolated PDF test directory");
    if (directory.isValid())
    {
        round_trip_cases(directory.path());
        rejection_cases(directory.path());
    }
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_pdf_storage_tests(argc, argv);
}
