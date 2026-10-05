#pragma once

#include <QTextCursor>
#include <QTextList>
#include <QVariantMap>
#include <map>
#include <mirrorfly/word.hpp>

namespace mirrorfly
{
    QTextListFormat word_list_format(const WordParagraph& paragraph);
    QTextListFormat word_list_format(WordListKind kind, int level);
    WordListKind word_list_kind(const QTextList* list);
    void extract_word_list(const QTextBlock& block, WordParagraph& paragraph);
    bool format_word_list(QTextDocument& document, QTextCursor& cursor, int kind, int level);
    bool format_word_list_variant(
        QTextDocument& document, QTextCursor& cursor, const QString& action, const QVariant& value);
    QVariantMap inspect_word_list(const QTextBlock& block);
    bool rebase_word_lists(QTextDocument& imported, const QTextDocument& destination);
    class WordListBuilder
    {
    public:
        void append(QTextCursor& cursor, const WordParagraph& paragraph);

    private:
        QTextList* current_ = nullptr;
        std::map<std::pair<int, int>, QTextList*> instances_;
    };
}
