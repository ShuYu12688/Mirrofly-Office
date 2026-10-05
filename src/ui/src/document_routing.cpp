#include "document_routing.hpp"

#include "bridge.hpp"
#include "canvas_bridge.hpp"
#include "presentation_bridge.hpp"
#include "spreadsheet_bridge.hpp"
#include "text_bridge.hpp"
#include "word_bridge.hpp"

#include <array>
#include <functional>
#include <memory>

namespace
{
    struct DocumentEndpoint
    {
        std::function<bool()> active;
        std::function<bool()> locked;
        std::function<bool(const QUrl&)> open;
        std::function<void(const QUrl&)> create;
        std::function<void(const QUrl&)> handoff;
        std::function<void(bool)> finish;
    };

    struct DocumentRoutes
    {
        std::array<DocumentEndpoint, 6> endpoints;
        int source = -1;
        int target = -1;
        bool creating = false;

        static int destination(const QUrl& url)
        {
            if (url == QUrl(QStringLiteral("mirrorfly:new-mindmap")))
                return 5;
            if (url.isLocalFile() && mirrorfly::is_pdf_path(url.toLocalFile().toUtf8().toStdString()))
                return 4;
            if (url.isLocalFile() && mirrorfly::is_mindmap_path(url.toLocalFile().toUtf8().toStdString()))
                return 5;
            if (url == QUrl(QStringLiteral("mirrorfly:new-word")) ||
                (url.isLocalFile() && mirrorfly::is_word_path(url.toLocalFile().toStdString())))
                return 3;
            if (url == QUrl(QStringLiteral("mirrorfly:new-presentation")))
            {
                return 1;
            }
            if (url == QUrl(QStringLiteral("mirrorfly:new-spreadsheet")))
            {
                return 2;
            }
            if (url == QUrl(QStringLiteral("mirrorfly:new-text")) ||
                url == QUrl(QStringLiteral("mirrorfly:new-markdown")))
            {
                return 0;
            }
            if (!url.isLocalFile())
            {
                return -1;
            }
            const auto path = url.toLocalFile().toUtf8().toStdString();
            if (mirrorfly::is_spreadsheet_path(path))
            {
                return 2;
            }
            if (mirrorfly::is_presentation_path(path))
            {
                return 1;
            }
            return mirrorfly::is_plain_text_path(path) ? 0 : -1;
        }

        void complete(int endpoint, bool new_document, bool success)
        {
            if (source < 0 || endpoint != target || new_document != creating)
            {
                return;
            }
            const int previous = source;
            source = -1;
            target = -1;
            endpoints[previous].finish(success);
        }

        void start(int from, const QUrl& url)
        {
            const int to = destination(url);
            if (source >= 0 || to < 0 || endpoints[to].locked())
            {
                if (from >= 0)
                {
                    endpoints[from].finish(false);
                }
                return;
            }
            source = from;
            target = to;
            creating = !url.isLocalFile();
            if (creating)
            {
                endpoints[to].create(url);
            }
            else if (!endpoints[to].open(url))
            {
                complete(to, false, false);
            }
            if (from < 0)
            {
                target = -1;
            }
        }

        void request(const QUrl& url)
        {
            const int to = destination(url);
            if (to < 0 || source >= 0)
            {
                return;
            }
            for (const auto& endpoint : endpoints)
            {
                if (endpoint.locked())
                {
                    return;
                }
            }
            for (int index = 0; index < static_cast<int>(endpoints.size()); ++index)
            {
                if (index != to && endpoints[index].active())
                {
                    endpoints[index].handoff(url);
                    return;
                }
            }
            start(-1, url);
        }
    };

    template <typename Bridge>
    void connect_endpoint(
        QObject* context, Bridge& bridge, const std::shared_ptr<DocumentRoutes>& routes, int index)
    {
        QObject::connect(&bridge, &Bridge::handoffRequested, context, [routes, index](const QUrl& url)
        {
            routes->start(index, url);
        });
        QObject::connect(&bridge, &Bridge::openCompleted, context, [routes, index](bool success)
        {
            routes->complete(index, false, success);
        });
        QObject::connect(&bridge, &Bridge::documentActivated, context, [routes, index]()
        {
            routes->complete(index, true, true);
        });
    }

    template <typename Bridge> DocumentEndpoint endpoint_for(Bridge& bridge)
    {
        return {[&bridge]()
        {
            return bridge.active();
        },
            [&bridge]()
        {
            return bridge.locked();
        },
            [&bridge](const QUrl& url)
        {
            return bridge.requestOpen(url);
        },
            [&bridge](const QUrl&)
        {
            bridge.requestNew();
        },
            [&bridge](const QUrl& url)
        {
            bridge.requestHandoff(url);
        }, [&bridge](bool success)
        {
            bridge.finishHandoff(success);
        }};
    }
}

