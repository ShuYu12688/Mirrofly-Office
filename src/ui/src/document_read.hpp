#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mirrorfly
{
    struct DocumentReadQuery
    {
        QString view = "overview";
        QString id;
        int index = -1;
        int offset = 0;
        int limit = 8;
        bool valid = true;

        explicit DocumentReadQuery(const QVariantMap& query)
        {
            for (auto it = query.cbegin(); it != query.cend(); ++it)
            {
                if (it.key() == "view" || it.key() == "id")
                {
                    valid = valid && it->metaType().id() == QMetaType::QString;
                    continue;
                }
                if (!QStringList{"index", "offset", "limit"}.contains(it.key()))
                {
                    valid = false;
                    continue;
                }
                const int type = it->metaType().id();
                const double value = it->toDouble();
                valid = valid &&
                    (type == QMetaType::Int || type == QMetaType::Double || type == QMetaType::LongLong ||
                        type == QMetaType::UInt) &&
                    std::isfinite(value) && std::floor(value) == value && value >= -1 &&
                    value <= std::numeric_limits<int>::max() - 2000;
            }
            view = query.value("view", "overview").toString();
            id = query.value("id").toString();
            index = query.value("index", -1).toInt();
            offset = query.value("offset", 0).toInt();
            limit = query.value("limit", view == "text" ? 1600 : 8).toInt();
            valid = valid && offset >= 0 && index >= -1 && limit >= 1 && limit <= 2000 && id.size() <= 1024;
        }
    };

    inline QVariantMap read_error(const QString& error)
    {
        return {{"ok", false}, {"error", error}};
    }

    inline QVariantMap read_text(const QString& text, int offset, int limit)
    {
        if (offset < 0 || offset > text.size() || limit < 1 || limit > 2000 ||
            (offset < text.size() && text.at(offset).isLowSurrogate()))
            return read_error("invalid_text_offset");
        int end = offset + static_cast<int>(std::min<qsizetype>(limit, text.size() - offset));
        if (end < text.size() && end > offset && text.at(end - 1).isHighSurrogate())
            --end;
        if (end == offset && end < text.size())
            end += 2;
        return {{"ok", true}, {"text", text.mid(offset, end - offset)}, {"offset", offset},
            {"nextOffset", end < text.size() ? end : -1}, {"total", text.size()}, {"unit", "UTF-16"}};
    }

    inline QString read_preview(const QString& text, int limit = 160)
    {
        return read_text(text, 0, limit).value("text").toString();
    }

    inline bool append_read_item(QVariantList& items, const QVariantMap& item)
    {
        items.append(item);
        if (QJsonDocument(QJsonArray::fromVariantList(items)).toJson(QJsonDocument::Compact).size() <= 10000)
            return true;
        items.removeLast();
        return false;
    }

    inline QVariantMap read_items(const QVariantList& items, int offset, int total)
    {
        if (offset > total)
            return read_error("invalid_offset");
        if (items.isEmpty() && offset < total)
            return read_error("item_too_large");
        const int next = offset + static_cast<int>(items.size());
        return {{"ok", true}, {"items", items}, {"offset", offset}, {"total", total},
            {"nextOffset", next < total ? next : -1}, {"unit", "items"}};
    }
}
