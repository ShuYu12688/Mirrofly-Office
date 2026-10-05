#include "office_ai_image_library.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <cstring>
#include <iostream>

namespace
{
    class Reply final : public QNetworkReply
    {
    public:
        Reply(const QNetworkRequest& request, QByteArray bytes, QObject* parent)
            : QNetworkReply(parent), bytes_(std::move(bytes))
        {
            setRequest(request);
            setUrl(request.url());
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
            open(QIODevice::ReadOnly);
            QTimer::singleShot(0, this, [this]()
            {
                emit readyRead();
                setFinished(true);
                emit finished();
            });
        }
        void abort() override
        {
        }
        qint64 bytesAvailable() const override
        {
            return bytes_.size() - offset_ + QNetworkReply::bytesAvailable();
        }

    protected:
        qint64 readData(char* target, qint64 maximum) override
        {
            const auto count = qMin(maximum, static_cast<qint64>(bytes_.size()) - offset_);
            if (count <= 0)
                return -1;
            std::memcpy(target, bytes_.constData() + offset_, static_cast<std::size_t>(count));
            offset_ += count;
            return count;
        }

    private:
        QByteArray bytes_;
        qint64 offset_ = 0;
    };

    class Images final : public QNetworkAccessManager
    {
    public:
        int searches = 0;
        QByteArray png;
        bool leaked_key = false;

    protected:
        QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
        {
            leaked_key = leaked_key || request.hasRawHeader("Authorization");
            if (request.url().host() != "api.openverse.org")
                return new Reply(request, png, this);
            const QString id = QStringLiteral("candidate-%1").arg(++searches);
            const QJsonObject candidate{{"id", id}, {"title", id}, {"license", "cc0"},
                {"license_url", "https://creativecommons.org/publicdomain/zero/1.0/"},
                {"url", "https://upload.wikimedia.org/test.png"},
                {"foreign_landing_url", "https://commons.wikimedia.org/wiki/File:Test.png"}};
            return new Reply(
                request, QJsonDocument(QJsonObject{{"results", QJsonArray{candidate}}}).toJson(), this);
        }
    };

    QJsonObject run(const std::function<void(mirrorfly::OfficeAiImageLibrary::Completion)>& operation)
    {
        bool done = false;
        QJsonObject result;
        operation([&](const QJsonObject& value)
        {
            result = value;
            done = true;
        });
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 3000)
        {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        return result;
    }
}

int run_office_ai_image_library_tests(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    Images network;
    QImage image(12, 12, QImage::Format_RGB32);
    image.fill(Qt::green);
    QBuffer buffer(&network.png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    mirrorfly::OfficeAiImageLibrary library(nullptr, &network);
    for (int index = 0; index < 2; ++index)
        run([&](auto complete)
        {
            library.search("forest", complete);
        });
    library.cancel();
    const auto first = run([&](auto complete)
    {
        library.fetch("candidate-1", complete);
    });
    const QString path = first.value("image").toObject().value("imagePath").toString();
    bool passed = first.value("ok").toBool() && QFile::exists(path) && !library.creditFor(path).isEmpty();
    for (int index = 0; index < 96; ++index)
        run([&](auto complete)
        {
            library.search("forest", complete);
        });
    const auto expired = run([&](auto complete)
    {
        library.fetch("candidate-1", complete);
    });
    passed = passed && expired.value("error") == "unknown_image";
    library.reset();
    const auto reset = run([&](auto complete)
    {
        library.fetch("candidate-98", complete);
    });
    passed = passed && reset.value("error") == "unknown_image" && library.creditFor(path).isEmpty() &&
        !network.leaked_key;
    std::cout << "Image candidate retention, bounded expiry, task reset and credential isolation: "
              << (passed ? "passed" : "FAILED") << '\n';
    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_office_ai_image_library_tests(argc, argv);
}
