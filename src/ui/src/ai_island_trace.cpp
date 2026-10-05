#include "ai_island_trace.hpp"

#include <QStringList>

namespace mirrorfly
{
    QVariantMap ai_island_trace_entry(const QVariantMap& entry)
    {
        const QString kind = entry.value("kind").toString();
        if (kind == "user" || kind == "assistant")
            return {{"kind", kind}, {"detail", entry.value("detail").toString().left(128000)}};
        if (kind != "tool" && kind != "office")
            return {};

        const QString state = entry.value("state").toString();
        // Keep diagnostics in the agent; the companion only needs an execution receipt.
        return {{"kind", kind}, {"title", entry.value("title").toString().left(160)},
            {"state", QStringList{"running", "done", "error", "stopped"}.contains(state) ? state : "pending"},
            {"elapsedMs", qMax<qint64>(0, entry.value("elapsedMs").toLongLong())}};
    }
}
