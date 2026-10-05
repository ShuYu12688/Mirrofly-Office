#pragma once

#include <QString>

namespace mirrorfly
{
    struct OfficeAiConfiguration
    {
        QString address = QStringLiteral("https://api.deepseek.com");
        QString model = QStringLiteral("deepseek-flash");
        QString key;
        QString effort = QStringLiteral("none");
    };

    class OfficeAiSettings final
    {
    public:
        explicit OfficeAiSettings(QString path);
        bool load();
        bool save(const OfficeAiConfiguration& configuration);
        const OfficeAiConfiguration& configuration() const;

    private:
        QString path_;
        OfficeAiConfiguration configuration_;
    };
}
