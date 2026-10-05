#include "office_ai_image_library.hpp"
#include <mirrorfly/build_version.hpp>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>

namespace
{
    QJsonObject failure(const QString& error, const QString& hint = {})
    {
        return {{"ok", false}, {"error", error}, {"hint", hint}};
    }

    bool allowed_media_url(const QUrl& url)
    {
        const QString host = url.host().toLower();
        return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty() &&
            (url.port() == -1 || url.port() == 443) && url.fragment().isEmpty() &&
            (host == "upload.wikimedia.org" || host == "live.staticflickr.com" || host == "cdn.stocksnap.io");
    }

    QNetworkRequest request_for(const QUrl& url)
    {
        QNetworkRequest request(url);
        request.setRawHeader(
            "User-Agent", "MirrorflyOffice/" MIRRORFLY_VERSION_STRING " (desktop image search)");
        request.setTransferTimeout(20000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        return request;
    }
}

namespace mirrorfly
{
    OfficeAiImageLibrary::OfficeAiImageLibrary(QObject* parent, QNetworkAccessManager* transport)
        : QObject(parent), network_(transport ? nullptr : std::make_unique<QNetworkAccessManager>())
    {
        transport_ = transport ? transport : network_.get();
    }

    OfficeAiImageLibrary::~OfficeAiImageLibrary()
    {
        cancel();
    }

    void OfficeAiImageLibrary::cancel()
    {
        ++epoch_;
        if (reply_)
        {
            auto* reply = reply_.data();
            reply_ = nullptr;
            disconnect(reply, nullptr, this, nullptr);
            reply->abort();
            reply->deleteLater();
        }
    }

    void OfficeAiImageLibrary::reset()
    {
        cancel();
        candidates_.clear();
        candidate_order_.clear();
        credits_.clear();
    }

    void OfficeAiImageLibrary::search(const QString& query, Completion completion)
    {
        if (reply_)
        {
            completion(failure("image_request_busy"));
            return;
        }
        const QString term = query.trimmed();
        if (term.size() < 2 || term.size() > 100)
        {
            completion(failure("invalid_image_query", "Use a short visual search phrase."));
            return;
        }
        QUrl url("https://api.openverse.org/v1/images/");
        QUrlQuery parameters;
        parameters.addQueryItem("q", term);
        parameters.addQueryItem("license", "cc0,pdm");
        parameters.addQueryItem("page_size", "12");
        url.setQuery(parameters);
        const auto epoch = epoch_;
        auto* reply = transport_->get(request_for(url));
        reply_ = reply;
        connect(reply, &QNetworkReply::finished, this,
            [this, reply, epoch, completion = std::move(completion)]()
        {
            if (epoch != epoch_ || reply_ != reply)
                return;
            reply_ = nullptr;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto bytes = reply->readAll();
            const bool valid_response =
                reply->error() == QNetworkReply::NoError && status == 200 && bytes.size() <= 512 * 1024;
            reply->deleteLater();
            if (!valid_response)
            {
                completion(failure("image_search_unavailable",
                    QStringLiteral("Openverse search failed (HTTP %1); continue without an image.")
                        .arg(status)));
                return;
            }
            const auto response = QJsonDocument::fromJson(bytes).object();
            QHash<QString, QJsonObject> candidates;
            QJsonArray results;
            for (const auto& value : response.value("results").toArray())
            {
                const auto entry = value.toObject();
                const QString id = entry.value("id").toString();
                const QString license = entry.value("license").toString();
                const QUrl media(entry.value("url").toString());
                const QUrl source(entry.value("foreign_landing_url").toString());
                if (id.isEmpty() || (license != "cc0" && license != "pdm") || !allowed_media_url(media) ||
                    source.scheme() != "https")
                    continue;
                QJsonObject candidate{{"id", id}, {"title", entry.value("title")},
                    {"creator", entry.value("creator")}, {"license", license},
                    {"licenseUrl", entry.value("license_url")}, {"sourceUrl", source.toString()},
                    {"mediaUrl", media.toString()}, {"width", entry.value("width")},
                    {"height", entry.value("height")}};
                candidates.insert(id, candidate);
                candidate.remove("mediaUrl");
                results.append(candidate);
            }
            for (auto it = candidates.begin(); it != candidates.end(); ++it)
            {
                candidates_.insert(it.key(), it.value());
                candidate_order_.removeAll(it.key());
                candidate_order_.append(it.key());
            }
            while (candidate_order_.size() > 96)
                candidates_.remove(candidate_order_.takeFirst());
            completion({{"ok", true}, {"images", results},
                {"hint",
                    "Review the source page and license for the intended use; then fetch one id. "
                    "Search metadata is not a copyright guarantee."}});
        });
    }

