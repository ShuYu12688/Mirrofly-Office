#include "presentation_media_session.hpp"
#include "presentation_playback.hpp"
#include "slide_renderer.hpp"

#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    bool SlideRenderer::mediaEnabled() const
    {
        return media_enabled_;
    }

    bool SlideRenderer::animationEnabled() const
    {
        return playback_->enabled();
    }

    void SlideRenderer::setAnimationEnabled(bool enabled)
    {
        if (enabled == playback_->enabled())
            return;
        playback_->setEnabled(enabled);
        emit animationEnabledChanged();
    }

    QVariantMap SlideRenderer::animationState() const
    {
        return playback_->snapshot();
    }

    bool SlideRenderer::advanceAnimation()
    {
        return playback_->advance();
    }

    bool SlideRenderer::triggerAnimation(int index)
    {
        if (!playback_->enabled() || !document_ || !document_->scene || slide_index_ < 0 || index < 0 ||
            slide_index_ >= static_cast<int>(document_->scene->slides.size()))
        {
            return false;
        }
        const auto& shapes = document_->scene->slides[slide_index_].shapes;
        if (index >= static_cast<int>(shapes.size()))
        {
            return false;
        }
        const auto& shape = shapes[index];
        if (playback_->trigger(shape.source_id))
        {
            return true;
        }
        for (auto group = shape.source_groups.rbegin(); group != shape.source_groups.rend(); ++group)
        {
            if (playback_->trigger(*group))
            {
                return true;
            }
        }
        return false;
    }

    bool SlideRenderer::triggerAnimationAt(qreal x, qreal y)
    {
        return triggerAnimation(hitTest(x, y));
    }

    void SlideRenderer::restartAnimation()
    {
        if (document_ && document_->scene && slide_index_ >= 0 &&
            slide_index_ < static_cast<int>(document_->scene->slides.size()))
        {
            const auto& slide = document_->scene->slides[slide_index_];
            playback_->setGroups(slide.groups, slide.source_part);
            playback_->setMediaCues(slide.media_cues);
            playback_->setAnimations(document_->scene->slides[slide_index_].animations);
        }
        else
        {
            playback_->setGroups({}, {});
            playback_->setMediaCues({});
            playback_->setAnimations({});
        }
    }

    bool SlideRenderer::seekAnimation(double time)
    {
        return playback_->seek(time);
    }

    bool SlideRenderer::transitionsEnabled() const
    {
        return transitions_enabled_;
    }

    void SlideRenderer::setTransitionsEnabled(bool enabled)
    {
        if (enabled == transitions_enabled_)
        {
            return;
        }
        transitions_enabled_ = enabled;
        if (!enabled)
        {
            finishTransition(true);
        }
        scheduleFrames();
        emit transitionsEnabledChanged();
        emit transitionStateChanged();
    }

    QVariantMap SlideRenderer::transitionState() const
    {
        if (!document_ || !document_->scene || slide_index_ < 0 ||
            slide_index_ >= static_cast<int>(document_->scene->slides.size()))
        {
            return {{QStringLiteral("active"), false}, {QStringLiteral("advanceOnClick"), true},
                {QStringLiteral("advanceAfterMs"), -1}};
        }
        const auto& transition = document_->scene->slides[slide_index_].transition;
        const int duration =
            transition.type.empty() || transition.type == "cut" ? 0 : qRound(transition.duration * 1000);
        return {{QStringLiteral("active"), transition_from_slide_ >= 0},
            {QStringLiteral("type"), QString::fromStdString(transition.type)},
            {QStringLiteral("direction"), QString::fromStdString(transition.direction)},
            {QStringLiteral("orientation"), QString::fromStdString(transition.orientation)},
            {QStringLiteral("durationMs"), duration},
            {QStringLiteral("advanceOnClick"), transition.advance_on_click},
            {QStringLiteral("advanceAfterMs"),
                transition.advance_after < 0 ? -1 : qRound(transition.advance_after * 1000)},
            {QStringLiteral("approximate"), transition.approximate},
            {QStringLiteral("progress"), transition_progress_},
            {QStringLiteral("from"), transition_from_slide_}, {QStringLiteral("to"), slide_index_}};
    }

    bool SlideRenderer::seekTransition(double progress)
    {
        if (transition_from_slide_ < 0 || !std::isfinite(progress) || progress < 0 || progress > 1)
        {
            return false;
        }
        transition_animation_->stop();
        transition_progress_ = progress;
        if (progress >= 1)
        {
            finishTransition(true);
        }
        else
        {
            emit transitionStateChanged();
            update();
        }
        return true;
    }

    bool SlideRenderer::startTransition(int previous_slide)
    {
        if (!transitions_enabled_ || !document_ || !document_->scene || previous_slide < 0 ||
            previous_slide >= static_cast<int>(document_->scene->slides.size()) || slide_index_ < 0 ||
            slide_index_ >= static_cast<int>(document_->scene->slides.size()))
        {
            return false;
        }
        const auto& transition = document_->scene->slides[slide_index_].transition;
        if (transition.type.empty() || transition.type == "cut" || !std::isfinite(transition.duration) ||
            transition.duration <= 0)
        {
            return false;
        }
        transition_from_slide_ = previous_slide;
        transition_progress_ = 0;
        playback_->setGroups({}, {});
        playback_->setMediaCues({});
        playback_->setAnimations({});
        transition_animation_->setDuration(std::clamp(qRound(transition.duration * 1000), 50, 10000));
        transition_animation_->start();
        emit transitionStateChanged();
        update();
        return true;
    }

    void SlideRenderer::finishTransition(bool restart_playback)
    {
        const bool changed = transition_from_slide_ >= 0 || transition_progress_ != 1;
        if (transition_animation_)
        {
            transition_animation_->stop();
        }
        transition_from_slide_ = -1;
        transition_progress_ = 1;
        if (media_)
        {
            media_->setEnabled(media_enabled_);
        }
        if (restart_playback && playback_)
        {
            restartAnimation();
        }
        if (changed)
        {
            emit transitionStateChanged();
            update();
        }
    }

    void SlideRenderer::setMediaEnabled(bool enabled)
    {
        if (enabled == media_enabled_)
            return;
        media_enabled_ = enabled;
        media_->setEnabled(enabled);
        playback_->setMediaEnabled(enabled);
        emit mediaEnabledChanged();
    }
    QVariantList SlideRenderer::mediaItems() const
    {
        return media_->items();
    }
    QVariantMap SlideRenderer::mediaStates() const
    {
        return media_->states();
    }
    bool SlideRenderer::mediaCommand(int shape, const QString& action, double value)
    {
        return media_->command(shape, action, value);
    }

}
