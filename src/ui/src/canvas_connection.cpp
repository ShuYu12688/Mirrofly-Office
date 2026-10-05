#include "canvas_bridge.hpp"

#include <algorithm>

namespace mirrorfly
{
    QString CanvasBridge::connectionMode() const
    {
        return connection_mode_;
    }

    QString CanvasBridge::connectionFrom() const
    {
        return connection_from_;
    }

    bool CanvasBridge::beginConnection(const QString& mode, const QString& from)
    {
        if (pdf_ || (mode != "off" && mode != "once" && mode != "continuous"))
            return false;
        if (mode != "off")
        {
            if (!active_ || locked())
                return false;
            const auto& nodes = std::get<MindMapDocument>(document_).nodes;
            if (!from.isEmpty() &&
                std::none_of(nodes.begin(), nodes.end(), [&](const auto& node)
            {
                return node.id == from.toStdString();
            }))
                return false;
        }
        connection_mode_ = mode;
        connection_from_ = mode == "off" ? QString{} : from;
        emit connectionChanged();
        return true;
    }

    bool CanvasBridge::connectNode(const QString& target)
    {
        if (connection_mode_ == "off" || connection_from_.isEmpty())
            return false;
        if (!execute("connect", {{"id", connection_from_}, {"toId", target}}))
            return false;
        return beginConnection(connection_mode_ == "once" ? "off" : "continuous", {});
    }
}
