#include "bridge.hpp"

#include <mirrorfly/mindmap.hpp>
#include <mirrorfly/pdf.hpp>
#include <mirrorfly/spreadsheet.hpp>
#include <mirrorfly/word.hpp>

#include <mirrorfly/platform.hpp>
#include <mirrorfly/presentation.hpp>
#include <mirrorfly/text.hpp>

#include <QTimer>

#include <algorithm>
#include <utility>

namespace
{
    QVariantList to_variant_list(const std::vector<mirrorfly::RecentFile>& files)
    {
        QVariantList result;
        result.reserve(static_cast<qsizetype>(files.size()));
        for (const auto& file : files)
        {
            result.append(QVariantMap{{"name", QString::fromStdString(file.name)},
                {"path", QString::fromStdString(file.path)},
                {"kind", QString::fromStdString(mirrorfly::document_kind_key(file.kind))},
                {"modified", QString::fromStdString(file.modified)},
                {"sizeText", QString::fromStdString(file.size_text)}, {"starred", file.starred}});
        }
        return result;
    }
}

namespace mirrorfly
{
    InterfaceBridge::InterfaceBridge(QVariantMap theme, QObject* parent)
        : QObject(parent), theme_(std::move(theme))
    {
        preview_timer_.setSingleShot(true);
        connect(&preview_timer_, &QTimer::timeout, this, &InterfaceBridge::finishLoadingPreview);
    }

    QVariantMap InterfaceBridge::theme() const
    {
        return theme_;
    }

    QVariantList InterfaceBridge::recentFiles() const
    {
        return to_variant_list(files_);
    }

    bool InterfaceBridge::ready() const
    {
        return ready_;
    }

    bool InterfaceBridge::residentEnabled() const
    {
        return resident_enabled_;
    }

    void InterfaceBridge::setResidentEnabled(bool enabled)
    {
        resident_enabled_ = enabled;
        emit residentChanged();
    }

    void InterfaceBridge::hideMainWindow()
    {
        emit hideMainRequested();
    }

    void InterfaceBridge::restoreMainWindow()
    {
        emit restoreMainRequested();
    }

    void InterfaceBridge::requestQuit()
    {
        emit quitRequested();
    }

    void InterfaceBridge::finishQuit()
    {
        emit quitApproved();
    }

    QString InterfaceBridge::notice() const
    {
        return notice_;
    }

    void InterfaceBridge::initialize()
    {
        files_ = load_recent_files();
        emit recentFilesChanged();
        finishLoadingPreview();
    }

    void InterfaceBridge::setNotice(const QString& message)
    {
        notice_ = message;
        emit noticeChanged();
    }

    QVariantList InterfaceBridge::filterFiles(const QString& query, const QString& category) const
    {
        return to_variant_list(filter_files(files_, query.toStdString(), category.toStdString()));
    }

    void InterfaceBridge::chooseFile()
    {
        emit fileDialogRequested();
    }

    void InterfaceBridge::selectFile(const QUrl& url)
    {
        if (url.isLocalFile())
        {
            rememberSelection(url.toLocalFile());
        }
    }

    void InterfaceBridge::inspectFile(const QString& path)
    {
        rememberSelection(path);
    }

    void InterfaceBridge::rememberSelection(const QString& path)
    {
        if (is_pdf_path(path.toUtf8().toStdString()))
        {
            clearNotice();
            emit pdfFileRequested(QUrl::fromLocalFile(path));
            return;
        }
        if (is_mindmap_path(path.toUtf8().toStdString()))
        {
            clearNotice();
            emit mindmapFileRequested(QUrl::fromLocalFile(path));
            return;
        }
        if (is_word_path(path.toUtf8().toStdString()))
        {
            clearNotice();
            emit wordFileRequested(QUrl::fromLocalFile(path));
            return;
        }
        if (is_spreadsheet_path(path.toUtf8().toStdString()))
        {
            clearNotice();
            emit spreadsheetFileRequested(QUrl::fromLocalFile(path));
            return;
        }
        if (is_presentation_path(path.toUtf8().toStdString()))
        {
            clearNotice();
            emit presentationFileRequested(QUrl::fromLocalFile(path));
            return;
        }
        if (is_plain_text_path(path.toUtf8().toStdString()))
        {
            clearNotice();
            emit textFileRequested(QUrl::fromLocalFile(path));
            return;
        }
        const auto file = inspect_local_file(path.toUtf8().toStdString());
        if (!file)
        {
            setNotice(QStringLiteral("文件不存在或无法读取，请重新选择。"));
            return;
        }
        files_ = remember_file(files_, *file);
        emit recentFilesChanged();
        const QString notice = QStringLiteral("已添加「%1」到最近文件。");
        setNotice(notice.arg(QString::fromStdString(file->name)));
        persistFiles();
    }

    void InterfaceBridge::recordFile(const QString& path)
    {
        const auto file = inspect_local_file(path.toUtf8().toStdString());
        if (file)
        {
            files_ = remember_file(files_, *file);
            emit recentFilesChanged();
            persistFiles();
        }
    }

    void InterfaceBridge::toggleStar(const QString& path)
    {
        files_ = toggle_star(files_, path.toStdString());
        emit recentFilesChanged();
        persistFiles();
    }

    void InterfaceBridge::persistFiles()
    {
        if (!save_recent_files(files_))
        {
            setNotice(QStringLiteral("这次文件记录未能保存，关闭应用后可能丢失。"));
        }
    }

    void InterfaceBridge::clearNotice()
    {
        setNotice({});
    }

    void InterfaceBridge::replayLoading()
    {
        if (!ready_)
        {
            return;
        }
        ready_ = false;
        emit readyChanged();
        // Only the explicit art preview holds the loading screen.
        const int preview_duration = theme_.value(QStringLiteral("loadingReplayDuration"), 3000).toInt();
        preview_timer_.start(std::clamp(preview_duration, 600, 10000));
    }

    void InterfaceBridge::finishLoadingPreview()
    {
        preview_timer_.stop();
        if (!ready_)
        {
            ready_ = true;
            emit readyChanged();
        }
    }

    void InterfaceBridge::requestCreate(const QString& kind)
    {
        if (kind == "pdf")
        {
            chooseFile();
            return;
        }
        if (kind == "mindmap")
        {
            clearNotice();
            emit newMindmapRequested();
            return;
        }
        QString label = QStringLiteral("文档");
        if (kind == QStringLiteral("word"))
        {
            clearNotice();
            emit newWordRequested();
            return;
        }
        if (kind == QStringLiteral("writer"))
        {
            clearNotice();
            emit newTextRequested();
            return;
        }
        else if (kind == QStringLiteral("markdown"))
        {
            clearNotice();
            emit newMarkdownRequested();
            return;
        }
        else if (kind == QStringLiteral("sheets"))
        {
            clearNotice();
            emit newSpreadsheetRequested();
            return;
        }
        else if (kind == QStringLiteral("slides"))
        {
            clearNotice();
            emit newPresentationRequested();
            return;
        }
        setNotice(QStringLiteral("%1编辑将在后续版本开放。现在可以新建或打开纯文本文件。").arg(label));
    }
}
