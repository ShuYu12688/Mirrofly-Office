#include "presentation_image_export_bridge.hpp"

#include <QFileInfo>
#include <QtConcurrentRun>

namespace mirrorfly
{
    PresentationImageExportBridge::PresentationImageExportBridge(QVariantMap theme, QObject* parent)
        : QObject(parent), theme_(std::move(theme))
    {
        pool_.setMaxThreadCount(1);
        pool_.setThreadPriority(QThread::LowPriority);
        timer_.setInterval(150);
        connect(&timer_, &QTimer::timeout, this, &PresentationImageExportBridge::stateChanged);
        connect(&watcher_, &QFutureWatcher<PresentationImageExportResult>::finished, this, [this]()
        {
            timer_.stop();
            const auto result = watcher_.result();
            busy_ = false;
            success_ = result.success;
            message_ = result.message;
            path_ = result.path;
            pages_ = result.pages;
            bytes_ = result.bytes;
            emit stateChanged();
        });
    }

    PresentationImageExportBridge::~PresentationImageExportBridge()
    {
        cancel();
        watcher_.waitForFinished();
    }

    void PresentationImageExportBridge::registerSource(
        std::function<PresentationImageExportSource()> provider)
    {
        provider_ = std::move(provider);
    }

    bool PresentationImageExportBridge::active() const
    {
        return true;
    }

    bool PresentationImageExportBridge::busy() const
    {
        return busy_;
    }

    QString PresentationImageExportBridge::message() const
    {
        return message_;
    }

    int PresentationImageExportBridge::completed() const
    {
        return progress_ ? progress_->completed.load() : 0;
    }

    int PresentationImageExportBridge::total() const
    {
        return progress_ ? progress_->total.load() : 0;
    }

    bool PresentationImageExportBridge::fail(const QString& message)
    {
        success_ = false;
        message_ = message;
        emit stateChanged();
        return false;
    }

    bool PresentationImageExportBridge::start(const QUrl& parent, const QVariantMap& options)
    {
        if (busy_)
            return false;
        if (!parent.isLocalFile() || !provider_)
            return fail(QStringLiteral("请选择本地导出文件夹并等待演示文稿准备好。"));
        if (options.size() != 3 || !options.contains("format") || !options.contains("scope") ||
            !options.contains("longEdge") || options.value("format").metaType().id() != QMetaType::QString ||
            options.value("scope").metaType().id() != QMetaType::QString)
            return fail(QStringLiteral("图片导出参数无效。"));
        PresentationImageExportOptions settings;
        settings.format = options.value("format").toString();
        settings.scope = options.value("scope").toString();
        bool number_ok = false;
        settings.long_edge = options.value("longEdge").toInt(&number_ok);
        if (!number_ok || !QStringList{"png", "jpg"}.contains(settings.format) ||
            !QStringList{"all", "current"}.contains(settings.scope) ||
            !QList<int>{1280, 1920}.contains(settings.long_edge))
            return fail(QStringLiteral("图片导出参数无效。"));
        const auto input = provider_();
        if (!input.error.isEmpty())
            return fail(input.error);
        const auto& source = input.document;
        if (!source || !source->scene || input.current_slide < 0 ||
            input.current_slide >= static_cast<int>(source->scene->slides.size()) ||
            !QFileInfo(parent.toLocalFile()).isDir())
            return fail(QStringLiteral("演示文稿或目标文件夹无效。"));
        auto snapshot = std::make_shared<RenderPresentation>(*source);
        // A file export keeps its immutable page snapshot even when GUI render revisions advance.
        snapshot->render_request_token.reset();
        progress_ = std::make_shared<PresentationImageExportProgress>();
        auto progress = progress_;
        const auto theme = theme_;
        const auto path = parent.toLocalFile();
        busy_ = true;
        success_ = false;
        message_ = QStringLiteral("正在导出幻灯片图片…");
        path_.clear();
        pages_ = 0;
        bytes_ = 0;
        watcher_.setFuture(QtConcurrent::run(&pool_,
            [snapshot, current_slide = input.current_slide, theme, path, document_name = input.document_name,
                settings, progress]()
        {
            try
            {
                return render_presentation_images(
                    snapshot, current_slide, theme, path, document_name, settings, *progress);
            }
            catch (...)
            {
                return PresentationImageExportResult{
                    false, QStringLiteral("图片导出失败，请检查可用内存和目标目录。")};
            }
        }));
        timer_.start();
        emit stateChanged();
        return true;
    }

    void PresentationImageExportBridge::cancel()
    {
        if (progress_)
            progress_->cancelled.store(true);
    }

    void PresentationImageExportBridge::clearMessage()
    {
        if (!busy_)
        {
            message_.clear();
            emit stateChanged();
        }
    }

    QVariantMap PresentationImageExportBridge::snapshot() const
    {
        return {{"busy", busy_}, {"success", success_}, {"message", message_}, {"path", path_},
            {"pages", pages_}, {"bytes", bytes_}, {"completed", completed()}, {"total", total()}};
    }
}
