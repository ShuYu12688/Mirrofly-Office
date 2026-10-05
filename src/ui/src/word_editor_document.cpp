#include "word_editor_document.hpp"
#include "word_annotations.hpp"
#include "word_format_properties.hpp"
#include "word_tabs.hpp"

#include <QAbstractTextDocumentLayout>
#include <QPointer>
#include <QTextFragment>

#include <algorithm>

namespace
{
    bool has_decoration(const QTextBlock& block)
    {
        using namespace mirrorfly;
        if (word_has_tab_decoration(block))
            return true;
        if (!block.blockFormat().property(word_paragraph_border_property).toString().isEmpty())
            return true;
        for (auto it = block.begin(); !it.atEnd(); ++it)
        {
            const auto format = it.fragment().charFormat();
            if (format.property(word_double_underline_property).toBool() ||
                format.property(word_double_strike_property).toBool() ||
                !format.property(word_character_border_property).toString().isEmpty() ||
                (!format.property(word_ruby_property).toString().isEmpty() &&
                    !format.property(word_ruby_base_property).toString().isEmpty()))
                return true;
        }
        return false;
    }

    bool contains_image(const QTextBlock& block, const QString& name)
    {
        for (auto it = block.begin(); !it.atEnd(); ++it)
        {
            const auto format = it.fragment().charFormat();
            if (format.isImageFormat() && format.toImageFormat().name() == name)
                return true;
        }
        return false;
    }
}

namespace mirrorfly
{
    WordEditorDocument::WordEditorDocument(const WordDocument& document, bool asynchronous_images)
        : source(document), images_(document.images, this), asynchronous_images_(asynchronous_images)
    {
    }

    QVariant WordEditorDocument::loadResource(int type, const QUrl& url)
    {
        if (type == QTextDocument::ImageResource && url.scheme() == "mirrorfly-word-image")
            return asynchronous_images_ ? images_.request(url) : images_.image(url);
        return QByteArray{};
    }

    void WordEditorDocument::watchImages()
    {
        indexDecorations(0, characterCount());
        trackImagePositions(0, 0, characterCount());
        image_structure_changed_ = false;
        if (asynchronous_images_)
            indexImages(0, characterCount());
        connect(this, &QTextDocument::contentsChange, this, [this](int position, int removed, int added)
        {
            indexDecorations(position, added);
            trackImagePositions(position, removed, added);
            if (asynchronous_images_)
                indexImages(position, added);
        });
        if (asynchronous_images_)
            connect(&images_, &WordImageResources::imageReady, this, &WordEditorDocument::refreshImage);
    }

    bool WordEditorDocument::refreshingImages() const
    {
        return refreshing_images_;
    }

    const QList<QTextBlock>& WordEditorDocument::decorationBlocks() const
    {
        return decoration_blocks_;
    }

    void WordEditorDocument::indexDecorations(int position, int added)
    {
        const auto first = findBlock(position);
        const auto last = position + added;
        const auto affected = [&](const QTextBlock& block)
        {
            return !block.isValid() ||
                (first.isValid() && block.position() >= first.position() && block.position() <= last);
        };
        decoration_blocks_.erase(
            std::remove_if(decoration_blocks_.begin(), decoration_blocks_.end(), affected),
            decoration_blocks_.end());
        for (auto block = first; block.isValid() && block.position() <= last; block = block.next())
            if (has_decoration(block))
                decoration_blocks_.append(block);
        std::sort(decoration_blocks_.begin(), decoration_blocks_.end(),
            [](const QTextBlock& left, const QTextBlock& right)
        {
            return left.position() < right.position();
        });
    }

    QList<QTextBlock> word_decoration_blocks(const QTextDocument& document)
    {
        if (const auto* editor = dynamic_cast<const WordEditorDocument*>(&document))
            return editor->decorationBlocks();
        QList<QTextBlock> blocks;
        for (auto block = document.begin(); block.isValid(); block = block.next())
            if (has_decoration(block))
                blocks.append(block);
        return blocks;
    }

    bool WordEditorDocument::takeImageStructureChange()
    {
        const auto changed = image_structure_changed_;
        image_structure_changed_ = false;
        return changed;
    }

    void WordEditorDocument::trackImagePositions(int position, int removed, int added)
    {
        // Keep the old object positions across contentsChange; deleted characters are already gone.
        using Identity = std::pair<qulonglong, qulonglong>;
        std::vector<Identity> before, after;
        std::map<int, Identity> positions;
        for (const auto& item : image_positions_)
        {
            if (item.first >= position && item.first < position + removed)
                before.push_back(item.second);
            else
                positions.emplace(
                    item.first < position ? item.first : item.first + added - removed, item.second);
        }
        for (auto block = findBlock(position); block.isValid() && block.position() < position + added;
            block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto fragment = it.fragment();
                const auto format = fragment.charFormat();
                if (!format.isImageFormat())
                    continue;
                const Identity identity{format.property(word_source_image_property).toULongLong(),
                    block.blockFormat().property(word_source_paragraph_property).toULongLong()};
                for (int index = std::max(position, fragment.position());
                    index < std::min(position + added, fragment.position() + fragment.length()); ++index)
                {
                    positions[index] = identity;
                    after.push_back(identity);
                }
            }
        image_positions_ = std::move(positions);
        image_structure_changed_ = image_structure_changed_ || before != after;
    }

    void WordEditorDocument::indexImages(int position, int added)
    {
        // Track only changed blocks after construction; no whole-document scan for each decoded image.
        for (auto block = findBlock(position); block.isValid() && block.position() <= position + added;
            block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto format = it.fragment().charFormat();
                if (!format.isImageFormat())
                    continue;
                auto& blocks = image_blocks_[format.toImageFormat().name()];
                const auto invalid = [](const auto& item)
                {
                    return !item.isValid();
                };
                blocks.erase(std::remove_if(blocks.begin(), blocks.end(), invalid), blocks.end());
                if (!blocks.contains(block))
                    blocks.push_back(block);
            }
    }

    void WordEditorDocument::refreshImage(const QUrl& url)
    {
        const auto name = url.toString();
        const auto found = image_blocks_.find(name);
        if (found == image_blocks_.end())
            return;
        auto& blocks = found->second;
        const auto invalid = [&name](const auto& block)
        {
            return !block.isValid() || !contains_image(block, name);
        };
        blocks.erase(std::remove_if(blocks.begin(), blocks.end(), invalid), blocks.end());
        const auto affected = blocks;
        const QPointer<WordEditorDocument> alive(this);
        for (const auto& block : affected)
        {
            auto* layout = documentLayout();
            emit layout->updateBlock(block);
            if (!alive)
                return;
        }
        if (affected.isEmpty() || refresh_pending_)
            return;
        refresh_pending_ = true;
        QMetaObject::invokeMethod(this, [this]()
        {
            refresh_pending_ = false;
            // Qt 6.8.3 setTextDocument omits the block-invalidation connection. Coalesce resource
            // notifications through its public content signal instead of invoking private Quick slots.
            // This repaints visible scene nodes, not document layout or application editing state.
            const QPointer<WordEditorDocument> alive(this);
            refreshing_images_ = true;
            emit contentsChanged();
            if (alive)
                refreshing_images_ = false;
        }, Qt::QueuedConnection);
    }
}