    void OfficeAiImageLibrary::fetch(const QString& id, Completion completion)
    {
        if (reply_)
        {
            completion(failure("image_request_busy"));
            return;
        }
        const auto candidate = candidates_.value(id);
        if (candidate.isEmpty())
        {
            completion(failure("unknown_image",
                "Use an id returned by the image searches in this task; older results expire after 96 "
                "candidates."));
            return;
        }
        const QUrl url(candidate.value("mediaUrl").toString());
        if (!allowed_media_url(url))
        {
            completion(failure("unsafe_image_url"));
            return;
        }
        const auto epoch = epoch_;
        auto* reply = transport_->get(request_for(url));
        reply_ = reply;
        auto bytes = std::make_shared<QByteArray>();
        connect(reply, &QNetworkReply::readyRead, this, [reply, bytes]()
        {
            bytes->append(reply->readAll());
            if (bytes->size() > 8 * 1024 * 1024)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this,
            [this, reply, epoch, candidate, bytes, completion = std::move(completion)]()
        {
            if (epoch != epoch_ || reply_ != reply)
                return;
            reply_ = nullptr;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            bytes->append(reply->readAll());
            const bool valid_response = reply->error() == QNetworkReply::NoError && status == 200 &&
                !bytes->isEmpty() && bytes->size() <= 8 * 1024 * 1024;
            reply->deleteLater();
            if (!valid_response)
            {
                completion(failure("image_download_failed",
                    QStringLiteral("Image download failed (HTTP %1); choose another result.").arg(status)));
                return;
            }
            QBuffer buffer;
            buffer.setData(*bytes);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            const QByteArray format = reader.format().toLower();
            const QSize size = reader.size();
            if ((format != "jpeg" && format != "png") || !size.isValid() || size.width() > 6000 ||
                size.height() > 6000 || static_cast<qint64>(size.width()) * size.height() > 20000000)
            {
                completion(failure("unsupported_image", "Choose a JPEG or PNG under 20 megapixels."));
                return;
            }
            const QString directory =
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai-images";
            if (!QDir().mkpath(directory))
            {
                completion(failure("image_cache_unavailable"));
                return;
            }
            const QString hash =
                QString::fromLatin1(QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256).toHex());
            const QString path = directory + '/' + hash + (format == "png" ? ".png" : ".jpg");
            if (!QFileInfo::exists(path))
            {
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() || !file.commit())
                {
                    completion(failure("image_cache_unavailable"));
                    return;
                }
            }
            QJsonObject credit = candidate;
            credit.remove("mediaUrl");
            QSaveFile metadata(path + ".json");
            const auto encoded = QJsonDocument(credit).toJson(QJsonDocument::Indented);
            if (metadata.open(QIODevice::WriteOnly) && metadata.write(encoded) == encoded.size())
                metadata.commit();
            credits_.insert(path, credit);
            credit.insert("imagePath", path);
            completion(QJsonObject{{"ok", true}, {"image", credit}});
        });
    }

    QJsonObject OfficeAiImageLibrary::creditFor(const QString& path) const
    {
        return credits_.value(path);
    }
}
