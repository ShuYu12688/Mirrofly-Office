#pragma once

#include "presentation_scene.hpp"

#include <mirrorfly/presentation_media.hpp>

#include <QObject>
#include <QTemporaryDir>
#include <QTimer>

#include <map>

namespace mirrorfly
{
    class PresentationMediaSession final : public QObject
    {
        Q_OBJECT
    public:
        explicit PresentationMediaSession(QObject* parent = nullptr);
        void setDocument(const RenderPresentationPtr& document, int slide);
        void setEnabled(bool enabled);
        void stopAll();
        void resetCurrentSlide();
        QVariantList items() const;
        QVariantMap states() const;
        QImage frame(int shape) const;
        bool hasVisibleFrame() const;
        bool command(int shape, const QString& action, double value = 0);
    signals:
        void frameChanged();
        void stateChanged();

    private:
        struct Track
        {
            std::unique_ptr<QTemporaryDir> directory;
            PresentationMediaPlayerPtr player;
            QImage frame;
            PresentationMediaState state;
            double volume = 1;
            bool loop = false;
            int slide_count = 1;
            std::optional<double> pending_seek;
            bool pending_play = false;
        };
        const PresentationShape* mediaShape(int shape) const;
        bool open(int shape, Track& track);
        void tick();
        void clear();
        RenderPresentationPtr document_;
        int slide_ = -1;
        bool enabled_ = false;
        QTimer timer_;
        std::map<std::pair<int, int>, Track> tracks_;
        int state_tick_ = 0;
    };
}
