#pragma once

namespace mirrorfly
{
    class InterfaceBridge;
    class TextEditorBridge;
    class PresentationBridge;
    class SpreadsheetBridge;
    class WordBridge;
    class CanvasBridge;

    void connect_document_routes(InterfaceBridge& interface_bridge, TextEditorBridge& text_editor,
        PresentationBridge& presentation, SpreadsheetBridge& spreadsheet, WordBridge& word,
        CanvasBridge* pdf = nullptr, CanvasBridge* mindmap = nullptr);
}
