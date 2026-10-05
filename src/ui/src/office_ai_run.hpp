#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace mirrorfly
{
    // One tool exchange at a time. Every accepted call receives exactly one result before another request.
    class OfficeAiRun
    {
    public:
        enum class Phase
        {
            Idle,
            Ready,
            Model,
            Tools
        };

        void begin();
        void cancel();
        bool request();
        bool accept(const QJsonArray& calls);
        QJsonObject current() const;
        bool resolve(const QString& id);
        Phase phase() const;
        quint64 epoch() const;

    private:
        Phase phase_ = Phase::Idle;
        QJsonArray calls_;
        int index_ = 0;
        quint64 epoch_ = 0;
    };
}
