#include "editor_tools.hpp"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

#include <d3d11.h>
#include <wrl/client.h>

#include <iostream>
#include <memory>

namespace
{
    constexpr int target_width = 720;
    constexpr int target_height = 480;

    struct RenderScene
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
        QQuickRenderControl control;
        QQuickWindow window{&control};

        ~RenderScene()
        {
            control.invalidate();
        }
    };

    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    }

    bool initialize_scene(RenderScene& scene)
    {
        const auto created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, scene.device.GetAddressOf(),
            nullptr, scene.context.GetAddressOf());
        if (!check(SUCCEEDED(created), "create a software D3D11 device without a native window"))
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC description{};
        description.Width = target_width;
        description.Height = target_height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (!check(
                SUCCEEDED(scene.device->CreateTexture2D(&description, nullptr, scene.texture.GetAddressOf())),
                "allocate the bounded in-memory render target"))
        {
            return false;
        }
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (!check(SUCCEEDED(
                       scene.device->CreateTexture2D(&description, nullptr, scene.readback.GetAddressOf())),
                "allocate the bounded in-memory readback target"))
        {
            return false;
        }
        scene.window.setGeometry(0, 0, target_width, target_height);
        scene.window.setColor(Qt::transparent);
        scene.window.setGraphicsDevice(
            QQuickGraphicsDevice::fromDeviceAndContext(scene.device.Get(), scene.context.Get()));
        if (!check(scene.control.initialize(), "initialize the D3D11 Qt Quick scene graph"))
        {
            return false;
        }
        scene.window.setRenderTarget(
            QQuickRenderTarget::fromD3D11Texture(scene.texture.Get(), QSize(target_width, target_height)));
        return check(scene.window.rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11,
            "exercise the RHI text-node path rather than the Qt software scene graph");
    }

    void render_frame(RenderScene& scene)
    {
        QCoreApplication::processEvents();
        scene.control.polishItems();
        scene.control.beginFrame();
        scene.control.sync();
        scene.control.render();
        scene.control.endFrame();
    }

    bool has_rendered_pixels(RenderScene& scene)
    {
        scene.context->CopyResource(scene.readback.Get(), scene.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!check(SUCCEEDED(scene.context->Map(scene.readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
                "read back the completed frame without saving an image"))
        {
            return false;
        }
        int painted = 0;
        for (int row = 0; row < target_height; ++row)
        {
            const auto* pixels = static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch;
            for (int column = 0; column < target_width; ++column)
            {
                painted += pixels[column * 4 + 3] > 0 ? 1 : 0;
            }
        }
        scene.context->Unmap(scene.readback.Get(), 0);
        return check(painted > 128, "the isolated scene produced text and code-frame pixels");
    }

    QVariantMap test_theme()
    {
        return {{QStringLiteral("fontFamily"), QStringLiteral("Segoe UI")},
            {QStringLiteral("editorFontFamily"), QStringLiteral("Consolas")},
            {QStringLiteral("editorFontSize"), 16}, {QStringLiteral("accentSoft"), QStringLiteral("#eee5dc")},
            {QStringLiteral("borderColor"), QStringLiteral("#cbb9a8")},
            {QStringLiteral("textPrimary"), QStringLiteral("#25211e")}};
    }

    bool exercise_editor(RenderScene& scene, bool legacy_border)
    {
        QQmlEngine engine;
        mirrorfly::EditorTools tools;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { width: 720; height: 480; "
                          "textFormat: TextEdit.RichText; wrapMode: TextEdit.Wrap; color: '#25211e' }",
            QUrl{});
        std::unique_ptr<QObject> object(component.create());
        auto* item = qobject_cast<QQuickItem*>(object.get());
        if (!check(item != nullptr, "create an isolated editor item, without application QML"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        item->setParentItem(scene.window.contentItem());
        auto* wrapper = object->property("textDocument").value<QQuickTextDocument*>();
        if (!check(wrapper != nullptr, "obtain the public editor document"))
        {
            return false;
        }
        QObject::connect(&tools, &mirrorfly::EditorTools::documentEdited, &tools,
            [&tools](QQuickTextDocument* document)
        {
            tools.sourceText(document);
            tools.inspectDocument(document, 0);
        });
        const auto theme = test_theme();
        const int cycles = legacy_border ? 1 : 16;
        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            if (!check(tools.loadDocument(wrapper, {}, true, theme).value(QStringLiteral("valid")).toBool(),
                    "load an empty Markdown document"))
            {
                return false;
            }
            const auto inserted = tools.applyEdit(
                wrapper, 0, 0, QStringLiteral("code"), {{QStringLiteral("language"), QStringLiteral("cpp")}});
            if (!check(inserted.value(QStringLiteral("valid")).toBool(), "insert a new code frame"))
            {
                return false;
            }
            auto* document = wrapper->textDocument();
            QTextCursor cursor(document);
            cursor.setPosition(inserted.value(QStringLiteral("selectionStart")).toInt());
            if (legacy_border)
            {
                // Explicit opt-in diagnostic: recreate the released Qt 6.8.3 crash trigger.
                auto* frame = cursor.currentFrame();
                auto format = frame->frameFormat();
                format.setBorder(1);
                format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                frame->setFrameFormat(format);
                std::cout << "Rendering the legacy non-table solid border.\n" << std::flush;
            }
            render_frame(scene);
            cursor.insertText(QStringLiteral("int value = 42;\n// study note"));
            render_frame(scene);
            document->undo();
            render_frame(scene);
            document->redo();
            render_frame(scene);
            const auto left =
                tools.applyEdit(wrapper, cursor.position(), cursor.position(), QStringLiteral("exitCode"));
            if (!check(left.value(QStringLiteral("valid")).toBool(), "leave the rendered code frame"))
            {
                return false;
            }
            const int position = left.value(QStringLiteral("selectionEnd")).toInt();
            const auto table = tools.applyEdit(wrapper, position, position, QStringLiteral("table"),
                {{QStringLiteral("rows"), 2}, {QStringLiteral("columns"), 2}});
            if (!check(table.value(QStringLiteral("valid")).toBool(), "insert a real bordered table"))
            {
                return false;
            }
            const int cell_position = table.value(QStringLiteral("selectionStart")).toInt();
            const auto inline_edit = tools.applyEdit(wrapper, cell_position, cell_position + 2, "inlineCode");
            if (!check(inline_edit.value("valid").toBool(), "render inline code within the table"))
                return false;
            const auto aligned = tools.applyEdit(
                wrapper, cell_position, cell_position, "tableAlign", {{"alignment", "center"}});
            if (!check(aligned.value("valid").toBool(), "render centered header and body cells"))
                return false;
            render_frame(scene);
            if (!has_rendered_pixels(scene) ||
                !check(tools.canSave(), "rendered structures remain saveable") ||
                !check(!scene.window.isVisible(), "no application window is displayed"))
            {
                return false;
            }
            if (!legacy_border)
            {
                tools.loadDocument(
                    wrapper, "- parent\n- [ ] task **bold**\n  - child\n- tail\n", true, theme);
                document = wrapper->textDocument();
                const int task_position = document->toRawText().indexOf("task");
                if (!check(tools.applyEdit(wrapper, task_position, task_position, "listIndent")
                                .value("valid")
                                .toBool() &&
                            tools
                                .applyEdit(
                                    wrapper, task_position, task_position, "taskSet", {{"checked", true}})
                                .value("valid")
                                .toBool(),
                        "render nested list movement and completed checkbox"))
                    return false;
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                if (!has_rendered_pixels(scene) || !tools.canSave())
                    return false;
                tools.loadDocument(wrapper,
                    "- parent\n\n  > ```cpp\n  > x\n  > ```\n  >\n  > | A |\n  > | :---: |\n"
                    "  > | a |\n  >\n  > ---\n\n- tail\n",
                    true, theme);
                document = wrapper->textDocument();
                if (!check(tools.applyEdit(wrapper, 0, 1, "italic").value("valid").toBool(),
                        "render mixed code/table/rule ownership and parent character edit"))
                    return false;
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                if (!has_rendered_pixels(scene) || tools.sourceText(wrapper).isEmpty() || !tools.canSave())
                    return false;
                tools.loadDocument(wrapper,
                    "- first\n- parent\n\n  > ```cpp\n  > x\n  > ```\n  >\n  > | A |\n  > | :---: |\n"
                    "  > | a |\n  >\n  > ---\n\n- tail\n",
                    true, theme);
                document = wrapper->textDocument();
                const int mixed_parent = document->toRawText().indexOf("parent");
                if (!check(tools.applyEdit(wrapper, mixed_parent, mixed_parent, "listIndent")
                               .value("valid")
                               .toBool(),
                        "render parent list movement with owned code/table/rule"))
                    return false;
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                if (!has_rendered_pixels(scene) || tools.sourceText(wrapper).isEmpty() || !tools.canSave())
                    return false;
                tools.loadDocument(wrapper, "[**label**](../a \"title\") and text\n", true, theme);
                document = wrapper->textDocument();
                if (!check(tools.applyEdit(wrapper, 2, 2, "hardBreak").value("valid").toBool(),
                        "render a linked paragraph-internal hard break"))
                    return false;
                if (!check(tools
                               .applyEdit(wrapper, 1, 1, "link",
                                   {{"url", "../new (target)"}, {"title", "new title"}})
                               .value("valid")
                               .toBool(),
                        "render link address/title edits while preserving styled text"))
                    return false;
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                if (!has_rendered_pixels(scene) || !tools.canSave())
                    return false;
                if (!check(tools.applyEdit(wrapper, 1, 1, "thematicBreak").value("valid").toBool(),
                        "render a separator after a linked paragraph"))
                    return false;
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                tools.loadDocument(wrapper, "# a**b**c\n", true, theme);
                if (!check(tools.applyEdit(wrapper, 0, 2, "italic").value("valid").toBool(),
                        "render crossing styles in a heading without losing explicit bold"))
                    return false;
                document = wrapper->textDocument();
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                if (!has_rendered_pixels(scene) || tools.sourceText(wrapper).isEmpty() || !tools.canSave())
                    return false;
                tools.loadDocument(
                    wrapper, "- first\n- parent\n\n  > quote\n\n  continuation\n\n- tail\n", true, theme);
                document = wrapper->textDocument();
                const int owned_parent = document->toRawText().indexOf("parent");
                if (!check(tools.applyEdit(wrapper, owned_parent, owned_parent, "listIndent")
                               .value("valid")
                               .toBool(),
                        "render parent movement with owned quote and continued paragraphs"))
                    return false;
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                if (!has_rendered_pixels(scene) || tools.sourceText(wrapper).isEmpty() || !tools.canSave())
                    return false;
                tools.loadDocument(wrapper, "98) first\n99) second\n100) third\n", true, theme);
                document = wrapper->textDocument();
                if (!check(tools.applyEdit(wrapper, 0, 1, "italic").value("valid").toBool(),
                        "render parenthesized markers across increasing digit widths"))
                    return false;
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                if (!has_rendered_pixels(scene) || tools.sourceText(wrapper).isEmpty() || !tools.canSave())
                    return false;
                tools.loadDocument(wrapper,
                    "> - parent\n>\n>   | A | B |\n>   | --- | :---: |\n>   | a | b |\n", true, theme);
                document = wrapper->textDocument();
                const int quote_position = document->toRawText().indexOf("parent");
                if (!check(tools
                               .applyEdit(
                                   wrapper, quote_position, quote_position, "quoteSet", {{"quoteLevel", 2}})
                               .value("valid")
                               .toBool(),
                        "render quoted parent and owned table container margins"))
                    return false;
                render_frame(scene);
                document->undo();
                render_frame(scene);
                document->redo();
                render_frame(scene);
                if (!has_rendered_pixels(scene) || !tools.canSave())
                    return false;
            }
        }
        scene.control.invalidate();
        std::cout << "D3D11 WARP Markdown render regression passed: " << cycles << " document cycles, "
                  << cycles * (legacy_border ? 5 : 32) << " in-memory frames; no visible window.\n";
        return true;
    }
}

int run_markdown_render_tests(int argc, char* argv[])
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    QGuiApplication application(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    RenderScene scene;
    if (!initialize_scene(scene))
    {
        return 1;
    }
    const bool legacy_border = application.arguments().contains(QStringLiteral("--legacy-border"));
    return exercise_editor(scene, legacy_border) ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_markdown_render_tests(argc, argv);
}
