#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace mirrorfly
{
    class OfficeAiImageLibrary final : public QObject
    {
    public:
        using Completion = std::function<void(const QJsonObject&)>;

        explicit OfficeAiImageLibrary(QObject* parent = nullptr, QNetworkAccessManager* transport = nullptr);
        ~OfficeAiImageLibrary() override;
        void cancel();
        void reset();
        void search(const QString& query, Completion completion);
        void fetch(const QString& id, Completion completion);
        QJsonObject creditFor(const QString& path) const;

    private:
        std::unique_ptr<QNetworkAccessManager> network_;
        QNetworkAccessManager* transport_ = nullptr;
        QPointer<QNetworkReply> reply_;
        QHash<QString, QJsonObject> candidates_;
        QStringList candidate_order_;
        QHash<QString, QJsonObject> credits_;
        unsigned long long epoch_ = 0;
    };
}
