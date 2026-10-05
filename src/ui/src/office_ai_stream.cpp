#include "office_ai_stream.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace mirrorfly
{
    void OfficeAiStream::append(const QByteArray& bytes)
    {
        bytes_ += bytes.size();
        if (bytes_ > 8 * 1024 * 1024)
            failed_ = true;
        if (failed_)
            return;
        pending_ += bytes;
        qsizetype newline;
        while ((newline = pending_.indexOf('\n')) >= 0)
        {
            const auto line = pending_.left(newline).trimmed();
            pending_.remove(0, newline + 1);
            if (line.startsWith("data:"))
                frame(line.mid(5).trimmed());
        }
    }

    void OfficeAiStream::frame(const QByteArray& data)
    {
        if (data == "[DONE]")
        {
            done_ = true;
            return;
        }
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(data, &error);
        if (done_ || error.error != QJsonParseError::NoError || !parsed.isObject())
        {
            failed_ = true;
            return;
        }
        const auto object = parsed.object();
        if (object.contains("error"))
            failed_ = true;
        if (object.value("usage").isObject())
            usage_ = object.value("usage").toObject();
        for (const auto& value : object.value("choices").toArray())
        {
            const auto choice = value.toObject();
            if (choice.value("index").toInt() != 0)
                continue;
            if (choice.value("finish_reason").isString())
                finish_reason_ = choice.value("finish_reason").toString();
            const auto delta = choice.value("delta").toObject();
            content_ += delta.value("content").toString();
            reasoning_ += delta.value("reasoning_content").toString();
            for (const auto& call_value : delta.value("tool_calls").toArray())
            {
                const auto call = call_value.toObject();
                const auto index = call.value("index");
                if (!index.isDouble() || index.toInt(-1) < 0)
                {
                    failed_ = true;
                    continue;
                }
                auto& target = calls_[index.toInt()];
                if (call.contains("id"))
                {
                    const auto id = call.value("id");
                    if (!id.isString() || id.toString().isEmpty() ||
                        (target.contains("id") && target.value("id") != id))
                    {
                        failed_ = true;
                        continue;
                    }
                    target.insert("id", id);
                }
                if (call.contains("type"))
                {
                    if (call.value("type") != "function" ||
                        (target.contains("type") && target.value("type") != call.value("type")))
                    {
                        failed_ = true;
                        continue;
                    }
                    target.insert("type", call.value("type"));
                }
                auto function = target.value("function").toObject();
                const auto fragment = call.value("function").toObject();
                for (const auto* key : {"name", "arguments"})
                    function.insert(key, function.value(key).toString() + fragment.value(key).toString());
                target.insert("function", function);
            }
        }
    }

    bool OfficeAiStream::complete() const
    {
        return done_ && !failed_ && !finish_reason_.isEmpty();
    }

    bool OfficeAiStream::failed() const
    {
        return failed_;
    }

    QString OfficeAiStream::content() const
    {
        return content_;
    }

    QString OfficeAiStream::activity() const
    {
        if (!calls_.isEmpty())
            return QStringLiteral("planning_tools");
        if (!content_.isEmpty())
            return QStringLiteral("responding");
        if (!reasoning_.isEmpty())
            return QStringLiteral("thinking");
        return QStringLiteral("waiting");
    }

    QJsonObject OfficeAiStream::response() const
    {
        if (!complete())
            return {};
        QJsonArray calls;
        for (const auto& call : calls_)
            calls.append(call);
        const QJsonObject message{{"role", "assistant"}, {"content", content_},
            {"reasoning_content", reasoning_}, {"tool_calls", calls}};
        const QJsonArray choices{QJsonObject{{"message", message}, {"finish_reason", finish_reason_}}};
        return {{"choices", choices}, {"usage", usage_}};
    }
}
