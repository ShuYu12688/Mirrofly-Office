#include "presentation_playback.hpp"

#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    PresentationPlayback::PresentationPlayback(QObject* parent) : QObject(parent)
    {
        timer_.setTimerType(Qt::PreciseTimer);
        timer_.setInterval(7);
        connect(&timer_, &QTimer::timeout, this, [this]
        {
            dispatchMedia();
            if (time() >= endTime())
                timer_.stop();
            emit changed();
        });
    }

    void PresentationPlayback::setAnimations(const std::vector<PresentationAnimation>& animations)
    {
        animations_ = animations;
        clicks_ = 0;
        for (const auto& animation : animations_)
            if (animation.trigger.empty())
                clicks_ = std::max(clicks_, animation.click);
        for (const auto& cue : media_cues_)
            if (cue.trigger.empty())
                clicks_ = std::max(clicks_, cue.click);
        restart();
    }

    void PresentationPlayback::setMediaCues(const std::vector<PresentationMediaCue>& cues)
    {
        media_cues_ = cues;
        media_fired_.assign(cues.size(), false);
    }

    void PresentationPlayback::setMediaEnabled(bool enabled)
    {
        media_enabled_ = enabled;
        if (enabled)
            dispatchMedia();
    }

    void PresentationPlayback::dispatchMedia()
    {
        if (!enabled_ || !media_enabled_)
            return;
        const double now = time();
        std::vector<std::pair<double, std::size_t>> pending;
        for (std::size_t index = 0; index < media_cues_.size(); ++index)
        {
            const auto& cue = media_cues_[index];
            const std::vector<double>* times = &click_times_;
            if (!cue.trigger.empty())
            {
                const auto found = trigger_times_.find(cue.trigger);
                times = found == trigger_times_.end() ? nullptr : &found->second;
            }
            if (media_fired_[index] || !times || cue.click < 0 ||
                static_cast<std::size_t>(cue.click) >= times->size() || !std::isfinite(cue.delay) ||
                cue.delay < 0)
                continue;
            const double start = (*times)[cue.click] + cue.delay;
            if (start <= now)
                pending.emplace_back(start, index);
        }
        std::stable_sort(pending.begin(), pending.end());
        for (const auto& [start, index] : pending)
        {
            media_fired_[index] = true;
            const auto& cue = media_cues_[index];
            double position = cue.position;
            if (position >= 0 && cue.action == "play")
                position += now - start;
            emit mediaCue(QString::fromStdString(cue.target), QString::fromStdString(cue.action), position,
                cue.volume, cue.loop, cue.slide_count);
        }
    }

    void PresentationPlayback::setEnabled(bool enabled)
    {
        if (enabled == enabled_)
            return;
        enabled_ = enabled;
        restart();
    }

    void PresentationPlayback::setGroups(
        const std::vector<PresentationGroupFrame>& groups, const std::string& source_part)
    {
        groups_.clear();
        for (const auto& group : groups)
        {
            if (group.source_part != source_part || group.source_id.empty())
                continue;
            PresentationShape frame;
            frame.transform = group.transform;
            frame.width = group.width;
            frame.height = group.height;
            groups_.emplace(group.source_id, std::move(frame));
        }
    }

    const PresentationShape* PresentationPlayback::group(const std::string& id) const
    {
        const auto found = groups_.find(id);
        return found == groups_.end() ? nullptr : &found->second;
    }

    bool PresentationPlayback::enabled() const
    {
        return enabled_;
    }

    double PresentationPlayback::time() const
    {
        return offset_ + (clock_.isValid() && enabled_ ? clock_.elapsed() / 1000.0 : 0);
    }

    double PresentationPlayback::endTime() const
    {
        double end = 0;
        for (const auto& animation : animations_)
        {
            const std::vector<double>* times = &click_times_;
            if (!animation.trigger.empty())
            {
                const auto found = trigger_times_.find(animation.trigger);
                times = found == trigger_times_.end() ? nullptr : &found->second;
            }
            if (times && animation.click >= 0 && static_cast<std::size_t>(animation.click) < times->size())
            {
                const double interval = animation.iterate_interval_percent
                    ? animation.duration * animation.iterate_interval
                    : animation.iterate_interval;
                end = std::max(end,
                    (*times)[animation.click] + animation.delay +
                        animation.duration * animation.repeat * (animation.reverse ? 2 : 1) +
                        std::max(0, animation.iterate_count - 1) * interval);
            }
        }
        for (const auto& cue : media_cues_)
        {
            const std::vector<double>* times = &click_times_;
            if (!cue.trigger.empty())
            {
                const auto found = trigger_times_.find(cue.trigger);
                times = found == trigger_times_.end() ? nullptr : &found->second;
            }
            if (times && cue.click >= 0 && static_cast<std::size_t>(cue.click) < times->size())
                end = std::max(end, (*times)[cue.click] + cue.delay + 0.05);
        }
        return end;
    }

    void PresentationPlayback::startTimer()
    {
        if (enabled_ && time() < endTime())
            timer_.start();
        else
            timer_.stop();
    }

    void PresentationPlayback::restart()
    {
        emit mediaReset();
        media_fired_.assign(media_cues_.size(), false);
        offset_ = 0;
        click_times_ = {0};
        trigger_times_.clear();
        clock_.restart();
        startTimer();
        dispatchMedia();
        emit changed();
    }

    bool PresentationPlayback::advance()
    {
        if (!enabled_ || static_cast<int>(click_times_.size()) > clicks_)
            return false;
        click_times_.push_back(time());
        startTimer();
        dispatchMedia();
        emit changed();
        return true;
    }

    bool PresentationPlayback::trigger(const std::string& target)
    {
        if (!enabled_ || target.empty())
            return false;
        auto& times = trigger_times_[target];
        const int next = static_cast<int>(times.size());
        const bool has_animation =
            std::any_of(animations_.begin(), animations_.end(), [&](const auto& animation)
        {
            return animation.trigger == target && animation.click == next;
        });
        const bool has_media = std::any_of(media_cues_.begin(), media_cues_.end(), [&](const auto& cue)
        {
            return cue.trigger == target && cue.click == next;
        });
        if (!has_animation && !has_media)
        {
            if (times.empty())
                trigger_times_.erase(target);
            return false;
        }
        times.push_back(time());
        startTimer();
        dispatchMedia();
        emit changed();
        return true;
    }

    bool PresentationPlayback::seek(double time)
    {
        if (!enabled_ || !std::isfinite(time) || time < 0 || time > 86400)
            return false;
        if (time < this->time())
        {
            emit mediaReset();
            media_fired_.assign(media_cues_.size(), false);
        }
        offset_ = time;
        clock_.restart();
        startTimer();
        dispatchMedia();
        emit changed();
        return true;
    }

    QVariantMap PresentationPlayback::snapshot() const
    {
        int triggered = 0;
        for (const auto& [target, times] : trigger_times_)
        {
            Q_UNUSED(target);
            triggered += static_cast<int>(times.size());
        }
        return {{QStringLiteral("enabled"), enabled_}, {QStringLiteral("running"), timer_.isActive()},
            {QStringLiteral("click"), static_cast<int>(click_times_.size()) - 1},
            {QStringLiteral("clicks"), clicks_}, {QStringLiteral("elapsed"), time()},
            {QStringLiteral("effects"), static_cast<int>(animations_.size())},
            {QStringLiteral("mediaCues"), static_cast<int>(media_cues_.size())},
            {QStringLiteral("triggers"), triggered}};
    }

    PresentationAnimationState PresentationPlayback::state(
        const std::string& target, double width, double height) const
    {
        if (!enabled_)
            return {};
        return presentation_animation_state(
            animations_, target, {}, time(), click_times_, width, height, trigger_times_);
    }

    PresentationAnimationState PresentationPlayback::paragraphState(
        const std::string& target, int paragraph, double width, double height) const
    {
        if (!enabled_ || paragraph < 0)
            return {};
        return presentation_animation_state(
            animations_, target, {}, time(), click_times_, width, height, trigger_times_, paragraph);
    }

    PresentationAnimationState PresentationPlayback::characterState(const std::string& target,
        const PresentationAnimationTextPosition& position, double width, double height) const
    {
        if (!enabled_ || position.character < 0)
            return {};
        return presentation_animation_state(
            animations_, target, {}, time(), click_times_, width, height, trigger_times_, -1, position);
    }
}
