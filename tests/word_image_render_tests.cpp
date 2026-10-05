#include "word_document.hpp"
#include "word_image_resources.hpp"
#include "word_viewport.hpp"
#include <mirrorfly/word_storage.hpp>

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextTable>
#include <QThread>

#include <d3d11.h>
#include <wrl/client.h>

#include <iostream>
#include <memory>
#include <set>

namespace
{
    constexpr int width = 960;
    constexpr int height = 720;
    int failures = 0;

    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << message << '\n';
        }
        return condition;
    }

    bool same_pixels(const QImage& left, const QImage& right)
    {
        if (left.size() != right.size() || left.format() != right.format())
            return false;
        // GPU glyph atlas resampling can round an 8-bit channel by one between scroll positions.
        for (int row = 0; row < left.height(); ++row)
            for (int byte = 0; byte < left.width() * 4; ++byte)
                if (std::abs(int(left.constScanLine(row)[byte]) - int(right.constScanLine(row)[byte])) > 1)
                    return false;
        return true;
    }

    struct Scene
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture, readback;
        QQuickRenderControl control;
        QQuickWindow window{&control};

        ~Scene()
        {
            control.invalidate();
        }

        bool initialize()
        {
            if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(),
                    nullptr, context.GetAddressOf())))
                return false;
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())))
                return false;
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device->CreateTexture2D(&description, nullptr, readback.GetAddressOf())))
                return false;
            window.setGeometry(0, 0, width, height);
            window.setColor(Qt::white);
            window.contentItem()->setSize(QSizeF(width, height));
            window.contentItem()->setClip(true);
            window.setGraphicsDevice(QQuickGraphicsDevice::fromDeviceAndContext(device.Get(), context.Get()));
            if (!control.initialize())
                return false;
            window.setRenderTarget(QQuickRenderTarget::fromD3D11Texture(texture.Get(), QSize(width, height)));
            return true;
        }

        QImage frame()
        {
            control.polishItems();
            control.beginFrame();
            control.sync();
            control.render();
            control.endFrame();
            context->CopyResource(readback.Get(), texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
                return {};
            const QImage result = QImage(static_cast<const uchar*>(mapped.pData), width, height,
                mapped.RowPitch, QImage::Format_RGBA8888)
                                      .copy();
            context->Unmap(readback.Get(), 0);
            return result;
        }

        int frame_pixels(const QColor& target)
        {
            const auto image = frame();
            if (image.isNull())
                return -1;
            int pixels = 0;
            for (int row = 0; row < height; ++row)
            {
                const auto* line = image.constScanLine(row);
                for (int column = 0; column < width; ++column)
                {
                    const auto* pixel = line + column * 4;
                    if (pixel[0] == target.red() && pixel[1] == target.green() && pixel[2] == target.blue())
                        ++pixels;
                }
            }
            return pixels;
        }
    };

    std::unique_ptr<QTextDocument> image_document(const QColor& color, int count = 1)
    {
        QImage pixels(64, 32, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(color);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        pixels.save(&buffer, "PNG");
        mirrorfly::WordDocument source;
        mirrorfly::WordImage image;
        image.id = 1;
        image.width = 96;
        image.height = 48;
        image.mime_type = "image/png";
        image.bytes = std::make_shared<const std::string>(bytes.constData(), bytes.size());
        source.paragraphs.clear();
        for (int index = 0; index < count; ++index)
        {
            auto occurrence = image;
            occurrence.id = index + 1;
            occurrence.bytes = std::make_shared<const std::string>(*image.bytes);
            source.images.push_back(occurrence);
            source.paragraphs.push_back({});
            source.paragraphs.back().runs = {{""}};
            source.paragraphs.back().runs[0].image_id = occurrence.id;
        }
        source.paragraphs.push_back({});
        source.paragraphs.back().runs = {{"Editable text"}};
        return mirrorfly::create_word_document(source, width, true);
    }

    void viewport_cases(Scene& scene)
    {
        auto document = image_document(Qt::red, 100);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; Flickable { width: 960; height: 720; clip: true; "
                          "contentWidth: width; contentHeight: editor.contentHeight; "
                          "TextEdit { id: editor; objectName: 'viewportEditor'; width: 960; "
                          "height: contentHeight; textFormat: TextEdit.RichText; wrapMode: TextEdit.Wrap } }",
            QUrl{});
        std::unique_ptr<QObject> object(component.create());
        auto* viewport = qobject_cast<QQuickItem*>(object.get());
        if (!check(viewport != nullptr, "create an isolated scrolling image viewport"))
            return;
        viewport->setParentItem(scene.window.contentItem());
        auto* text = object->findChild<QQuickItem*>(QStringLiteral("viewportEditor"));
        auto* wrapper = text->property("textDocument").value<QQuickTextDocument*>();
        text->setFlag(QQuickItem::ItemObservesViewport, true);
        wrapper->setTextDocument(document.get());
        auto* resources = document->findChild<mirrorfly::WordImageResources*>();
        std::set<qulonglong> completed;
        QObject::connect(resources, &mirrorfly::WordImageResources::imageReady, viewport,
            [&](const QUrl& name)
        {
            completed.insert(name.path().mid(1).toULongLong());
        });
        for (const qulonglong target : {1, 100})
        {
            const auto top = target == 1 ? 0 : document->documentLayout()->documentSize().height() - height;
            viewport->setProperty("contentY", top);
            QElapsedTimer timer;
            timer.start();
            int settled = 0;
            while ((completed.count(target) == 0 || settled < 2) && timer.elapsed() < 5000)
            {
                QCoreApplication::processEvents();
                scene.frame();
                settled = resources->pending() ? 0 : settled + 1;
                QThread::msleep(1);
            }
            check(completed.count(target) != 0 && settled >= 2,
                "scrolling to a new viewport loads its images without starvation");
            check(scene.frame_pixels(Qt::red) > 1000, "visible asynchronous images reach the scene graph");
        }
        check(
            completed.size() < 32, "visiting first and last viewports never decodes all 100 document images");
        check(!document->isModified() && !document->isUndoAvailable(),
            "viewport changes and resource completions are not document edits");
        scene.control.invalidate();
    }

    void cases(Scene& scene)
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { width: 480; height: 320; "
                          "textFormat: TextEdit.RichText; wrapMode: TextEdit.Wrap }",
            QUrl{});
        std::unique_ptr<QObject> object(component.create());
        auto* item = qobject_cast<QQuickItem*>(object.get());
        if (!check(item != nullptr, "create an isolated Quick text editor"))
            return;
        item->setParentItem(scene.window.contentItem());
        auto* wrapper = object->property("textDocument").value<QQuickTextDocument*>();
        if (!check(wrapper != nullptr, "obtain the public Quick document wrapper"))
            return;
        for (const auto color : {QColor(Qt::red), QColor(Qt::blue)})
        {
            auto document = image_document(color);
            const QPointer<QTextDocument> previous = wrapper->textDocument();
            document->setParent(wrapper);
            auto* current = document.release();
            wrapper->setTextDocument(current);
            if (previous && previous->parent() == wrapper)
                delete previous.data();
            check(scene.frame_pixels(color) == 0, "first frame contains a non-blocking image placeholder");
            QElapsedTimer timer;
            timer.start();
            int pixels = 0;
            while (pixels < 1000 && timer.elapsed() < 5000)
            {
                QCoreApplication::processEvents();
                pixels = scene.frame_pixels(color);
                QThread::msleep(1);
            }
            check(pixels >= 1000, "image completion replaces the placeholder in actual D3D11 scene nodes");
            check(!current->isModified() && !current->isUndoAvailable(),
                "Quick repaint does not create an edit or undo transaction");
            QTextCursor cursor(current);
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(" changed");
            QCoreApplication::processEvents();
            check(scene.frame_pixels(color) >= 1000, "typing leaves loaded image pixels visible");
            current->undo();
            QCoreApplication::processEvents();
            check(scene.frame_pixels(color) >= 1000, "undo preserves loaded image pixels");
            current->redo();
            QCoreApplication::processEvents();
            check(scene.frame_pixels(color) >= 1000, "redo preserves loaded image pixels");
            if (color == QColor(Qt::blue))
                check(scene.frame_pixels(Qt::red) == 0, "switching documents cannot reuse old image pixels");
        }
        check(!scene.window.isVisible(), "the render regression never shows an application window");
        scene.control.invalidate();
    }

    void retained_cases(Scene& scene)
    {
        auto document = image_document(Qt::red, 100);
        QTextCursor cursor(document.get());
        cursor.movePosition(QTextCursor::End);
        auto* table = cursor.insertTable(20, 3);
        for (int row = 0; row < table->rows(); ++row)
            for (int column = 0; column < table->columns(); ++column)
                table->cellAt(row, column)
                    .firstCursorPosition()
                    .insertText(QStringLiteral("Table %1/%2: retained text and images").arg(row).arg(column));
        document->clearUndoRedoStacks();
        document->setModified(false);
        check(mirrorfly::prepare_word_document_images(*document), "prepare scroll regression images");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; import QtQuick.Controls; Flickable { id: view; "
                          "width: 960; height: 720; clip: true; property real zoom: 1; "
                          "contentHeight: paper.height * zoom + 32; Rectangle { id: paper; "
                          "x: 20; y: 12; width: 820; height: editor.contentHeight + 96; "
                          "scale: view.zoom; transformOrigin: Item.TopLeft; color: 'white'; "
                          "TextArea { id: editor; objectName: 'retainedEditor'; "
                          "x: 48; y: 40; width: 724; height: parent.height - 80; padding: 0; "
                          "readOnly: false; opacity: 0; background: null; textFormat: TextEdit.RichText; "
                          "wrapMode: TextEdit.Wrap } } }",
            QUrl{});
        std::unique_ptr<QObject> object(component.create());
        auto* viewport = qobject_cast<QQuickItem*>(object.get());
        if (!check(viewport != nullptr, "create the nested Word paper regression"))
            return;
        viewport->setParentItem(scene.window.contentItem());
        auto* text = object->findChild<QQuickItem*>(QStringLiteral("retainedEditor"));
        text->setFlag(QQuickItem::ItemObservesViewport, false);
        text->setFlag(QQuickItem::ItemHasContents, false);
        auto* wrapper = text->property("textDocument").value<QQuickTextDocument*>();
        wrapper->setTextDocument(document.get());
        auto* surface = new mirrorfly::WordViewport(text->parentItem());
        surface->setTextDocument(wrapper);
        const auto position_surface = [&]()
        {
            const auto zoom = viewport->property("zoom").toReal();
            const auto top = std::max<qreal>(0.0, (viewport->property("contentY").toReal() - 12) / zoom - 40);
            surface->setPosition(QPointF(48, 40 + top));
            surface->setSize(QSizeF(724, height / zoom));
            surface->setDocumentTop(top);
            surface->setRenderScale(zoom);
        };
        int changes = 0;
        QObject::connect(document.get(), &QTextDocument::contentsChanged, viewport, [&]()
        {
            ++changes;
        });
        for (const auto zoom : {1.0, 1.5, 0.75})
        {
            viewport->setProperty("zoom", zoom);
            QCoreApplication::processEvents();
            position_surface();
            scene.frame();
            const auto bottom = viewport->property("contentHeight").toReal() - height;
            std::vector<std::pair<qreal, QImage>> baselines;
            for (const auto fraction : {0.0, 0.15, 0.5, 0.9, 1.0})
            {
                viewport->setProperty("contentY", bottom * fraction);
                QCoreApplication::processEvents();
                position_surface();
                scene.frame();
                baselines.emplace_back(bottom * fraction, scene.frame());
            }
            for (int pass = 0; pass < 3; ++pass)
                for (auto it = baselines.rbegin(); it != baselines.rend(); ++it)
                {
                    viewport->setProperty("contentY", it->first);
                    QCoreApplication::processEvents();
                    position_surface();
                    const auto image = scene.frame();
                    check(image == it->second, "backward scrolling preserves text, images and table pixels");
                    check(scene.frame() == image, "retained text remains stable on consecutive frames");
                }
        }
        check(changes == 0 && !document->isModified() && !document->isUndoAvailable(),
            "scrolling cached Word content causes no document refresh or edit");
        check(!document->findChild<mirrorfly::WordImageResources*>()->pending(),
            "prepared scrolling has no late image requests");
        viewport->setProperty("zoom", 1.0);
        viewport->setProperty("contentY", 0.0);
        text->setProperty("cursorPosition", 0);
        text->forceActiveFocus();
        position_surface();
        QCoreApplication::processEvents();
        const auto initial = scene.frame();
        QInputMethodEvent preedit(QStringLiteral("输入预编辑"), {});
        QCoreApplication::sendEvent(text, &preedit);
        QCoreApplication::processEvents();
        check(scene.frame() != initial, "IME preedit is visible without native text scene nodes");
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("输入预编辑"));
        QCoreApplication::sendEvent(text, &commit);
        QCoreApplication::processEvents();
        check(document->toPlainText().startsWith(QStringLiteral("输入预编辑")),
            "the original input control still commits Chinese composition");
        document->undo();
        QCoreApplication::processEvents();
        check(same_pixels(initial, scene.frame()), "undo after IME composition restores visible pixels");
        scene.control.invalidate();
    }

    void cell_cases(Scene& scene)
    {
        using namespace mirrorfly;
        WordDocument source;
        source.paragraphs[0].runs = {{"Cell text"}};
        WordTable table;
        table.rows = 2;
        table.column_widths = {200, 200};
        table.border_width = 3;
        table.border_color = "#663399";
        WordTableCell cell;
        cell.row_span = 2;
        cell.background = "#ccddee";
        cell.vertical_alignment = 1;
        cell.margins = {18.5, 12.25, 9.75, 24};
        cell.blocks = {{WordBlock::Kind::Paragraph, 0}};
        table.cells.push_back(cell);
        source.tables.push_back(table);
        source.blocks = {{WordBlock::Kind::Table, 0}};
        auto document = create_word_document(source, 600);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { width: 600; height: contentHeight; opacity: 0; "
                          "textFormat: TextEdit.RichText; wrapMode: TextEdit.Wrap }",
            QUrl{});
        std::unique_ptr<QObject> object(component.create());
        auto* text = qobject_cast<QQuickItem*>(object.get());
        if (!check(text != nullptr, "create the cell decoration regression"))
            return;
        text->setParentItem(scene.window.contentItem());
        text->setFlag(QQuickItem::ItemHasContents, false);
        auto* wrapper = text->property("textDocument").value<QQuickTextDocument*>();
        wrapper->setTextDocument(document.get());
        mirrorfly::WordViewport surface(scene.window.contentItem());
        surface.setSize(QSizeF(600, 400));
        surface.setTextDocument(wrapper);
        QCoreApplication::processEvents();
        check(scene.frame_pixels(QColor("#ccddee")) > 10000,
            "merged cell background survives model import and appears in the visible raster");
        check(scene.frame_pixels(QColor("#663399")) > 1000,
            "collapsed table borders are present in native viewport pixels");
        const auto baseline = scene.frame();
        auto* actual = qobject_cast<QTextTable*>(document->rootFrame()->childFrames().front());
        const auto position = actual->cellAt(0, 0).firstCursorPosition().position();
        check(format_word_document(*document, position, position, "cellFill", "#22aa66"),
            "cell background editing reaches the visible document");
        QCoreApplication::processEvents();
        check(scene.frame_pixels(QColor("#22aa66")) > 10000 && scene.frame_pixels(QColor("#ccddee")) == 0,
            "editing a cell refreshes its pixels without leaving the old fill");
        document->undo();
        QCoreApplication::processEvents();
        check(same_pixels(baseline, scene.frame()), "cell fill undo restores identical visible pixels");
        surface.setDocumentTop(30);
        scene.frame();
        surface.setDocumentTop(0);
        check(same_pixels(baseline, scene.frame()), "scrolling back restores cell borders and shading");
        scene.control.invalidate();
    }

    void partial_repaint_cases(Scene& scene)
    {
        auto document = std::make_unique<QTextDocument>();
        QTextCursor seed(document.get());
        for (int index = 0; index < 40; ++index)
        {
            seed.insertText(QStringLiteral("段落 %1 · Paragraph keeps local editing responsive.").arg(index));
            seed.insertBlock();
        }
        document->setTextWidth(600);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { width: 600; height: contentHeight; opacity: 0; "
                          "textFormat: TextEdit.RichText; wrapMode: TextEdit.Wrap }",
            QUrl{});
        std::unique_ptr<QObject> object(component.create());
        auto* text = qobject_cast<QQuickItem*>(object.get());
        if (!check(text != nullptr, "create the partial Word repaint regression"))
            return;
        text->setParentItem(scene.window.contentItem());
        text->setFlag(QQuickItem::ItemHasContents, false);
        auto* wrapper = text->property("textDocument").value<QQuickTextDocument*>();
        wrapper->setTextDocument(document.get());
        mirrorfly::WordViewport surface(scene.window.contentItem());
        surface.setSize(QSizeF(600, 400));
        surface.setTextDocument(wrapper);
        QCoreApplication::processEvents();
        scene.frame();
        check(surface.lastPaintedDocumentArea() == QRectF(0, 0, 600, 400),
            "initial Word raster covers the complete visible viewport");

        surface.setDocumentTop(80);
        QCoreApplication::processEvents();
        scene.frame();
        const auto scrolled = surface.lastPaintedDocumentArea();
        check(scrolled.isValid() && scrolled.height() > 0 && scrolled.height() < surface.height() / 2 &&
                scrolled.bottom() >= 478,
            "Word scrolling shifts retained pixels and paints only the newly exposed strip");
        surface.setDocumentTop(0);
        QCoreApplication::processEvents();
        scene.frame();

        const auto block = document->findBlockByNumber(3);
        QTextCursor edit(block);
        edit.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 9);
        QTextCharFormat format;
        format.setForeground(QColor("#B02020"));
        edit.mergeCharFormat(format);
        QCoreApplication::processEvents();
        scene.frame();
        const auto painted = surface.lastPaintedDocumentArea();
        check(painted.isValid() && painted.height() > 0 && painted.height() < surface.height() / 2,
            "formatting one paragraph repaints a local document region instead of the full viewport");
        for (const qreal scale : {1.0, 1.25, 1.5, 2.0})
        {
            surface.setRenderScale(scale);
            surface.setDocumentTop(0);
            scene.frame();
            for (int pass = 0; pass < 8; ++pass)
            {
                QTextCursor change(block);
                change.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 8);
                QTextCharFormat updated;
                updated.setForeground(pass % 2 ? QColor("#203040") : QColor("#B02020"));
                change.mergeCharFormat(updated);
                QCoreApplication::processEvents();
                scene.frame();
                const auto area = document->documentLayout()->blockBoundingRect(block);
                // Layout invalidations may have fractional edges through a glyph's ink.
                emit document->documentLayout()->update(
                    QRectF(0, area.top() + 7.13 + pass * 0.37, 600, 0.63));
                const auto partial = scene.frame();
                surface.setRenderScale(scale + 0.001);
                surface.setRenderScale(scale);
                check(same_pixels(partial, scene.frame()),
                    "fractional dirty strips must match a complete render without white glyph gaps");
                surface.setDocumentTop(surface.documentTop() + 0.37);
                const auto scrolled_frame = scene.frame();
                surface.setRenderScale(scale + 0.001);
                surface.setRenderScale(scale);
                check(same_pixels(scrolled_frame, scene.frame()),
                    "fractional scrolling must keep painted text aligned with document coordinates");
            }
        }
        scene.control.invalidate();
    }

    int corpus(Scene& scene, const QString& path, const QString& output, bool asynchronous, bool prepared,
        bool retained)
    {
        const auto source = mirrorfly::load_word_file(path.toStdString());
        if (!check(source.success, "load the independent Word corpus"))
            return 1;
        const auto fonts = QFontDatabase::families().size();
        if (!check(fonts > 0, "the corpus render must have access to system fonts"))
            return 1;
        auto document = mirrorfly::create_word_document(source.document, width, asynchronous);
        if (prepared)
            check(mirrorfly::prepare_word_document_images(*document), "prepare every corpus preview");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; Flickable { width: 960; height: 720; clip: true; "
                          "contentWidth: width; contentHeight: editor.contentHeight; "
                          "TextEdit { id: editor; objectName: 'corpusEditor'; width: 960; "
                          "height: contentHeight; readOnly: true; textFormat: TextEdit.RichText; "
                          "wrapMode: TextEdit.Wrap } }",
            QUrl{});
        std::unique_ptr<QObject> object(component.create());
        auto* item = qobject_cast<QQuickItem*>(object.get());
        if (!check(item != nullptr, "create a corpus editor viewport"))
            return 1;
        item->setParentItem(scene.window.contentItem());
        auto* text = object->findChild<QQuickItem*>(QStringLiteral("corpusEditor"));
        auto* wrapper = text->property("textDocument").value<QQuickTextDocument*>();
        text->setFlag(QQuickItem::ItemObservesViewport, !prepared);
        wrapper->setTextDocument(document.get());
        mirrorfly::WordViewport* surface = nullptr;
        if (prepared && !retained)
        {
            text->setOpacity(0);
            text->setFlag(QQuickItem::ItemHasContents, false);
            surface = new mirrorfly::WordViewport(text->parentItem());
            surface->setSize(QSizeF(width, height));
            surface->setTextDocument(wrapper);
        }
        const auto position_surface = [&](qreal top)
        {
            if (surface)
            {
                surface->setY(top);
                surface->setDocumentTop(top);
            }
        };
        auto* resources = document->findChild<mirrorfly::WordImageResources*>();
        std::set<qulonglong> completed;
        int notifications = 0;
        QObject::connect(resources, &mirrorfly::WordImageResources::imageReady, item, [&](const QUrl& name)
        {
            completed.insert(name.path().mid(1).toULongLong());
        });
        QObject::connect(document.get(), &QTextDocument::contentsChanged, item, [&]()
        {
            if (mirrorfly::word_image_refresh_in_progress(*document))
                ++notifications;
        });
        std::vector<std::pair<qulonglong, QTextBlock>> candidates;
        std::set<std::string> formats;
        for (auto block = document->begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto format = it.fragment().charFormat();
                if (!format.isImageFormat())
                    continue;
                const QUrl name(format.toImageFormat().name());
                const auto id = name.path().mid(1).toULongLong();
                const auto found = std::find_if(source.document.images.begin(), source.document.images.end(),
                    [id](const auto& image)
                {
                    return image.id == id;
                });
                if (found != source.document.images.end() && formats.insert(found->mime_type).second)
                    candidates.emplace_back(id, block);
            }
        QDir().mkpath(output);
        if (candidates.empty())
            candidates.emplace_back(0, document->begin());
        QJsonArray views;
        std::vector<std::pair<qreal, QImage>> baselines;
        for (const auto& candidate : candidates)
        {
            const auto top = std::max<qreal>(
                0, document->documentLayout()->blockBoundingRect(candidate.second).top() - 20);
            object->setProperty("contentY", top);
            position_surface(top);
            QCoreApplication::processEvents();
            QElapsedTimer timer;
            timer.start();
            scene.frame();
            const auto first_ms = timer.elapsed();
            qint64 longest_repaint = 0;
            bool loaded = prepared || !asynchronous || completed.count(candidate.first);
            int settled = prepared || !asynchronous ? 2 : 0;
            while ((!loaded || settled < 2) && timer.elapsed() < 15000)
            {
                QElapsedTimer repaint;
                repaint.start();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                scene.frame();
                longest_repaint = qMax(longest_repaint, repaint.elapsed());
                loaded = completed.count(candidate.first) != 0;
                settled = resources->pending() ? 0 : settled + 1;
                QThread::msleep(1);
            }
            QCoreApplication::processEvents();
            const auto image = scene.frame();
            baselines.emplace_back(top, image);
            const bool saved =
                image.save(QDir(output).filePath(QStringLiteral("image-%1.png").arg(candidate.first)));
            views.append(QJsonObject{{"imageId", static_cast<int>(candidate.first)}, {"viewportTop", top},
                {"firstFrameMs", first_ms}, {"longestRepaintMs", longest_repaint}, {"loaded", loaded},
                {"pngSaved", saved}, {"completedImages", static_cast<int>(completed.size())},
                {"viewportWidth", item->width()}, {"viewportHeight", item->height()},
                {"editorHeight", text->height()}, {"clipWidth", text->clipRect().width()},
                {"clipHeight", text->clipRect().height()}});
            check(loaded && settled >= 2 && saved, "all images in the selected viewport finish rendering");
        }
        if (prepared)
        {
            for (int pass = 0; pass < 3; ++pass)
                for (auto it = baselines.rbegin(); it != baselines.rend(); ++it)
                {
                    object->setProperty("contentY", it->first);
                    position_surface(it->first);
                    QCoreApplication::processEvents();
                    const auto image = scene.frame();
                    if (pass == 0)
                        image.save(QDir(output).filePath(QStringLiteral("return-%1.png").arg(it->first)));
                    check(same_pixels(image, it->second), "returning to a corpus viewport preserves content");
                    check(scene.frame() == image, "a stationary corpus viewport never flickers");
                }
            check(notifications == 0 && completed.empty() && !resources->pending(),
                "prepared browsing never queues image decoding or resource-driven document refreshes");
        }
        QJsonArray edits;
        if (prepared)
        {
            for (const auto fraction : {0.0, 0.5, 0.95})
            {
                const auto block = document->findBlockByNumber(int((document->blockCount() - 1) * fraction));
                const auto top =
                    std::max<qreal>(0, document->documentLayout()->blockBoundingRect(block).top() - 20);
                object->setProperty("contentY", top);
                position_surface(top);
                QCoreApplication::processEvents();
                scene.frame();
                const auto baseline = scene.frame();
                const auto original_html = document->toHtml();
                const auto original_y = object->property("contentY").toReal();
                const auto original_bounds = document->documentLayout()->blockBoundingRect(block);
                std::vector<double> timings;
                std::vector<double> mutations, events, frames;
                for (int step = 0; step < 12; ++step)
                {
                    QTextCursor cursor(block);
                    QElapsedTimer timer;
                    timer.start();
                    cursor.insertText(QStringLiteral("测"));
                    const auto mutation_ms = timer.nsecsElapsed() / 1000000.0;
                    QCoreApplication::processEvents();
                    const auto event_ms = timer.nsecsElapsed() / 1000000.0;
                    scene.frame();
                    timings.push_back(timer.nsecsElapsed() / 1000000.0);
                    mutations.push_back(mutation_ms);
                    events.push_back(event_ms - mutation_ms);
                    frames.push_back(timings.back() - event_ms);
                    document->undo();
                    QCoreApplication::processEvents();
                    scene.frame();
                }
                std::sort(timings.begin(), timings.end());
                std::sort(mutations.begin(), mutations.end());
                std::sort(events.begin(), events.end());
                std::sort(frames.begin(), frames.end());
                edits.append(QJsonObject{{"block", block.blockNumber()}, {"medianMs", timings[6]},
                    {"characters", block.text().size()},
                    {"distributed",
                        mirrorfly::inspect_word_document(*document, block.position())
                                .value("align")
                                .toInt() == 4},
                    {"table",
                        mirrorfly::inspect_word_document(*document, block.position())
                            .value("inTable")
                            .toBool()},
                    {"p95Ms", timings[11]}, {"mutationMedianMs", mutations[6]}, {"eventsMedianMs", events[6]},
                    {"frameMedianMs", frames[6]}, {"beforeY", original_y},
                    {"afterY", object->property("contentY").toReal()},
                    {"beforeBlockY", original_bounds.top()},
                    {"afterBlockY", document->documentLayout()->blockBoundingRect(block).top()}});
                // Flickable rounds fractional content offsets during geometry changes. Compare the
                // document at the same coordinates, not two views separated by a subpixel scroll.
                object->setProperty("contentY", original_y);
                position_surface(original_y);
                QCoreApplication::processEvents();
                scene.frame();
                const auto restored = scene.frame();
                check(
                    original_html == document->toHtml(), "typing and undo preserve all document formatting");
                if (!same_pixels(baseline, restored))
                {
                    baseline.save(
                        QDir(output).filePath(QStringLiteral("edit-before-%1.png").arg(block.blockNumber())));
                    restored.save(
                        QDir(output).filePath(QStringLiteral("edit-after-%1.png").arg(block.blockNumber())));
                    std::cerr << "corpus pixel mismatch after undo at block " << block.blockNumber() << '\n';
                }
                check(same_pixels(baseline, restored), "typing and undo retain exact visible corpus pixels");
            }
        }
        const QJsonObject report{{"success", failures == 0}, {"asynchronous", asynchronous},
            {"prepared", prepared}, {"preparedBytes", resources->preparedBytes()}, {"fontFamilies", fonts},
            {"platform", QGuiApplication::platformName()}, {"backend", "D3D11 WARP, 960x720, no window"},
            {"repaintNotifications", notifications}, {"views", views}, {"editing", edits},
            {"retained", retained}, {"modified", document->isModified()},
            {"undo", document->isUndoAvailable()}};
        std::cout << QJsonDocument(report).toJson().constData();
        scene.control.invalidate();
        return failures ? 1 : 0;
    }
}

