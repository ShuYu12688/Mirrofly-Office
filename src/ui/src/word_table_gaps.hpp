#pragma once

#include <mirrorfly/word.hpp>

class QTextCursor;
class QTextTable;

namespace mirrorfly
{
    void mark_word_table_gaps(QTextTable& table, const WordTable& source);
    bool word_table_gap(const QTextCursor& cursor);
}
