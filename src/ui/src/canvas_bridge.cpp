#include "canvas_bridge.hpp"
#include "document_path.hpp"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace mirrorfly
{
    CanvasBridge::CanvasBridge(bool pdf, QObject* parent) : DocumentView(parent), pdf_(pdf)
    {
        if (!pdf_)
            document_ = make_mindmap();
        connect(&worker_, &QFutureWatcher<Result>::finished, this, &CanvasBridge::completeOperation);
        connect(this, &CanvasBridge::stateChanged, this, [this]()
        {
            ++revision_;
        });
        connect(this, &CanvasBridge::viewChanged, this, [this]()
        {
            ++revision_;
        });
        render_timer_.setSingleShot(true);
        render_timer_.setInterval(90);
        connect(&render_timer_, &QTimer::timeout, this, &CanvasBridge::render);
        connect(&renderer_, &QFutureWatcher<PdfRenderResult>::finished, this, [this]()
        {
            PdfRenderResult result;
            try
            {
                result = renderer_.result();
            }
            catch (...)
            {
                result.error = PdfError::RenderFailed;
                result.message = "页面预览失败，可以切换页面重试。";
            }
            if (active_ && rendering_token_ == render_token_)
            {
                if (result.error == PdfError::None)
                {
                    const QImage preview(result.rgba.data(), result.width, result.height, result.width * 4,
                        QImage::Format_RGBA8888);
                    image_ = preview.copy();
                    emit imageChanged();
                }
                else
                {
                    error(QString::fromStdString(result.message));
                    emit imageChanged();
                }
            }
            else if (active_)
                render_timer_.start();
        });
    }
    CanvasBridge::~CanvasBridge()
    {
        worker_.waitForFinished();
        renderer_.waitForFinished();
    }
    bool CanvasBridge::previewReady() const
    {
        return !pdf_ || !image_.isNull() || !message_.isEmpty();
    }
    bool CanvasBridge::active() const
    {
        return active_;
    }
    bool CanvasBridge::locked() const
    {
        return operation_ != Operation::None || confirming_ || save_dialog_;
    }
    bool CanvasBridge::modified() const
    {
        return active_ && generation_ != saved_generation_;
    }
    bool CanvasBridge::canUndo() const
    {
        return !undo_.empty();
    }
    bool CanvasBridge::canRedo() const
    {
        return !redo_.empty();
    }
    QString CanvasBridge::kind() const
    {
        return pdf_ ? "pdf" : "mindmap";
    }
    QString CanvasBridge::documentName() const
    {
        if (!path_.isEmpty())
            return QFileInfo(path_).fileName();
        return pdf_ ? QStringLiteral("PDF 文档") : QStringLiteral("未命名思维导图");
    }
    QString CanvasBridge::message() const
    {
        return message_;
    }
    QVariantMap CanvasBridge::viewData() const
    {
        return view_;
    }
    QImage CanvasBridge::image() const
    {
        return image_;
    }
    QString CanvasBridge::documentPath() const
    {
        return path_;
    }

    QUrl CanvasBridge::saveUrl() const
    {
        QString suffix = ".pdf";
        if (!pdf_)
            suffix = ".mfg";
        QString file = path_;
        if (!file.isEmpty() && !file.endsWith(suffix, Qt::CaseInsensitive))
            file = QDir(QFileInfo(file).absolutePath()).filePath(QFileInfo(file).completeBaseName() + suffix);
        if (file.isEmpty() || (pdf_ && same_document_path(file, source_path_)))
        {
            QString directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            QString name = QStringLiteral("未命名思维导图");
            if (!path_.isEmpty())
            {
                directory = QFileInfo(path_).absolutePath();
                name = QFileInfo(path_).completeBaseName() + QStringLiteral("-编辑副本");
            }
            file = QDir(directory).filePath(name + suffix);
        }
        return QUrl::fromLocalFile(file);
    }
    void CanvasBridge::error(const QString& value)
    {
        message_ = value;
        emit stateChanged();
    }
    void CanvasBridge::clearMessage()
    {
        error({});
    }
    bool CanvasBridge::requestOpen(const QUrl& url)
    {
        const auto path = url.toLocalFile().toUtf8().toStdString();
        if (!url.isLocalFile() || locked() || !(pdf_ ? is_pdf_path(path) : is_mindmap_path(path)))
            return false;
        requestAction(Action::Open, url);
        return true;
    }
    void CanvasBridge::requestNew()
    {
        if (locked())
            return;
        if (pdf_)
        {
            emit openDialogRequested();
            return;
        }
        requestAction(Action::New);
    }
    void CanvasBridge::requestHome()
    {
        requestAction(Action::Home);
    }
    void CanvasBridge::requestHandoff(const QUrl& url)
    {
        requestAction(Action::Handoff, url);
    }
    bool CanvasBridge::requestWindowClose()
    {
        if (locked())
            return false;
        if (!modified())
            return true;
        requestAction(Action::Quit);
        return false;
    }
    void CanvasBridge::requestAction(Action action, const QUrl& url)
    {
        if (locked())
            return;
        pending_ = action;
        pending_url_ = url;
        if (modified())
        {
            confirming_ = true;
            emit stateChanged();
            emit confirmUnsavedRequested();
        }
        else
            performPending();
    }
    void CanvasBridge::resolveUnsaved(const QString& decision)
    {
        if (!confirming_)
            return;
        if (decision != "save" && decision != "discard" && decision != "cancel")
            return;
        confirming_ = false;
        if (decision == "save")
            save();
        else if (decision == "discard")
            performPending();
        else
        {
            pending_ = Action::None;
            pending_url_ = {};
            emit stateChanged();
        }
    }
    void CanvasBridge::reset()
    {
        beginConnection("off", {});
        active_ = false;
        document_ = pdf_ ? Document(PdfDocument{}) : Document(make_free_mindmap());
        undo_.clear();
        redo_.clear();
        generation_ = 0;
        saved_generation_ = 0;
        path_.clear();
        source_path_.clear();
        disk_revision_.clear();
        view_.clear();
        image_ = {};
        ++render_token_;
        emit viewChanged();
        emit imageChanged();
    }
    void CanvasBridge::performPending()
    {
        const auto action = pending_;
        const auto url = pending_url_;
        pending_ = Action::None;
        pending_url_ = {};
        if (action == Action::Open)
        {
            operation_ = Operation::Load;
            const auto path = url.toLocalFile();
            const bool pdf = pdf_;
            worker_.setFuture(QtConcurrent::run([path, pdf]() -> Result
            {
                if (pdf)
                {
                    auto result = load_pdf_file(path.toUtf8().toStdString());
                    return {result.error == PdfError::None, QString::fromStdString(result.message),
                        QString::fromStdString(result.path), result.revision, std::move(result.document)};
                }
                auto result = load_mindmap_file(path.toUtf8().toStdString());
                return {result.error == MindMapError::None, QString::fromStdString(result.message),
                    QString::fromStdString(result.path), result.revision, std::move(result.document)};
            }));
        }
        else if (action == Action::New)
        {
            reset();
            active_ = true;
            node_ = QString::fromStdString(std::get<MindMapDocument>(document_).root_id);
            refreshView();
            emit documentActivated();
        }
        else if (action == Action::Home)
            reset();
        else if (action == Action::Handoff)
        {
            operation_ = Operation::Handoff;
            emit handoffRequested(url);
        }
        else if (action == Action::Quit)
            emit windowCloseAllowed();
        emit stateChanged();
    }
    void CanvasBridge::finishHandoff(bool success)
    {
        if (operation_ != Operation::Handoff)
            return;
        operation_ = Operation::None;
        if (success)
            reset();
        emit stateChanged();
    }
    void CanvasBridge::save()
    {
        if (!active_ || locked())
            return;
        if (path_.isEmpty() || (pdf_ && same_document_path(path_, source_path_)) ||
            (!pdf_ && !path_.endsWith(".mfg", Qt::CaseInsensitive)))
            saveAs();
        else
            beginSave(path_);
    }
    bool CanvasBridge::saveTo(const QUrl& destination)
    {
        if (!active_ || locked() ||
            !new_document_destination(destination, pdf_ ? QStringList{"pdf"} : QStringList{"mfg"}))
        {
            error(QStringLiteral("请等待编辑完成，并选择已有目录下尚不存在的目标文件；不能覆盖原件。"));
            return false;
        }
        beginSave(QFileInfo(destination.toLocalFile()).absoluteFilePath(), true);
        return operation_ == Operation::Save;
    }

    void CanvasBridge::saveAs()
    {
        if (!active_ || locked())
            return;
        save_dialog_ = true;
        emit stateChanged();
        emit saveDialogRequested();
    }
    void CanvasBridge::selectSaveFile(const QUrl& url)
    {
        if (!save_dialog_)
            return;
        save_dialog_ = false;
        if (!url.isLocalFile())
        {
            pending_ = Action::None;
            error(QStringLiteral("请选择本地文件。"));
            return;
        }
        beginSave(url.toLocalFile());
    }
    void CanvasBridge::cancelSaveDialog()
    {
        if (!save_dialog_)
            return;
        save_dialog_ = false;
        pending_ = Action::None;
        pending_url_ = {};
        emit stateChanged();
    }
    void CanvasBridge::beginSave(const QString& path, bool new_file)
    {
        if (pdf_ && same_document_path(path, source_path_))
        {
            pending_ = Action::None;
            error(QStringLiteral("PDF 请保存到新的副本路径，原文件保持保留。"));
            return;
        }
        auto revision = same_document_path(path, path_) ? disk_revision_ : std::string{};
        if (new_file)
            revision = "missing";
        const auto document = document_;
        const bool pdf = pdf_;
        operation_ = Operation::Save;
        emit stateChanged();
        worker_.setFuture(QtConcurrent::run([document, path, revision, pdf]() -> Result
        {
            if (pdf)
            {
                const auto result =
                    save_pdf_file(path.toUtf8().toStdString(), std::get<PdfDocument>(document), revision);
                return {result.error == PdfError::None, QString::fromStdString(result.message),
                    QString::fromStdString(result.path), result.revision, {}};
            }
            auto result =
                save_mindmap_file(path.toUtf8().toStdString(), std::get<MindMapDocument>(document), revision);
            return {result.error == MindMapError::None, QString::fromStdString(result.message),
                QString::fromStdString(result.path), result.revision, {}};
        }));
    }
    void CanvasBridge::completeOperation()
    {
        const auto operation = operation_;
        Result result;
        try
        {
            result = worker_.result();
        }
        catch (...)
        {
            result.message = QStringLiteral("文件操作失败，当前文档已保留。");
        }
        operation_ = Operation::None;
        if (!result.success)
        {
            pending_ = Action::None;
            pending_url_ = {};
            error(result.message);
            if (operation == Operation::Load)
                emit openCompleted(false);
            return;
        }
        if (operation == Operation::Load)
        {
            reset();
            document_ = std::move(result.document);
            active_ = true;
            page_ = 0;
            source_path_ = result.path;
            if (!pdf_)
                node_ = QString::fromStdString(std::get<MindMapDocument>(document_).root_id);
        }
        path_ = result.path;
        disk_revision_ = result.revision;
        saved_generation_ = generation_;
        message_.clear();
        refreshView();
        emit fileRecorded(path_);
        emit stateChanged();
        if (operation == Operation::Load)
        {
            emit openCompleted(true);
            emit documentActivated();
        }
        else
            performPending();
    }
    std::size_t CanvasBridge::documentBytes(const Document& document)
    {
        std::size_t bytes = 1024;
        if (const auto* pdf = std::get_if<PdfDocument>(&document))
            for (const auto& page : pdf->pages)
            {
                bytes += sizeof(PdfPage) + page.text.size();
                for (const auto* annotations : {&page.original_annotations, &page.added_annotations})
                    for (const auto& annotation : *annotations)
                        bytes += sizeof(PdfAnnotation) + annotation.contents.size() + annotation.id.size() +
                            (annotation.stamp_bytes ? annotation.stamp_bytes->size() : 0);
            }
        else
        {
            const auto& graph = std::get<MindMapDocument>(document);
            for (const auto& node : graph.nodes)
                bytes += sizeof(MindMapNode) + node.id.size() + node.parent_id.size() + node.text.size() +
                    node.children.size() * 80 + node.border.size() + node.fill.size();
            for (const auto& edge : graph.edges)
                bytes += sizeof(MindMapEdge) + edge.id.size() + edge.from.size() + edge.to.size() +
                    edge.label.size();
        }
        return bytes;
    }
    void CanvasBridge::trim(std::deque<History>& history)
    {
        std::size_t bytes = 0;
        for (const auto& entry : history)
            bytes += entry.bytes;
        while (!history.empty() && (history.size() > 32 || bytes > 32 * 1024 * 1024))
        {
            bytes -= history.front().bytes;
            history.pop_front();
        }
    }
    void CanvasBridge::restoreHistory(bool redo)
    {
        auto& from = redo ? redo_ : undo_;
        auto& to = redo ? undo_ : redo_;
        if (locked() || from.empty())
            return;
        try
        {
            to.push_back({document_, generation_, documentBytes(document_)});
            document_ = std::move(from.back().document);
            generation_ = from.back().generation;
            from.pop_back();
            trim(to);
            refreshView();
            emit stateChanged();
        }
        catch (const std::bad_alloc&)
        {
            error(QStringLiteral("内存不足，历史操作未完成。"));
        }
    }
    void CanvasBridge::undo()
    {
        restoreHistory(false);
    }
    void CanvasBridge::redo()
    {
        restoreHistory(true);
    }
    void CanvasBridge::selectPage(int index)
    {
        if (!active_ || !pdf_ || locked() || index < 0 ||
            index >= static_cast<int>(std::get<PdfDocument>(document_).pages.size()))
            return;
        page_ = index;
        image_ = {};
        emit imageChanged();
        refreshView();
    }
    void CanvasBridge::selectNode(const QString& id)
    {
        if (!active_ || pdf_ || locked())
            return;
        const auto& nodes = std::get<MindMapDocument>(document_).nodes;
        if (std::none_of(nodes.begin(), nodes.end(), [&](const auto& node)
        {
            return node.id == id.toStdString();
        }))
            return;
        node_ = id;
        refreshView();
    }
    void CanvasBridge::setRenderSize(int width, int height)
    {
        width = std::clamp(width, 64, 2200);
        height = std::clamp(height, 64, 2200);
        if (width == render_width_ && height == render_height_)
            return;
        render_width_ = width;
        render_height_ = height;
        ++render_token_;
        render_timer_.start();
    }
    void CanvasBridge::render()
    {
        if (!active_ || !pdf_ || renderer_.isRunning())
            return;
        const auto document = std::get<PdfDocument>(document_);
        const auto id = document.pages[page_].id;
        const double ratio =
            pdf_page_display_width(document.pages[page_]) / pdf_page_display_height(document.pages[page_]);
        const int width = std::max(1, std::min(render_width_, static_cast<int>(render_height_ * ratio)));
        const int height = std::max(1, static_cast<int>(width / ratio));
        rendering_token_ = render_token_;
        renderer_.setFuture(QtConcurrent::run([document, id, width, height]()
        {
            return render_pdf_page(document, id, width, height);
        }));
    }
}