namespace mirrorfly
{
    void connect_document_routes(InterfaceBridge& interface_bridge, TextEditorBridge& text_editor,
        PresentationBridge& presentation, SpreadsheetBridge& spreadsheet, WordBridge& word, CanvasBridge* pdf,
        CanvasBridge* mindmap)
    {
        const auto routes = std::make_shared<DocumentRoutes>();
        routes->endpoints[0] = {[&text_editor]()
        {
            return text_editor.active();
        },
            [&text_editor]()
        {
            return text_editor.locked();
        },
            [&text_editor](const QUrl& url)
        {
            text_editor.requestOpen(url);
            return true;
        },
            [&text_editor](const QUrl& url)
        {
            if (url == QUrl(QStringLiteral("mirrorfly:new-markdown")))
            {
                text_editor.requestNewMarkdown();
            }
            else
            {
                text_editor.requestNew();
            }
        },
            [&text_editor](const QUrl& url)
        {
            text_editor.requestHandoff(url);
        }, [&text_editor](bool success)
        {
            text_editor.finishHandoff(success);
        }};
        routes->endpoints[1] = endpoint_for(presentation);
        routes->endpoints[2] = endpoint_for(spreadsheet);
        routes->endpoints[3] = endpoint_for(word);
        for (int index : {4, 5})
            routes->endpoints[index] = {[]()
            {
                return false;
            },
                []()
            {
                return false;
            },
                [](const QUrl&)
            {
                return false;
            },
                [](const QUrl&)
            {
            },
                [](const QUrl&)
            {
            }, [](bool)
            {
            }};
        if (pdf)
        {
            routes->endpoints[4] = endpoint_for(*pdf);
            connect_endpoint(&interface_bridge, *pdf, routes, 4);
        }
        if (mindmap)
        {
            routes->endpoints[5] = endpoint_for(*mindmap);
            connect_endpoint(&interface_bridge, *mindmap, routes, 5);
        }
        connect_endpoint(&interface_bridge, word, routes, 3);
        connect_endpoint(&interface_bridge, text_editor, routes, 0);
        connect_endpoint(&interface_bridge, presentation, routes, 1);
        connect_endpoint(&interface_bridge, spreadsheet, routes, 2);
        const auto open = [routes](const QUrl& url)
        {
            routes->request(url);
        };
        QObject::connect(&interface_bridge, &InterfaceBridge::pdfFileRequested, &interface_bridge, open);
        QObject::connect(&interface_bridge, &InterfaceBridge::mindmapFileRequested, &interface_bridge, open);
        QObject::connect(&interface_bridge, &InterfaceBridge::newMindmapRequested, &interface_bridge,
            [routes]()
        {
            routes->request(QUrl(QStringLiteral("mirrorfly:new-mindmap")));
        });
        if (pdf)
            QObject::connect(
                pdf, &CanvasBridge::fileRecorded, &interface_bridge, &InterfaceBridge::recordFile);
        if (mindmap)
            QObject::connect(
                mindmap, &CanvasBridge::fileRecorded, &interface_bridge, &InterfaceBridge::recordFile);
        QObject::connect(&interface_bridge, &InterfaceBridge::wordFileRequested, &interface_bridge, open);
        QObject::connect(&interface_bridge, &InterfaceBridge::newWordRequested, &interface_bridge, [routes]()
        {
            routes->request(QUrl(QStringLiteral("mirrorfly:new-word")));
        });
        QObject::connect(&word, &WordBridge::fileRecorded, &interface_bridge, &InterfaceBridge::recordFile);
        QObject::connect(&interface_bridge, &InterfaceBridge::textFileRequested, &interface_bridge, open);
        QObject::connect(
            &interface_bridge, &InterfaceBridge::presentationFileRequested, &interface_bridge, open);
        QObject::connect(
            &interface_bridge, &InterfaceBridge::spreadsheetFileRequested, &interface_bridge, open);
        QObject::connect(&interface_bridge, &InterfaceBridge::newTextRequested, &interface_bridge, [routes]()
        {
            routes->request(QUrl(QStringLiteral("mirrorfly:new-text")));
        });
        QObject::connect(&interface_bridge, &InterfaceBridge::newMarkdownRequested, &interface_bridge,
            [routes]()
        {
            routes->request(QUrl(QStringLiteral("mirrorfly:new-markdown")));
        });
        QObject::connect(&interface_bridge, &InterfaceBridge::newPresentationRequested, &interface_bridge,
            [routes]()
        {
            routes->request(QUrl(QStringLiteral("mirrorfly:new-presentation")));
        });
        QObject::connect(&interface_bridge, &InterfaceBridge::newSpreadsheetRequested, &interface_bridge,
            [routes]()
        {
            routes->request(QUrl(QStringLiteral("mirrorfly:new-spreadsheet")));
        });
        QObject::connect(
            &text_editor, &TextEditorBridge::fileRecorded, &interface_bridge, &InterfaceBridge::recordFile);
        QObject::connect(&presentation, &PresentationBridge::fileRecorded, &interface_bridge,
            &InterfaceBridge::recordFile);
        QObject::connect(
            &spreadsheet, &SpreadsheetBridge::fileRecorded, &interface_bridge, &InterfaceBridge::recordFile);
    }
}
