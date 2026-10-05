#include "office_ai_settings.hpp"
#include "office_ai_provider.hpp"

#include <mirrorfly/credential_protection.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace mirrorfly
{
    OfficeAiSettings::OfficeAiSettings(QString path) : path_(std::move(path))
    {
    }

    const OfficeAiConfiguration& OfficeAiSettings::configuration() const
    {
        return configuration_;
    }

    bool OfficeAiSettings::load()
    {
        QFile file(path_);
        if (!file.exists())
            return true;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
            return false;
        const QByteArray encrypted = file.readAll();
        auto decrypted = unprotect_current_user_credential(
            std::string_view(encrypted.constData(), static_cast<std::size_t>(encrypted.size())));
        if (!decrypted.ok)
            return false;
        QByteArray plain(decrypted.bytes.data(), static_cast<qsizetype>(decrypted.bytes.size()));
        const QJsonObject object = QJsonDocument::fromJson(plain).object();
        plain.fill(0);
        std::fill(decrypted.bytes.begin(), decrypted.bytes.end(), '\0');
        OfficeAiConfiguration loaded{object.value("address").toString(), object.value("model").toString(),
            object.value("key").toString(), object.value("effort").toString()};
        if (object.value("version").toInt() != 1 ||
            !OfficeAiProvider::validConfiguration(loaded.address, loaded.model, loaded.key, loaded.effort))
            return false;
        configuration_ = std::move(loaded);
        return true;
    }

    bool OfficeAiSettings::save(const OfficeAiConfiguration& configuration)
    {
        if (!OfficeAiProvider::validConfiguration(
                configuration.address, configuration.model, configuration.key, configuration.effort))
            return false;
        const QJsonObject object{{"version", 1}, {"address", configuration.address},
            {"model", configuration.model}, {"key", configuration.key}, {"effort", configuration.effort}};
        QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
        const auto encrypted = protect_current_user_credential(
            std::string_view(json.constData(), static_cast<std::size_t>(json.size())));
        json.fill(0);
        if (!encrypted.ok || !QDir().mkpath(QFileInfo(path_).absolutePath()))
            return false;
        QSaveFile file(path_);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        if (file.write(encrypted.bytes.data(), static_cast<qint64>(encrypted.bytes.size())) !=
                static_cast<qint64>(encrypted.bytes.size()) ||
            !file.commit())
            return false;
        configuration_ = configuration;
        return true;
    }
}
