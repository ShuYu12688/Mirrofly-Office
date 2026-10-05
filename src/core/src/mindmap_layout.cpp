#include "mindmap_layout.hpp"

#include <algorithm>
#include <map>
#include <numeric>

namespace mirrorfly
{
    MindMapStatus layout_mindmap_graph(MindMapDocument& document)
    {
        // Build a spanning forest without changing edges. Cycles and cross-links remain intact.
        const auto count = document.nodes.size();
        std::map<std::string, std::size_t> indices;
        for (std::size_t i = 0; i < count; ++i)
            indices.emplace(document.nodes[i].id, i);
        std::vector<std::vector<std::size_t>> outgoing(count), children(count);
        std::vector<std::size_t> incoming(count), depth(count), order, roots;
        for (const auto& edge : document.edges)
        {
            outgoing[indices.at(edge.from)].push_back(indices.at(edge.to));
            ++incoming[indices.at(edge.to)];
        }
        std::vector<std::size_t> candidates{indices.at(document.root_id)};
        for (std::size_t i = 0; i < count; ++i)
            if (incoming[i] == 0)
                candidates.push_back(i);
        for (std::size_t i = 0; i < count; ++i)
            candidates.push_back(i);
        std::vector<bool> visited(count);
        for (const auto root : candidates)
        {
            if (visited[root])
                continue;
            roots.push_back(root);
            visited[root] = true;
            const auto first = order.size();
            order.push_back(root);
            for (std::size_t cursor = first; cursor < order.size(); ++cursor)
            {
                const auto parent = order[cursor];
                for (const auto child : outgoing[parent])
                {
                    if (visited[child])
                        continue;
                    visited[child] = true;
                    depth[child] = depth[parent] + 1;
                    children[parent].push_back(child);
                    order.push_back(child);
                }
            }
        }
        constexpr double margin = 80;
        constexpr double horizontal_gap = 120;
        constexpr double vertical_gap = 40;
        std::vector<double> spans(count), columns(count, 0), positions(count, margin), top(count, margin);
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            const auto i = *it;
            double span = 0;
            for (const auto child : children[i])
                span += spans[child] + vertical_gap;
            if (!children[i].empty())
                span -= vertical_gap;
            spans[i] = std::max(document.nodes[i].height, span);
            columns[depth[i]] = std::max(columns[depth[i]], document.nodes[i].width);
        }
        for (std::size_t i = 1; i < count; ++i)
            positions[i] = positions[i - 1] + columns[i - 1] + horizontal_gap;
        double next_top = margin;
        for (const auto root : roots)
        {
            top[root] = next_top;
            next_top += spans[root] + vertical_gap * 2;
        }
        auto candidate = document;
        bool changed = false;
        for (const auto i : order)
        {
            auto& node = candidate.nodes[i];
            const double x = positions[depth[i]];
            const double y = top[i] + (spans[i] - node.height) / 2;
            if (x > 48000 || y > 48000)
                return {MindMapError::TooLarge, "分支排版超出画布范围，请拆分导图后重试。", false};
            changed = changed || node.x != x || node.y != y;
            node.x = x;
            node.y = y;
            double child_span = 0;
            for (const auto child : children[i])
                child_span += spans[child] + vertical_gap;
            if (!children[i].empty())
                child_span -= vertical_gap;
            double child_top = top[i] + (spans[i] - child_span) / 2;
            for (const auto child : children[i])
            {
                top[child] = child_top;
                child_top += spans[child] + vertical_gap;
            }
        }
        document = std::move(candidate);
        return {MindMapError::None, {}, changed};
    }
}
