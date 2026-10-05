#include "mindmap_placement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace mirrorfly
{
    bool place_mindmap_node(const MindMapDocument& document, MindMapNode& node)
    {
        constexpr double gap = 16;
        constexpr double limit = 48000;
        if (!std::isfinite(node.x) || !std::isfinite(node.y) || !std::isfinite(node.width) ||
            !std::isfinite(node.height) || node.x < 0 || node.y < 0 || node.x > limit || node.y > limit ||
            node.width < 64 || node.width > 1000 || node.height < 48 || node.height > 800)
            return false;

        // A nearest free rectangle touches an obstacle boundary or the requested position.
        std::vector<double> rows{node.y, 0, limit};
        for (const auto& other : document.nodes)
        {
            if (other.id == node.id)
                continue;
            rows.push_back(std::clamp(other.y + other.height + gap, 0.0, limit));
            rows.push_back(std::clamp(other.y - node.height - gap, 0.0, limit));
        }
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        const double requested_x = node.x, requested_y = node.y;
        std::sort(rows.begin(), rows.end(), [requested_y](double a, double b)
        {
            const auto da = std::abs(a - requested_y), db = std::abs(b - requested_y);
            return da == db ? a < b : da < db;
        });
        double best = std::numeric_limits<double>::infinity();
        std::vector<std::pair<double, double>> intervals;
        for (const double y : rows)
        {
            const double dy = y - requested_y;
            if (dy * dy > best)
                break;
            intervals.clear();
            for (const auto& other : document.nodes)
            {
                if (other.id != node.id && y < other.y + other.height + gap &&
                    y + node.height + gap > other.y)
                    intervals.emplace_back(other.x - node.width - gap, other.x + other.width + gap);
            }
            std::sort(intervals.begin(), intervals.end());
            std::size_t count = 0;
            for (const auto interval : intervals)
            {
                if (count && interval.first < intervals[count - 1].second)
                    intervals[count - 1].second = std::max(intervals[count - 1].second, interval.second);
                else
                    intervals[count++] = interval;
            }
            double x = requested_x;
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto [left, right] = intervals[i];
                if (x > left && x < right)
                {
                    if (left >= 0 && (right > limit || x - left <= right - x))
                        x = left;
                    else
                        x = right;
                    break;
                }
            }
            if (x < 0 || x > limit)
                continue;
            const double distance = (x - requested_x) * (x - requested_x) + dy * dy;
            if (distance < best)
            {
                best = distance;
                node.x = x;
                node.y = y;
            }
        }
        return std::isfinite(best);
    }
}