int run_word_image_render_tests(int argc, char* argv[])
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    QGuiApplication application(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    Scene scene;
    if (!check(scene.initialize(), "initialize the isolated D3D11 WARP rendering backend"))
        return 1;
    const auto arguments = application.arguments();
    if (arguments.size() == 4 &&
        (arguments[1] == "--corpus" || arguments[1] == "--corpus-sync" ||
            arguments[1] == "--corpus-prepared" || arguments[1] == "--corpus-retained"))
        return corpus(scene, arguments[2], arguments[3], arguments[1] != "--corpus-sync",
            arguments[1] == "--corpus-prepared" || arguments[1] == "--corpus-retained",
            arguments[1] == "--corpus-retained");
    cases(scene);
    Scene viewport_scene;
    if (check(viewport_scene.initialize(), "initialize the scrolling render regression"))
        viewport_cases(viewport_scene);
    Scene retained_scene;
    if (check(retained_scene.initialize(), "initialize the retained Word render regression"))
        retained_cases(retained_scene);
    Scene cell_scene;
    if (check(cell_scene.initialize(), "initialize the cell decoration render regression"))
        cell_cases(cell_scene);
    Scene partial_scene;
    if (check(partial_scene.initialize(), "initialize the partial Word repaint regression"))
        partial_repaint_cases(partial_scene);
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_image_render_tests(argc, argv);
}
