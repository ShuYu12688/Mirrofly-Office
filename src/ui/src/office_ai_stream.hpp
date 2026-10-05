#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>

namespace mirrorfly
{
    // Accumulates complete SSE frames; incomplete tool JSON is never executable.
    class OfficeAiStream
    {
    public:
        void append(const QByteArray& bytes);
        bool complete() const;
        bool failed() const;
        QString content() const;
        QString activity() const;
        QJsonObject response() const;

    private:
        void frame(const QByteArray& line);
        QByteArray pending_;
        QJsonObject usage_;
        QMap<int, QJsonObject> calls_;
        QString content_;
        QString reasoning_;
        QString finish_reason_;
        qsizetype bytes_ = 0;
        bool done_ = false;
        bool failed_ = false;
    };
}
