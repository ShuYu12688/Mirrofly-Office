#pragma once

#include "presentation_image_export.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include <functional>

namespace mirrorfly
{
    struct PresentationImageExportSource
    {
        RenderPresentationPtr document;
        int current_slide = 0;
        QString document_name;
        QString error;
    };

    class PresentationImageExportBridge final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool active READ active CONSTANT)
        Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
        Q_PROPERTY(QString message READ message NOTIFY stateChanged)
        Q_PROPERTY(int completed READ completed NOTIFY stateChanged)
        Q_PROPERTY(int total READ total NOTIFY stateChanged)

    public:
        explicit PresentationImageExportBridge(QVariantMap theme, QObject* parent = nullptr);
        ~PresentationImageExportBridge() override;
        void registerSource(std::function<PresentationImageExportSource()> provider);
        bool active() const;
        bool busy() const;
        QString message() const;
        int completed() const;
        int total() const;
        Q_INVOKABLE bool start(const QUrl& parent, const QVariantMap& options);
        Q_INVOKABLE void cancel();
        Q_INVOKABLE void clearMessage();
        Q_INVOKABLE QVariantMap snapshot() const;

    signals:
        void stateChanged();

    private:
        bool fail(const QString& message);
        QVariantMap theme_;
        std::function<PresentationImageExportSource()> provider_;
        QThreadPool pool_;
        QFutureWatcher<PresentationImageExportResult> watcher_;
        QTimer timer_;
        std::shared_ptr<PresentationImageExportProgress> progress_;
        QString message_;
        QString path_;
        int pages_ = 0;
        qint64 bytes_ = 0;
        bool busy_ = false;
        bool success_ = false;
    };
}
