#include "office_ai_run.hpp"

#include <QSet>

namespace mirrorfly
{
    void OfficeAiRun::begin()
    {
        cancel();
        phase_ = Phase::Ready;
    }

    void OfficeAiRun::cancel()
    {
        ++epoch_;
        phase_ = Phase::Idle;
        calls_ = {};
        index_ = 0;
    }

    bool OfficeAiRun::request()
    {
        if (phase_ != Phase::Ready)
            return false;
        phase_ = Phase::Model;
        return true;
    }

    bool OfficeAiRun::accept(const QJsonArray& calls)
    {
        if (phase_ != Phase::Model)
            return false;
        QSet<QString> ids;
        for (const auto& value : calls)
        {
            const auto call = value.toObject();
            const auto id = call.value("id").toString();
            if (id.isEmpty() || ids.contains(id) || call.value("name").toString().isEmpty() ||
                !call.value("input").isObject())
                return false;
            ids.insert(id);
        }
        calls_ = calls;
        index_ = 0;
        phase_ = calls.isEmpty() ? Phase::Ready : Phase::Tools;
        return true;
    }

    QJsonObject OfficeAiRun::current() const
    {
        return phase_ == Phase::Tools && index_ < calls_.size() ? calls_.at(index_).toObject()
                                                                : QJsonObject{};
    }

    bool OfficeAiRun::resolve(const QString& id)
    {
        if (phase_ != Phase::Tools || current().value("id") != id)
            return false;
        ++index_;
        if (index_ == calls_.size())
        {
            calls_ = {};
            phase_ = Phase::Ready;
        }
        return true;
    }

    OfficeAiRun::Phase OfficeAiRun::phase() const
    {
        return phase_;
    }

    quint64 OfficeAiRun::epoch() const
    {
        return epoch_;
    }
}
