#include "pdf_export_bridge.hpp"
#include "document_path.hpp"
#include <QFileInfo>
#include <QtConcurrentRun>
#include <algorithm>

namespace mirrorfly
{
    PdfExportBridge::PdfExportBridge(QVariantMap theme, QObject* parent)
        : QObject(parent), theme_(std::move(theme))
    {
        timer_.setInterval(150);
        connect(&timer_, &QTimer::timeout, this, &PdfExportBridge::stateChanged);
        connect(&watcher_, &QFutureWatcher<Result>::finished, this, [this]()
        {
            timer_.stop();
            const auto result = watcher_.result();
            busy_ = false;
            success_ = result.success;
            message_ = result.message;
            path_ = result.path;
            bytes_ = result.bytes;
            emit stateChanged();
        });
    }

    PdfExportBridge::~PdfExportBridge()
    {
        cancel();
        watcher_.waitForFinished();
    }

    void PdfExportBridge::registerSource(const QString& module, std::function<PdfExportSource()> provider)
    {
        providers_[module] = std::move(provider);
    }

    bool PdfExportBridge::active() const
    {
        return true;
    }
    bool PdfExportBridge::busy() const
    {
        return busy_;
    }
    QString PdfExportBridge::message() const
    {
        return message_;
    }
    int PdfExportBridge::completed() const
    {
        return progress_ ? progress_->completed.load() : 0;
    }
    int PdfExportBridge::total() const
    {
        return progress_ ? progress_->total.load() : 0;
    }

    bool PdfExportBridge::fail(const QString& message)
    {
        message_ = message;
        success_ = false;
        emit stateChanged();
        return false;
    }

    bool PdfExportBridge::start(const QString& module, const QUrl& destination, const QVariantMap& options)
    {
        if (busy_)
            return false;
        const auto provider = providers_.find(module);
        if (provider == providers_.end() || !destination.isLocalFile())
            return fail(QStringLiteral("导出模块或目标位置无效。"));
        PdfExportOptions settings;
        for (auto item = options.cbegin(); item != options.cend(); ++item)
        {
            const bool boolean = item.key() == "overwrite" || item.key() == "landscape";
            if ((!boolean && item.key() != "scope" && item.key() != "layout" &&
                    item.key() != "compression") ||
                item->metaType().id() != (boolean ? QMetaType::Bool : QMetaType::QString))
                return fail(QStringLiteral("导出参数名称或类型无效。"));
        }
        settings.scope = options.value("scope", "all").toString();
        settings.layout = options.value("layout", "fit").toString();
        settings.compression = options.value("compression", "structure").toString();
        settings.landscape = options.value("landscape", false).toBool();
        if (!QStringList{"all", "current", "selection"}.contains(settings.scope) ||
            !QStringList{"fit", "tiles"}.contains(settings.layout) ||
            !QStringList{"structure", "screen", "print"}.contains(settings.compression) ||
            (settings.scope == "selection" && module != "sheets") ||
            (settings.scope == "current" && module != "sheets" && module != "slides" && module != "pdf") ||
            (settings.layout != "fit" && module != "mindmap") ||
            (settings.compression != "structure" && module != "pdf"))
            return fail(QStringLiteral("此模块不支持选定的导出范围或选项。"));
        try
        {
            auto source = provider->second();
            if (!source.error.empty() || std::holds_alternative<std::monostate>(source.content))
            {
                if (source.error.empty())
                    return fail(QStringLiteral("请先完成当前输入或等待文件准备好。"));
                return fail(QString::fromStdString(source.error));
            }
            const auto file = destination.toLocalFile();
            if (!source.protected_source_path.empty() &&
                same_document_path(file, QString::fromStdString(source.protected_source_path)))
                return fail(QStringLiteral("不能覆盖最初导入的 PDF，请选择新路径。"));
            if (!source.source_path.empty() &&
                same_document_path(file, QString::fromStdString(source.source_path)))
                return fail(QStringLiteral("导出必须选择新路径，保留原始文件。"));
            const auto target = inspect_pdf_destination(file.toUtf8().toStdString());
            if (target.error != PdfError::None)
                return fail(QString::fromStdString(target.message));
            if (target.revision != "missing" && !options.value("overwrite", false).toBool())
                return fail(QStringLiteral("目标文件已存在，请选择新文件名或明确允许覆盖。"));
            progress_ = std::make_shared<PdfExportProgress>();
            const auto progress = progress_;
            const auto theme = theme_;
            busy_ = true;
            success_ = false;
            module_ = module;
            path_.clear();
            bytes_ = 0;
            message_ = QStringLiteral("正在导出 PDF，可继续编辑…");
            watcher_.setFuture(
                QtConcurrent::run([source = std::move(source), settings, target, progress, theme]() -> Result
            {
                try
                {
                    auto output = render_pdf_export(source, settings, theme, *progress);
                    if (progress->cancelled.load())
                        return {false, QStringLiteral("导出已取消，未写入目标文件。"), {}, 0};
                    if (output.error != PdfError::None)
                        return {false, QString::fromStdString(output.message), {}, 0};
                    const auto saved = save_pdf_bytes(target.path, output.bytes, target.revision);
                    if (saved.error != PdfError::None)
                        return {false, QString::fromStdString(saved.message), {}, 0};
                    const auto bytes = static_cast<qint64>(output.bytes.size());
                    QString message = QStringLiteral("PDF 已导出 · %1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
                    if (const auto* pdf = std::get_if<PdfDocument>(&source.content))
                    {
                        if (settings.scope == "all" && pdf->source_bytes)
                        {
                            const double change =
                                100.0 * (1 - static_cast<double>(bytes) / pdf->source_bytes->size());
                            message += change > 0
                                ? QStringLiteral(" · 比源文件减少 %1%").arg(change, 0, 'f', 1)
                                : QStringLiteral(" · 本文件未缩小，原文件保留");
                        }
                    }
                    return {true, message, QString::fromStdString(saved.path), bytes};
                }
                catch (...)
                {
                    return {false, QStringLiteral("导出失败，请检查文件和可用内存。"), {}, 0};
                }
            }));
            timer_.start();
            emit stateChanged();
            return true;
        }
        catch (...)
        {
            busy_ = false;
            return fail(QStringLiteral("无法准备导出快照，当前文档已保留。"));
        }
    }

    void PdfExportBridge::cancel()
    {
        if (progress_)
            progress_->cancelled.store(true);
    }

    void PdfExportBridge::clearMessage()
    {
        if (!busy_)
        {
            message_.clear();
            emit stateChanged();
        }
    }

    QVariantMap PdfExportBridge::snapshot() const
    {
        return {{"busy", busy_}, {"success", success_}, {"module", module_}, {"path", path_},
            {"bytes", bytes_}, {"message", message_}, {"completed", completed()}, {"total", total()}};
    }
}
