#pragma once

#include "office_ai_stream.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace mirrorfly
{
    enum class OfficeAiStop
    {
        Complete,
        Tools,
        Truncated,
        Error
    };

    struct OfficeAiModelReply
    {
        OfficeAiStop stop = OfficeAiStop::Error;
        QString text;
        QString error;
        QString error_code;
        QString finish_reason;
        bool retryable = false;
        int retry_after_ms = 0;
        int http_status = 0;
        QJsonArray calls;
        QJsonObject continuation;
        QJsonObject usage;
        qint64 elapsed_ms = 0;
        qint64 first_packet_ms = -1;
    };

    // Owns credentials, HTTP and the provider's wire protocol. Never reads or edits an Office session.
    class OfficeAiProvider final : public QObject
    {
        Q_OBJECT

    public:
        explicit OfficeAiProvider(QNetworkAccessManager* transport = nullptr, QObject* parent = nullptr);
        ~OfficeAiProvider() override;
        bool configure(
            const QString& address, const QString& model, const QString& key, const QString& effort);
        bool configured() const;
        static bool validConfiguration(
            const QString& address, const QString& model, const QString& key, const QString& effort);
        QString address() const;
        QString model() const;
        QString effort() const;
        bool setEffort(const QString& effort);
        void probe();
        QJsonObject request(const QJsonArray& messages, const QJsonArray& tools);
        void cancel();

    signals:
        void progress(const QString& text, qint64 elapsed_ms, const QString& activity);
        void completed(const mirrorfly::OfficeAiModelReply& reply);
        void probeCompleted(const QString& status);

    private:
        QUrl endpoint(const QString& suffix) const;
        void read(QNetworkReply* reply);
        void complete(QNetworkReply* reply);

        QNetworkAccessManager network_;
        QNetworkAccessManager* transport_;
        QPointer<QNetworkReply> reply_;
        QUrl base_url_;
        QString model_;
        QByteArray key_;
        QString effort_;
        OfficeAiStream stream_;
        QElapsedTimer clock_;
        QElapsedTimer ui_clock_;
        qint64 first_packet_ms_ = -1;
        QTimer deadline_;
        bool probing_ = false;
    };
}
