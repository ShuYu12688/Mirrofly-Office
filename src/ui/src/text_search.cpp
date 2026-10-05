#include "text_search.hpp"
#include "document_read.hpp"

namespace mirrorfly
{
    QVariantMap read_text_matches(const QString& source, const QVariantMap& query)
    {
        auto page = query;
        page.remove("text");
        const DocumentReadQuery read(page);
        const auto value = query.value("text");
        const auto needle = value.toString();
        if (!read.valid || read.view != "find" || query.contains("id") || query.contains("index") ||
            value.metaType().id() != QMetaType::QString || needle.isEmpty() || needle.size() > 1024 ||
            !needle.isValidUtf16() || needle.contains(QChar::Null) || read.offset > source.size() ||
            (read.offset < source.size() && source.at(read.offset).isLowSurrogate()))
            return read_error("invalid_find_query");
        QVariantList items;
        int next = read.offset;
        while (items.size() < read.limit)
        {
            const int start = static_cast<int>(source.indexOf(needle, next, Qt::CaseSensitive));
            if (start < 0)
            {
                next = -1;
                break;
            }
            const int end = start + static_cast<int>(needle.size());
            int context_start = std::max(0, start - 24);
            if (context_start > 0 && source.at(context_start).isLowSurrogate())
                --context_start;
            const auto context = read_text(source, context_start, static_cast<int>(needle.size()) + 48);
            if (!append_read_item(items,
                    {{"start", start}, {"end", end}, {"text", needle}, {"context", context.value("text")},
                        {"contextOffset", context_start}}))
            {
                if (items.isEmpty())
                    return read_error("item_too_large");
                break;
            }
            next = end;
        }
        if (next >= 0 && source.indexOf(needle, next, Qt::CaseSensitive) < 0)
            next = -1;
        return {{"ok", true}, {"items", items}, {"offset", read.offset}, {"nextOffset", next},
            {"total", source.size()}, {"unit", "UTF-16"}, {"caseSensitive", true}, {"overlapping", false}};
    }
}
