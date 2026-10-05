#include "presentation_media_session.hpp"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace mirrorfly
{
    PresentationMediaSession::PresentationMediaSession(QObject* parent) : QObject(parent)
    {
        timer_.setTimerType(Qt::PreciseTimer);
        timer_.setInterval(7);
        connect(&timer_, &QTimer::timeout, this, &PresentationMediaSession::tick);
    }

    void PresentationMediaSession::clear()
    {
        timer_.stop();
        tracks_.clear();
        emit frameChanged();
        emit stateChanged();
    }

    void PresentationMediaSession::stopAll()
    {
        clear();
    }

    void PresentationMediaSession::resetCurrentSlide()
    {
        bool changed = false;
        for (auto item = tracks_.begin(); item != tracks_.end();)
            if (item->first.first == slide_)
            {
                item = tracks_.erase(item);
                changed = true;
            }
            else
                ++item;
        if (tracks_.empty())
            timer_.stop();
        if (changed)
        {
            emit frameChanged();
            emit stateChanged();
        }
    }

    void PresentationMediaSession::setDocument(const RenderPresentationPtr& document, int slide)
    {
        if (document == document_ && slide == slide_)
            return;
        if (document != document_)
            clear();
        document_ = document;
        slide_ = slide;
        bool changed = false;
        for (auto item = tracks_.begin(); item != tracks_.end();)
        {
            const int origin = item->first.first;
            if (slide_ <= origin || slide_ >= origin + item->second.slide_count)
            {
                item = tracks_.erase(item);
                changed = true;
            }
            else
                ++item;
        }
        if (tracks_.empty())
            timer_.stop();
        if (changed)
        {
            emit frameChanged();
            emit stateChanged();
        }
    }

    void PresentationMediaSession::setEnabled(bool enabled)
    {
        if (enabled_ == enabled)
            return;
        enabled_ = enabled;
        if (!enabled)
            clear();
    }

    const PresentationShape* PresentationMediaSession::mediaShape(int shape) const
    {
        if (!document_ || !document_->scene || slide_ < 0 ||
            slide_ >= static_cast<int>(document_->scene->slides.size()))
            return nullptr;
        const auto& shapes = document_->scene->slides[slide_].shapes;
        if (shape < 0 || shape >= static_cast<int>(shapes.size()) || shapes[shape].media_path.empty())
            return nullptr;
        return &shapes[shape];
    }

    QVariantList PresentationMediaSession::items() const
    {
        QVariantList result;
        if (!document_ || !document_->scene || slide_ < 0 ||
            slide_ >= static_cast<int>(document_->scene->slides.size()))
            return result;
        const auto& shapes = document_->scene->slides[slide_].shapes;
        for (std::size_t index = 0; index < shapes.size(); ++index)
        {
            const auto& shape = shapes[index];
            if (shape.media_path.empty())
                continue;
            QVariantList transform;
            for (double value : shape.transform)
                transform.append(value);
            result.append(
                QVariantMap{{"index", static_cast<int>(index)}, {"name", QString::fromStdString(shape.name)},
                    {"width", shape.width}, {"height", shape.height}, {"transform", transform}});
        }
        return result;
    }

    QVariantMap PresentationMediaSession::states() const
    {
        QVariantMap result;
        for (const auto& [key, track] : tracks_)
        {
            const auto& state = track.state;
            const QString name = key.first == slide_
                ? QString::number(key.second)
                : QStringLiteral("carried:%1:%2").arg(key.first).arg(key.second);
            result.insert(name,
                QVariantMap{{"ready", state.ready}, {"playing", state.playing}, {"ended", state.ended},
                    {"position", state.position}, {"duration", state.duration}, {"volume", track.volume},
                    {"loop", track.loop}, {"error", QString::fromStdString(state.error)}});
        }
        return result;
    }

    bool PresentationMediaSession::hasVisibleFrame() const
    {
        for (const auto& [key, track] : tracks_)
            if (key.first == slide_ && !track.frame.isNull())
                return true;
        return false;
    }

    QImage PresentationMediaSession::frame(int shape) const
    {
        const auto found = tracks_.find({slide_, shape});
        return found == tracks_.end() ? QImage{} : found->second.frame;
    }

    bool PresentationMediaSession::open(int shape, Track& track)
    {
        const auto* object = mediaShape(shape);
        if (!object)
            return false;
        const PresentationMedia* media = nullptr;
        for (const auto& candidate : document_->scene->media)
            if (candidate.path == object->media_path)
                media = &candidate;
        if (!media || !media->bytes || media->bytes->empty())
        {
            track.state.error = "此媒体没有可播放的包内数据。";
            return false;
        }
        track.directory = std::make_unique<QTemporaryDir>();
        QString suffix = QFileInfo(QString::fromStdString(media->path)).suffix().toLower();
        if (suffix.size() > 8 || suffix.contains('/') || suffix.contains('\\'))
            suffix = "bin";
        const auto path = track.directory->filePath("clip." + suffix);
        QFile output(path);
        if (!track.directory->isValid() || !output.open(QIODevice::WriteOnly) ||
            output.write(media->bytes->data(), static_cast<qint64>(media->bytes->size())) !=
                static_cast<qint64>(media->bytes->size()))
        {
            track.state.error = "无法准备媒体临时文件。";
            return false;
        }
        output.close();
        const auto opened = open_presentation_media(path.toUtf8().toStdString());
        track.player = opened.player;
        track.state.error = opened.error;
        if (track.player)
        {
            configure_presentation_media(track.player, track.volume, track.loop);
            timer_.start();
        }
        return track.player != nullptr;
    }

    bool PresentationMediaSession::command(int shape, const QString& action, double value)
    {
        if (!enabled_ || !mediaShape(shape) || !std::isfinite(value) ||
            (action != "play" && action != "pause" && action != "stop" && action != "seek" &&
                action != "volume" && action != "loop" && action != "slides"))
            return false;
        if ((action == "volume" && (value < 0 || value > 1)) ||
            (action == "loop" && value != 0 && value != 1) ||
            (action == "slides" && (value < 1 || value > 100000 || std::floor(value) != value)) ||
            (action == "seek" && (value < 0 || value > 86400)))
            return false;
        const std::pair key{slide_, shape};
        if (action == "stop")
        {
            tracks_.erase(key);
            if (tracks_.empty())
                timer_.stop();
            emit frameChanged();
            emit stateChanged();
            return true;
        }
        auto& track = tracks_[key];
        const auto active = std::count_if(tracks_.begin(), tracks_.end(), [](const auto& item)
        {
            return item.second.player != nullptr;
        });
        if (!track.player && active >= 4)
        {
            track.state.error = "同时最多打开四个媒体，请先停止一个再播放。";
            emit stateChanged();
            return false;
        }
        if (!track.player && !open(shape, track))
        {
            emit stateChanged();
            return false;
        }
        bool success = false;
        track.state = presentation_media_state(track.player);
        if (action == "play")
        {
            if (track.pending_seek && !track.state.ready)
            {
                track.pending_play = true;
                return true;
            }
            if (track.state.ended)
                seek_presentation_media(track.player, 0);
            success = play_presentation_media(track.player, true);
        }
        else if (action == "pause")
        {
            track.pending_play = false;
            success = play_presentation_media(track.player, false);
        }
        else if (action == "seek")
        {
            if (!track.state.ready)
            {
                track.pending_seek = value;
                success = true;
            }
            else
            {
                track.pending_seek.reset();
                success = seek_presentation_media(track.player, value);
            }
        }
        else if (action == "slides")
        {
            track.slide_count = static_cast<int>(value);
            success = true;
        }
        else
        {
            const double volume = action == "volume" ? value : track.volume;
            const bool loop = action == "loop" ? value != 0 : track.loop;
            success = configure_presentation_media(track.player, volume, loop);
            if (success)
            {
                track.volume = volume;
                track.loop = loop;
            }
        }
        track.state = presentation_media_state(track.player);
        emit stateChanged();
        return success;
    }

    void PresentationMediaSession::tick()
    {
        bool changed = false;
        for (auto& [key, track] : tracks_)
        {
            Q_UNUSED(key);
            if (!track.player)
                continue;
            track.state = presentation_media_state(track.player);
            if (track.pending_seek && track.state.ready)
            {
                const auto position = std::min(*track.pending_seek, track.state.duration);
                if (seek_presentation_media(track.player, position))
                {
                    track.pending_seek.reset();
                    if (track.pending_play)
                        play_presentation_media(track.player, true);
                    track.pending_play = false;
                }
            }
            auto frame = presentation_media_frame(track.player);
            if (!frame.bgra.empty())
            {
                const QImage image(
                    frame.bgra.data(), frame.width, frame.height, frame.width * 4, QImage::Format_ARGB32);
                track.frame = image.copy();
                changed = true;
            }
            track.state = presentation_media_state(track.player);
        }
        if (changed)
            emit frameChanged();
        if (++state_tick_ % 4 == 0)
            emit stateChanged();
    }
}
