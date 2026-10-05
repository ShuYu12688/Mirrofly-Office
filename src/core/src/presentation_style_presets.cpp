#include <mirrorfly/presentation.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    const std::map<std::string, std::vector<double>> dash_patterns{{"solid", {}}, {"dot", {1, 3}},
        {"dash", {4, 3}}, {"lgDash", {8, 3}}, {"dashDot", {4, 3, 1, 3}}, {"lgDashDot", {8, 3, 1, 3}},
        {"lgDashDotDot", {8, 3, 1, 3, 1, 3}}, {"sysDot", {1, 1}}, {"sysDash", {3, 1}},
        {"sysDashDot", {3, 1, 1, 1}}, {"sysDashDotDot", {3, 1, 1, 1, 1, 1}}};
}

namespace mirrorfly
{
    const std::vector<std::string>& presentation_pattern_presets()
    {
        static const std::vector<std::string> patterns{"pct5", "pct10", "pct25", "pct50", "pct75", "pct90",
            "horz", "vert", "dnDiag", "upDiag", "cross", "diagCross", "smGrid", "lgGrid", "smCheck",
            "lgCheck"};
        return patterns;
    }

    bool presentation_pattern_supported(const std::string& pattern)
    {
        const auto& patterns = presentation_pattern_presets();
        return pattern.empty() || std::find(patterns.begin(), patterns.end(), pattern) != patterns.end();
    }

    const std::vector<std::string>& presentation_line_dash_presets()
    {
        static const std::vector<std::string> presets{"solid", "dot", "dash", "lgDash", "dashDot",
            "lgDashDot", "lgDashDotDot", "sysDot", "sysDash", "sysDashDot", "sysDashDotDot"};
        return presets;
    }

    std::vector<double> presentation_line_dash_pattern(const std::string& preset)
    {
        const auto found = dash_patterns.find(preset);
        return found != dash_patterns.end() ? found->second : std::vector<double>{};
    }

    std::string presentation_line_dash_name(const std::vector<double>& pattern)
    {
        for (const auto& [preset, values] : dash_patterns)
            if (pattern.size() == values.size() &&
                std::equal(pattern.begin(), pattern.end(), values.begin(), [](double left, double right)
            {
                return std::abs(left - right) < 1e-6;
            }))
                return preset;
        return "custom";
    }
}
