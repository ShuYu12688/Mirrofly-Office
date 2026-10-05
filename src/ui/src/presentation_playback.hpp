#pragma once

#include <mirrorfly/presentation.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <map>

namespace mirrorfly
{
    class PresentationPlayback : public QObject
    {
        Q_OBJECT

    public:
        explicit PresentationPlayback(QObject* parent = nullptr);
        void setAnimations(const std::vector<PresentationAnimation>& animations);
        void setMediaCues(const std::vector<PresentationMediaCue>& cues);
        void setMediaEnabled(bool enabled);
        void setGroups(const std::vector<PresentationGroupFrame>& groups, const std::string& source_part);
        const PresentationShape* group(const std::string& id) const;
        void setEnabled(bool enabled);
        bool enabled() const;
        void restart();
        bool advance();
        bool trigger(const std::string& target);
        bool seek(double time);
        QVariantMap snapshot() const;
        PresentationAnimationState state(const std::string& target, double width, double height) const;
        PresentationAnimationState paragraphState(
            const std::string& target, int paragraph, double width, double height) const;
        PresentationAnimationState characterState(const std::string& target,
            const PresentationAnimationTextPosition& position, double width, double height) const;

    signals:
        void changed();
        void mediaReset();
        void mediaCue(const QString& target, const QString& action, double position, double volume, bool loop,
            int slideCount);

    private:
        double time() const;
        double endTime() const;
        void startTimer();
        void dispatchMedia();
        bool enabled_ = false;
        bool media_enabled_ = false;
        int clicks_ = 0;
        double offset_ = 0;
        QElapsedTimer clock_;
        QTimer timer_;
        std::vector<double> click_times_{0};
        std::map<std::string, std::vector<double>> trigger_times_;
        std::vector<PresentationAnimation> animations_;
        std::vector<PresentationMediaCue> media_cues_;
        std::vector<bool> media_fired_;
        std::map<std::string, PresentationShape> groups_;
    };
}
