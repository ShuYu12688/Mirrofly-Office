#include "word_document.hpp"
#include "word_image_resources.hpp"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSemaphore>
#include <QTextBlock>
#include <QTextCursor>
#include <QThread>
#include <QtConcurrentRun>

#include <atomic>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace
{
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << message << '\n';
        }
    }

    bool wait_until(const std::function<bool()>& ready)
    {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        return ready();
    }

    QUrl url(qulonglong id)
    {
        return QUrl(QStringLiteral("mirrorfly-word-image:/%1").arg(id));
    }

    mirrorfly::WordImage source_image(qulonglong id)
    {
        mirrorfly::WordImage image;
        image.id = id;
        image.width = 48;
        image.height = 24;
        image.mime_type = "image/png";
        QImage pixels(64, 32, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::red);
        for (int y = 0; y < pixels.height(); ++y)
            for (int x = 32; x < pixels.width(); ++x)
                pixels.setPixelColor(x, y, Qt::green);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        pixels.save(&buffer, "PNG");
        image.bytes = std::make_shared<const std::string>(bytes.constData(), bytes.size());
        return image;
    }

    void decode_cases()
    {
        auto first = source_image(1);
        auto alias = first;
        alias.id = 2;
        auto cropped = first;
        cropped.id = 3;
        cropped.crop[0] = 0.5;
        cropped.rotation = 90;
        mirrorfly::WordImageResources resources({first, alias, cropped});
        const auto decoded = resources.image(url(1));
        check(decoded.size() == QSize(64, 32) && decoded.pixelColor(1, 1) == QColor(Qt::red),
            "package PNG decodes without accessing a file or network");
        check(resources.image(url(2)).cacheKey() == decoded.cacheKey(),
            "duplicate occurrences share the decoded pixels");
        const auto transformed = resources.image(url(3));
        check(transformed.size() == QSize(32, 32) && transformed.pixelColor(1, 1) == QColor(Qt::green),
            "crop and rotation have a distinct cache identity without enlarging tiny images to 2048 pixels");
        for (const auto& invalid : {"file:///1", "https://example.com/1", "mirrorfly-word-image:/0",
                 "mirrorfly-word-image:/01", "mirrorfly-word-image:/1?q=x", "mirrorfly-word-image:/1#x",
                 "mirrorfly-word-image://host/1", "mirrorfly-word-image:/999"})
            check(resources.request(QUrl(QString::fromLatin1(invalid))).isNull(),
                "non-package and malformed image requests are rejected");
    }

    void async_cases()
    {
        auto first = source_image(1);
        auto alias = first;
        alias.id = 2;
        auto other = first;
        other.id = 3;
        other.rotation = 90;
        std::atomic_int started{0};
        std::atomic_bool worker_thread{true};
        QSemaphore gate;
        auto* owner = QThread::currentThread();
        mirrorfly::WordImageResources resources({first, alias, other}, nullptr, [&](const auto&)
        {
            worker_thread.store(worker_thread.load() && QThread::currentThread() != owner);
            ++started;
            gate.acquire();
            QImage result(32, 32, QImage::Format_ARGB32_Premultiplied);
            result.fill(Qt::red);
            return result;
        });
        std::set<qulonglong> ready;
        bool owner_completion = true;
        QObject::connect(&resources, &mirrorfly::WordImageResources::imageReady, &resources,
            [&](const QUrl& name)
        {
            owner_completion = owner_completion && QThread::currentThread() == owner;
            ready.insert(name.path().mid(1).toULongLong());
        });
        QImage pending;
        std::thread render([&]()
        {
            pending = resources.request(url(1));
            resources.request(url(2));
            resources.request(url(3));
        });
        render.join();
        check(pending.size() == QSize(1, 1), "render-thread requests return a placeholder immediately");
        check(wait_until(
                  [&]()
        {
            return started == 2;
        }),
            "two unique images start on the bounded pool");
        for (int count = 0; count < 20; ++count)
        {
            resources.request(url(1));
            QCoreApplication::processEvents();
        }
        check(started == 2 && ready.empty(), "pending requests coalesce while the owner thread stays free");
        gate.release(2);
        check(wait_until(
                  [&]()
        {
            return ready.size() == 3;
        }),
            "all duplicate occurrences are notified");
        check(owner_completion && worker_thread, "decode and completion execute on their correct threads");
        check(resources.request(url(1)).size() == QSize(32, 32) &&
                resources.request(url(1)).cacheKey() == resources.request(url(2)).cacheKey(),
            "completed requests expose the same cached pixels to rendering");

        int failures_decoded = 0;
        bool failed_ready = false;
        mirrorfly::WordImageResources broken({first}, nullptr, [&](const auto&) -> QImage
        {
            ++failures_decoded;
            throw std::runtime_error("bad image");
        });
        QObject::connect(&broken, &mirrorfly::WordImageResources::imageReady, &broken, [&]()
        {
            failed_ready = true;
        });
        broken.request(url(1));
        check(wait_until(
                  [&]()
        {
            return failed_ready;
        }),
            "decoder exceptions become a failed resource");
        check(broken.request(url(1)).isNull() && broken.request(url(1)).isNull() && failures_decoded == 1,
            "failed image results are cached rather than retried each frame");
    }

    void bounded_cases()
    {
        std::vector<mirrorfly::WordImage> images;
        for (qulonglong id = 1; id <= 200; ++id)
            images.push_back(source_image(id));
        int ready = 0;
        mirrorfly::WordImageResources resources(images, nullptr, [](const auto&)
        {
            QImage result(4, 4, QImage::Format_ARGB32_Premultiplied);
            result.fill(Qt::blue);
            return result;
        });
        QObject::connect(&resources, &mirrorfly::WordImageResources::imageReady, &resources, [&]()
        {
            ++ready;
        });
        for (const auto& image : images)
            resources.request(url(image.id));
        check(wait_until(
                  [&]()
        {
            return ready >= 64;
        }) && ready == 64,
            "rapid scroll queues at most the latest 64 unique images");
        check(resources.request(url(200)).size() == QSize(4, 4), "the latest viewport is preferred");
        resources.request(url(1));
        check(wait_until(
                  [&]()
        {
            return ready == 65;
        }),
            "a dropped offscreen request can be retried");

        int decoded = 0;
        mirrorfly::WordImageResources large(images, nullptr, [&](const auto&)
        {
            ++decoded;
            QImage result(2048, 2048, QImage::Format_ARGB32_Premultiplied);
            result.fill(Qt::blue);
            return result;
        });
        large.image(url(1));
        large.image(url(2));
        large.image(url(3));
        large.image(url(2));
        check(decoded == 3, "recent images remain in the 32 MiB pixel cache");
        large.image(url(1));
        check(decoded == 4, "old pixels are evicted when the bounded cache is full");
    }

    void prepared_cases()
    {
        std::vector<mirrorfly::WordImage> images;
        for (qulonglong id = 1; id <= 13; ++id)
            images.push_back(source_image(id));
        auto alias = images.front();
        alias.id = 14;
        images.push_back(alias);
        int decoded = 0, notifications = 0;
        mirrorfly::WordImageResources resources(images, nullptr, [&](const auto& source)
        {
            ++decoded;
            if (source.id == 13)
                throw std::runtime_error("unsupported test image");
            QImage result(2048, 2048, QImage::Format_ARGB32_Premultiplied);
            result.fill(Qt::red);
            return result;
        });
        QObject::connect(&resources, &mirrorfly::WordImageResources::imageReady, &resources, [&]()
        {
            ++notifications;
        });
        resources.request(url(1));
        std::size_t previous = 0, reports = 0;
        check(resources.prepare(
                  [&](std::size_t completed, std::size_t total)
        {
            check(total == 13 && completed >= previous && completed <= total,
                "preparation progress counts unique image representations monotonically");
            previous = completed;
            ++reports;
        }),
            "prepare all previews before attachment");
        check(decoded == 13 && previous == 13 && reports == 14 &&
                resources.preparedBytes() <= 128 * 1024 * 1024 &&
                resources.preparedBytes() > 32 * 1024 * 1024,
            "complete previews fit the document budget even beyond the old LRU capacity");
        const auto first_key = resources.request(url(1)).cacheKey();
        for (int pass = 0; pass < 3; ++pass)
            for (int id = 14; id > 0; --id)
                check(id == 13 ? resources.request(url(id)).isNull() : !resources.request(url(id)).isNull(),
                    "revisiting prepared images never returns a transient placeholder");
        QCoreApplication::processEvents();
        check(resources.prepare() && decoded == 13 && notifications == 0 && !resources.pending() &&
                resources.request(url(1)).cacheKey() == first_key &&
                resources.request(url(14)).cacheKey() == first_key,
            "preparation cancels queued placeholders and browsing does not decode or invalidate content");
    }

    void lifetime_cases()
    {
        QSemaphore gate;
        std::atomic_int started{0}, finished{0}, stale{0};
        int notifications = 0;
        auto first = source_image(1), second = source_image(2);
        auto resources = std::make_unique<mirrorfly::WordImageResources>(
            std::vector<mirrorfly::WordImage>{first, second}, nullptr, [&](const auto&)
        {
            ++started;
            gate.acquire();
            ++finished;
            return QImage{};
        });
        QObject::connect(resources.get(), &mirrorfly::WordImageResources::imageReady,
            QCoreApplication::instance(), [&]()
        {
            ++notifications;
        });
        resources->request(url(1));
        resources->request(url(2));
        check(wait_until(
                  [&]()
        {
            return started == 2;
        }),
            "old document has two active decodes");
        auto queued = std::make_unique<mirrorfly::WordImageResources>(
            std::vector<mirrorfly::WordImage>{first}, nullptr, [&](const auto&)
        {
            ++stale;
            return QImage{};
        });
        queued->request(url(1));
        QCoreApplication::processEvents();
        QElapsedTimer timer;
        timer.start();
        queued.reset();
        resources.reset();
        check(timer.elapsed() < 100, "closing a document never waits for a running decode");
        gate.release(2);
        check(wait_until(
                  [&]()
        {
            return finished == 2;
        }),
            "in-flight workers release their private inputs");

        bool next_ready = false;
        mirrorfly::WordImageResources next({first});
        QObject::connect(&next, &mirrorfly::WordImageResources::imageReady, &next, [&]()
        {
            next_ready = true;
        });
        next.request(url(1));
        check(wait_until(
                  [&]()
        {
            return next_ready;
        }),
            "a newly opened document loads after cancellation");
        check(stale == 0 && notifications == 0, "closed documents have no queued work or late callbacks");
    }

    void document_cases()
    {
        mirrorfly::WordDocument source;
        source.paragraphs.resize(3);
        source.images.push_back(source_image(1));
        source.paragraphs[0].runs = {{"before"}};
        source.paragraphs[1].runs = {{""}};
        source.paragraphs[1].runs[0].image_id = 1;
        source.paragraphs[2].runs = {{"after"}};
        auto* owner = QThread::currentThread();
        auto prepared = QtConcurrent::run([source, owner]()
        {
            auto document = mirrorfly::create_word_document(source, 400, true);
            document->resource(QTextDocument::ImageResource, url(1));
            document->moveToThread(owner);
            return std::shared_ptr<QTextDocument>(std::move(document));
        });
        prepared.waitForFinished();
        auto document = prepared.result();
        check(document->thread() == owner, "prepared document and queued image requests move together");
        QTextCursor cursor(document->begin().next());
        cursor.insertText("prefix");
        cursor.insertBlock();
        document->undo();
        document->redo();
        const auto before = document->toPlainText();
        const auto geometry = document->documentLayout()->documentSize();
        const auto undo_steps = document->availableUndoSteps();
        document->setModified(false);
        int changes = 0, updated = 0, repaint_notifications = 0;
        bool target_only = true;
        QObject::connect(document.get(), &QTextDocument::contentsChanged, document.get(), [&]()
        {
            if (mirrorfly::word_image_refresh_in_progress(*document))
                ++repaint_notifications;
            else
                ++changes;
        });
        QObject::connect(document->documentLayout(), &QAbstractTextDocumentLayout::updateBlock,
            document.get(), [&](const QTextBlock& block)
        {
            ++updated;
            target_only = target_only && block.text().contains(QChar::ObjectReplacementCharacter);
        });
        check(wait_until(
                  [&]()
        {
            return updated > 0 && repaint_notifications > 0;
        }),
            "completion refreshes an image moved by split and undo");
        check(target_only && changes == 0 && !document->isModified() &&
                document->availableUndoSteps() == undo_steps && document->toPlainText() == before &&
                document->documentLayout()->documentSize() == geometry,
            "image refresh targets only its block without layout, dirty-state or undo changes");
        check(
            document->resource(QTextDocument::ImageResource, url(1)).value<QImage>().size() == QSize(64, 32),
            "QTextDocument does not retain the asynchronous placeholder as a permanent resource");
        auto synchronous = mirrorfly::create_word_document(source, 400);
        check(synchronous->resource(QTextDocument::ImageResource, url(1)).value<QImage>().size() ==
                QSize(64, 32),
            "export documents retain complete synchronous rendering without an event loop");
    }
}

int run_word_image_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    decode_cases();
    async_cases();
    bounded_cases();
    prepared_cases();
    lifetime_cases();
    document_cases();
    return failures ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_word_image_tests(argc, argv);
}
