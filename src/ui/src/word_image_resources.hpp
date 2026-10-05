#pragma once

#include <mirrorfly/word.hpp>

#include <QCache>
#include <QImage>
#include <QObject>
#include <QUrl>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>

namespace mirrorfly
{
    class WordImageResources final : public QObject
    {
        Q_OBJECT

    public:
        using Decoder = std::function<QImage(const WordImage&)>;

        explicit WordImageResources(
            const std::vector<WordImage>& images, QObject* parent = nullptr, Decoder decoder = {});
        ~WordImageResources() override;
        // Export and isolated rendering may decode synchronously on their worker thread.
        QImage image(const QUrl& url);
        // Quick may request resources from its render thread; completion belongs to the owner thread.
        QImage request(const QUrl& url);
        bool pending() const;
        // Before attachment, prepare immutable display previews within a document-wide pixel budget.
        // Original package images remain untouched; export keeps its existing decode resolution.
        bool prepare(const std::function<void(std::size_t, std::size_t)>& progress = {});
        qsizetype preparedBytes() const;

    signals:
        void imageReady(const QUrl& url);

    private:
        qulonglong resourceId(const QUrl& url) const;
        void startNext();
        void cacheImage(qulonglong id, const QImage& image);

        std::map<std::uint64_t, WordImage> images_;
        std::map<qulonglong, qulonglong> aliases_;
        std::map<qulonglong, std::vector<qulonglong>> occurrences_;
        Decoder decoder_;
        std::shared_ptr<std::atomic_bool> cancelled_ = std::make_shared<std::atomic_bool>(false);
        mutable std::mutex mutex_;
        QCache<qulonglong, QImage> cache_{32 * 1024};
        std::map<qulonglong, QImage> prepared_;
        bool prepared_complete_ = false;
        std::deque<qulonglong> queue_;
        std::set<qulonglong> requested_;
        bool dispatch_pending_ = false;
        int active_ = 0;
    };
}
