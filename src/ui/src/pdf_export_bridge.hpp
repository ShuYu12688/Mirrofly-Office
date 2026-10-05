#pragma once

#include "pdf_export_writer.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <map>

namespace mirrorfly
{
    class PdfExportBridge final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool active READ active CONSTANT)
        Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
        Q_PROPERTY(QString message READ message NOTIFY stateChanged)
        Q_PROPERTY(int completed READ completed NOTIFY stateChanged)
        Q_PROPERTY(int total READ total NOTIFY stateChanged)

    public:
        explicit PdfExportBridge(QVariantMap theme, QObject* parent = nullptr);
        ~PdfExportBridge() override;
        void registerSource(const QString& module, std::function<PdfExportSource()> provider);
        bool active() const;
        bool busy() const;
        QString message() const;
        int completed() const;
        int total() const;
        Q_INVOKABLE bool start(const QString& module, const QUrl& destination, const QVariantMap& options);
        Q_INVOKABLE void cancel();
        Q_INVOKABLE void clearMessage();
        Q_INVOKABLE QVariantMap snapshot() const;

    signals:
        void stateChanged();

    private:
        struct Result
        {
            bool success = false;
            QString message;
            QString path;
            qint64 bytes = 0;
        };
        bool fail(const QString& message);
        QVariantMap theme_;
        std::map<QString, std::function<PdfExportSource()>> providers_;
        std::shared_ptr<PdfExportProgress> progress_;
        QFutureWatcher<Result> watcher_;
        QTimer timer_;
        QString message_;
        QString path_;
        QString module_;
        qint64 bytes_ = 0;
        bool busy_ = false;
        bool success_ = false;
    };
}
