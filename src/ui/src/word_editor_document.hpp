#pragma once

#include "word_image_resources.hpp"

#include <QTextBlock>
#include <QTextDocument>

namespace mirrorfly
{
    class WordEditorDocument final : public QTextDocument
    {
    public:
        explicit WordEditorDocument(const WordDocument& document, bool asynchronous_images);
        void watchImages();
        bool refreshingImages() const;
        bool takeImageStructureChange();
        const QList<QTextBlock>& decorationBlocks() const;

        WordDocument source;

    protected:
        QVariant loadResource(int type, const QUrl& url) override;

    private:
        void indexImages(int position, int added);
        void trackImagePositions(int position, int removed, int added);
        void refreshImage(const QUrl& url);
        void indexDecorations(int position, int added);

        WordImageResources images_;
        bool asynchronous_images_;
        bool refresh_pending_ = false;
        bool refreshing_images_ = false;
        bool image_structure_changed_ = false;
        std::map<int, std::pair<qulonglong, qulonglong>> image_positions_;
        std::map<QString, QList<QTextBlock>> image_blocks_;
        QList<QTextBlock> decoration_blocks_;
    };

    // Plain QTextDocument clones fall back to scanning; live editors index only changed paragraphs.
    QList<QTextBlock> word_decoration_blocks(const QTextDocument& document);
}
