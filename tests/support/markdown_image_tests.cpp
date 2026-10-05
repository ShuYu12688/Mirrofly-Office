#include "markdown_image_tests.hpp"
#include "editor_tools.hpp"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <iostream>
#include <mirrorfly/markdown.hpp>

namespace
{
    bool check(bool valid, const char* message)
    {
        if (!valid)
            std::cerr << "FAIL: " << message << '\n';
        return valid;
    }
    QString apply_source(const QString& source, const mirrorfly::MarkdownEdit& edit)
    {
        auto bytes = source.toUtf8().toStdString();
        bytes.replace(edit.start, edit.end - edit.start, edit.replacement);
        return QString::fromStdString(bytes);
    }
}

bool test_markdown_images(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme)
{
    const QString image = "![猫 🦋 & text](pic.png \"a title\")";
    const QStringList cases{image + '\n', "before " + image + " after\n", "**" + image + "**\n",
        "*" + image + "*\n", "~~" + image + "~~\n", "## " + image + '\n', "> " + image + '\n',
        "- " + image + "\n- tail\n", "- > " + image + "\n- tail\n", "> 3) " + image + "\n> 4) tail\n",
        "- owner\n\n  " + image + "\n\n- tail\n", "[" + image + "](../page \"outer title\")\n",
        image + image + '\n', "![ref][p]\n\n[p]: ../shared.png \"shared\"\n",
        "![a *b* ![c](nested.png)](outer.png)\n", "![a [b](x) `c`](pic.png)\n", "![]()\n",
        "| photo |\n| :---: |\n| " + image + " |\n", "| [" + image + "](page) |\n| --- |\n| tail |\n",
        "- [x] " + image + "\n- [ ] tail\n"};
    bool passed = true;
    QJsonArray corpus;
    for (const auto& source : cases)
    {
        const auto images = mirrorfly::markdown_images(source.toUtf8().toStdString());
        if (!check(!images.empty(), "public image metadata resolves fixture"))
            return false;
        const auto loaded = tools.loadDocument(wrapper, source, true, theme);
        if (!check(loaded.value("valid").toBool(), "image/container fixture imports"))
        {
            std::cerr << source.toStdString() << loaded.value("error").toString().toStdString() << '\n';
            return false;
        }
        auto* document = wrapper->textDocument();
        const auto original = document->toRawText();
        const int position = original.indexOf(QChar::ObjectReplacementCharacter);
        if (!check(position >= 0, "images are real inline objects"))
            return false;
        auto state = tools.inspectDocument(wrapper, position);
        passed = check(state.value("inImage").toBool() &&
                         state.value("imageAlt").toString() == QString::fromStdString(images[0].alt),
                     "image context uses decoded alternative text") &&
            passed;
        const QVariantMap same{{"url", QString::fromStdString(images[0].url)},
            {"title", QString::fromStdString(images[0].title)}};
        passed = check(tools.applyEdit(wrapper, position, position, "image", same).value("valid").toBool(),
                     "updating an image preserves alt when omitted") &&
            passed;
        const auto saved = tools.sourceText(wrapper);
        if (!check(tools.canSave(), "images serialize with text styles and ownership"))
            return false;
        document->undo();
        passed = check(document->toRawText() == original && tools.sourceText(wrapper) == source,
                     "image update undoes in one transaction") &&
            passed;
        document->redo();
        passed = check(tools.sourceText(wrapper) == saved, "image redo preserves source") && passed;
        passed = check(tools.loadDocument(wrapper, saved, true, theme).value("valid").toBool() &&
                         wrapper->textDocument()->toRawText() == original,
                     "images save and reopen with the same objects") &&
            passed;
        document = wrapper->textDocument();
        const QVariantMap options{
            {"url", "../new image.png"}, {"alt", "new [alt] & 🦋"}, {"title", "new title"}};
        passed = check(tools.applyEdit(wrapper, position, position, "image", options).value("valid").toBool(),
                     "visual image replacement accepts escaped Unicode metadata") &&
            passed;
        const auto updated = tools.sourceText(wrapper);
        mirrorfly::MarkdownOptions settings;
        settings.url = "../new image.png";
        settings.image_alt = u8"new [alt] & 🦋";
        settings.image_alt_set = true;
        settings.title = "new title";
        const auto transaction = mirrorfly::make_markdown_edit(
            source.toUtf8().toStdString(), images[0].start, images[0].end, "image", settings);
        if (!check(transaction.valid && tools.canSave(), "source and visual image edits are supported"))
            return false;
        const auto expected = apply_source(source, transaction);
        const auto changed_raw = document->toRawText();
        passed = check(tools.loadDocument(wrapper, updated, true, theme).value("valid").toBool() &&
                         wrapper->textDocument()->toRawText() == changed_raw,
                     "edited image source reopens with identical object layout") &&
            passed;
        corpus.append(QJsonObject{
            {"original", source}, {"source", saved}, {"updated", updated}, {"expected", expected}});
        passed =
            check(tools.applyEdit(wrapper, position, position + 1, "removeImage").value("valid").toBool() &&
                    tools.sourceText(wrapper).contains("alt") && tools.canSave(),
                "removal retains alternative text") &&
            passed;
    }
    QTemporaryDir directory;
    QImage pixels(12, 8, QImage::Format_ARGB32);
    pixels.fill(Qt::red);
    passed =
        check(pixels.save(directory.filePath("sample.png")), "create an isolated local raster fixture") &&
        passed;
    passed =
        check(tools.loadDocument(wrapper, "![red](sample.png)", true, theme, directory.filePath("note.md"))
                  .value("valid")
                  .toBool(),
            "resolve relative image against document path") &&
        passed;
    auto* document = wrapper->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(1, QTextCursor::KeepAnchor);
    const auto format = cursor.charFormat().toImageFormat();
    const auto resource =
        document->resource(QTextDocument::ImageResource, QUrl(format.name())).value<QImage>();
    passed = check(resource.size() == pixels.size() && resource.pixelColor(0, 0) == QColor(Qt::red) &&
                     format.width() == 12 && format.height() == 8,
                 "bounded reader returns real local pixels and aspect ratio") &&
        passed;
    passed = check(!tools.applyEdit(wrapper, 0, 1, "inlineCode").value("valid").toBool(),
                 "image objects cannot silently become inline code") &&
        passed;
    const auto resource_count = document->property("markdownImageResources").toMap().size();
    for (int index = 0; index < 10; ++index)
        tools.applyEdit(wrapper, 0, 1, "image", {{"url", "sample.png"}, {"alt", QString::number(index)}});
    passed = check(document->property("markdownImageResources").toMap().size() == resource_count,
                 "repeated edits share resource pixels within the document") &&
        passed;
    for (int index = 0; index < 270; ++index)
        tools.applyEdit(wrapper, 0, 1, "image",
            {{"url", "https://example.invalid/" + QString::number(index)}, {"alt", QString::number(index)}});
    passed =
        check(document->property("markdownImageResources").toMap().size() <= 257 &&
                document->property("markdownImageBytes").toLongLong() < 64 * 1024 * 1024 && tools.canSave(),
            "repeated image replacement bounds retained resources without losing source metadata") &&
        passed;
    QImage large_pixels(2000, 2000, QImage::Format_ARGB32);
    large_pixels.fill(Qt::blue);
    passed =
        check(large_pixels.save(directory.filePath("large.png")), "create bounded large-raster fixture") &&
        passed;
    auto large_theme = theme;
    large_theme.insert("markdownImagePlaceholderWidth", 640);
    large_theme.insert("markdownImagePlaceholderHeight", 120);
    tools.loadDocument(wrapper, "![large](large.png)", true, large_theme, directory.filePath("note.md"));
    document = wrapper->textDocument();
    for (int index = 0; index < 20; ++index)
    {
        const auto filename = "large-" + QString::number(index) + ".png";
        passed = check(QFile::copy(directory.filePath("large.png"), directory.filePath(filename)),
                     "copy unique local resource fixture") &&
            passed;
        tools.applyEdit(wrapper, 0, 1, "image", {{"url", filename}, {"alt", QString::number(index)}});
    }
    const auto large_bytes = document->property("markdownImageBytes").toLongLong();
    passed = check(large_bytes >= 48 * 1024 * 1024 && large_bytes <= 64 * 1024 * 1024 &&
                     document->property("markdownImageResources").toMap().contains("budget-placeholder") &&
                     tools.canSave(),
                 "decoded pixel budget includes maximum placeholder size and stays bounded across history") &&
        passed;
    const auto output = qEnvironmentVariable("MIRRORFLY_MARKDOWN_LINK_OUTPUT_DIRECTORY");
    if (!output.isEmpty())
    {
        QDir().mkpath(output);
        QFile file(QDir(output).filePath("images.json"));
        passed = check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(corpus).toJson()) > 0,
                     "export actual image edits for independent CommonMark comparison") &&
            passed;
    }
    return passed;
}
