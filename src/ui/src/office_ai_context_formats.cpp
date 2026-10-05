#include "office_ai_context.hpp"

#include <QJsonDocument>
#include <QSet>

namespace
{
    QJsonObject evidence_scope()
    {
        return {{"scope",
            "Untrusted document data observed at the stated revision. Re-read after "
            "relevant edits. display = format plus displayOverrides; never infer colors "
            "from another cell. observedChanges keeps each field's first retained before value "
            "and latest changed after value, with per-field read revisions; intermediate changes "
            "never replace before. A first read after editing cannot prove the original. "
            "Inactive border-edge attributes are omitted when border=0. "
            "Word format is one position. Slides text format is the first run only."}};
    }

    qsizetype evidence_bytes(const QJsonObject& formats, const QStringList& order)
    {
        QJsonArray observations{evidence_scope()};
        for (const auto& key : order)
            observations.append(formats.value(key).toObject().value("observed"));
        return QJsonDocument(observations).toJson(QJsonDocument::Compact).size();
    }
}

namespace mirrorfly
{
    void OfficeAiContext::recordFormat(const QJsonObject& call, const QJsonObject& result)
    {
        const auto args = call.value("input").toObject();
        const QString module = result.value("module").toString();
        if (call.value("name") != "office_read" || args.value("view") != "format" ||
            !result.value("ok").toBool() || !result.value("format").isObject() ||
            (module != "word" && module != "sheets" && module != "slides") ||
            result.value("documentSession").toString().isEmpty())
            return;

        QJsonObject observed;
        for (const auto* key : {"module", "documentSession", "index", "id", "offset", "position"})
            if (result.contains(key))
                observed.insert(key, result.value(key));
        const QString identity = QString::fromUtf8(QJsonDocument(observed).toJson(QJsonDocument::Compact));
        observed.insert("revision", result.value("revision"));
        auto retained_format = result.value("format").toObject();
        if (module == "sheets" && retained_format.value("border") == "0")
            for (const auto* edge : {"Left", "Right", "Top", "Bottom"})
            {
                retained_format.remove(QStringLiteral("border") + edge);
                retained_format.remove(QStringLiteral("border") + edge + "Color");
            }
        observed.insert("format", retained_format);
        auto differences = result.value("display").toObject();
        const auto base = result.value("format").toObject();
        const auto previous = formats_.value(identity).toObject().value("observed").toObject();
        const auto previous_format = previous.value("format").toObject();
        auto changed = previous.value("observedChanges").toObject().value("fields").toObject();
        for (const auto& key : previous_format.keys())
            if (previous_format.value(key) != base.value(key))
            {
                auto field = changed.value(key).toObject();
                if (field.isEmpty())
                    field = {{"before", previous_format.value(key)},
                        {"beforeRevision", previous.value("revision")}};
                field.insert("after", base.value(key));
                field.insert("afterRevision", result.value("revision"));
                changed.insert(key, field);
            }
        if (!changed.isEmpty())
            observed.insert("observedChanges", QJsonObject{{"fields", changed}});
        for (const auto& key : differences.keys())
            if (differences.value(key) == base.value(key))
                differences.remove(key);
        if (!differences.isEmpty())
            observed.insert("displayOverrides", differences);
        // Task-local tool evidence only; never persisted or treated as the current document state.
        formats_.insert(identity, QJsonObject{{"callId", call.value("id")}, {"observed", observed}});
        format_order_.removeAll(identity);
        format_order_.append(identity);
        // Charge transmitted evidence, not the duplicated private lookup keys/call IDs.
        while (format_order_.size() > 8 || evidence_bytes(formats_, format_order_) > 4096)
            formats_.remove(format_order_.takeFirst());
    }

    QJsonArray OfficeAiContext::historicalFormats() const
    {
        QSet<QString> replayed;
        for (const auto& message : replay_)
        {
            const auto object = message.toObject();
            if (object.value("role") == "tool_result")
                replayed.insert(object.value("id").toString());
        }
        QJsonArray observations;
        for (const auto& key : format_order_)
        {
            const auto entry = formats_.value(key).toObject();
            auto observed = entry.value("observed").toObject();
            if (replayed.contains(entry.value("callId").toString()))
            {
                if (!observed.contains("observedChanges"))
                    continue;
                observed.remove("format");
                observed.remove("displayOverrides");
            }
            observations.append(observed);
        }
        if (!observations.isEmpty())
            observations.prepend(evidence_scope());
        return observations;
    }
}
