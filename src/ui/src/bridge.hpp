#pragma once

#include <mirrorfly/core.hpp>

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace mirrorfly
{
    class InterfaceBridge final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(QVariantMap theme READ theme CONSTANT)
        Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)
        Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
        Q_PROPERTY(bool residentEnabled READ residentEnabled NOTIFY residentChanged)
        Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

    public:
        explicit InterfaceBridge(QVariantMap theme, QObject* parent = nullptr);

        QVariantMap theme() const;
        QVariantList recentFiles() const;
        bool ready() const;
        bool residentEnabled() const;
        void setResidentEnabled(bool enabled);
        Q_INVOKABLE void hideMainWindow();
        Q_INVOKABLE void restoreMainWindow();
        Q_INVOKABLE void requestQuit();
        Q_INVOKABLE void finishQuit();
        QString notice() const;

        void initialize();
        void setNotice(const QString& message);
        void recordFile(const QString& path);

        Q_INVOKABLE QVariantList filterFiles(const QString& query, const QString& category) const;
        Q_INVOKABLE void chooseFile();
        Q_INVOKABLE void selectFile(const QUrl& url);
        Q_INVOKABLE void inspectFile(const QString& path);
        Q_INVOKABLE void toggleStar(const QString& path);
        Q_INVOKABLE void clearNotice();
        Q_INVOKABLE void replayLoading();
        Q_INVOKABLE void finishLoadingPreview();
        Q_INVOKABLE void requestCreate(const QString& kind);

    signals:
        void recentFilesChanged();
        void readyChanged();
        void residentChanged();
        void hideMainRequested();
        void restoreMainRequested();
        void quitRequested();
        void quitApproved();
        void noticeChanged();
        void fileDialogRequested();
        void presentationDialogRequested();
        void pdfFileRequested(const QUrl& url);
        void mindmapFileRequested(const QUrl& url);
        void newMindmapRequested();
        void textFileRequested(const QUrl& url);
        void presentationFileRequested(const QUrl& url);
        void newTextRequested();
        void newWordRequested();
        void wordFileRequested(const QUrl& url);
        void newMarkdownRequested();
        void newPresentationRequested();
        void spreadsheetFileRequested(const QUrl& url);
        void newSpreadsheetRequested();

    private:
        void rememberSelection(const QString& path);
        void persistFiles();

        QVariantMap theme_;
        std::vector<RecentFile> files_;
        QString notice_;
        QTimer preview_timer_;
        bool ready_ = false;
        bool resident_enabled_ = false;
    };
}
