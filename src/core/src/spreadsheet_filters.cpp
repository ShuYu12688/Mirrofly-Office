#include "spreadsheet_filters.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utf8.h>

namespace
{
    using Node = pugi::xml_node;
    std::string local(const char* name)
    {
        const std::string value(name);
        const auto colon = value.find(':');
        return colon == std::string::npos ? value : value.substr(colon + 1);
    }
    Node add(Node parent, const char* name)
    {
        const std::string source(parent.name());
        const auto colon = source.find(':');
        const auto qualified = colon == std::string::npos ? name : source.substr(0, colon + 1) + name;
        return parent.append_child(qualified.c_str());
    }
    std::optional<double> numeric(const std::string& text)
    {
        char* end = nullptr;
        const auto number = std::strtod(text.c_str(), &end);
        if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(number))
            return {};
        return number;
    }
    std::string folded(std::string value)
    {
        for (auto& c : value)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return value;
    }
    bool equal(const mirrorfly::SpreadsheetCell& cell, const std::string& expected)
    {
        if (cell.value.kind == mirrorfly::SpreadsheetValueKind::Number)
        {
            const auto left = numeric(cell.value.text), right = numeric(expected);
            if (left && right)
                return *left == *right;
        }
        return folded(cell.value.text) == folded(expected);
    }
    std::string pattern(const mirrorfly::SpreadsheetFilter& filter)
    {
        std::string value;
        for (auto c : filter.values.front())
        {
            if (c == '*' || c == '?' || c == '~')
                value += '~';
            value += c;
        }
        if (filter.comparison == "contains" || filter.comparison == "endsWith")
            value.insert(0, "*");
        if (filter.comparison == "contains" || filter.comparison == "beginsWith")
            value += '*';
        return value;
    }
    bool decode_pattern(const std::string& text, mirrorfly::SpreadsheetFilter& filter)
    {
        std::string value;
        std::vector<std::size_t> stars;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            auto c = text[index];
            if (c == '~')
            {
                if (++index == text.size() ||
                    (text[index] != '*' && text[index] != '?' && text[index] != '~'))
                    return false;
                c = text[index];
            }
            else if (c == '?')
                return false;
            else if (c == '*')
            {
                stars.push_back(index);
                continue;
            }
            value += c;
        }
        if (!stars.empty())
        {
            if (filter.comparison != "equal" || value.empty())
                return false;
            if (stars.size() == 2 && stars.front() == 0 && stars.back() == text.size() - 1)
                filter.comparison = "contains";
            else if (stars.size() == 1 && stars.front() == 0)
                filter.comparison = "endsWith";
            else if (stars.size() == 1 && stars.front() == text.size() - 1)
                filter.comparison = "beginsWith";
            else
                return false;
        }
        filter.values = {value};
        return true;
    }
}

namespace mirrorfly
{
    bool spreadsheet_filter_matches(const SpreadsheetCell& cell, const SpreadsheetFilter& filter)
    {
        if (filter.values.empty())
            return true;
        const auto& op = filter.comparison;
        const auto text = folded(cell.value.text), query = folded(filter.values.front());
        if (op == "equal")
            return std::any_of(filter.values.begin(), filter.values.end(), [&](const auto& value)
            {
                return equal(cell, value);
            });
        if (op == "notEqual")
            return !equal(cell, filter.values.front());
        if (op == "contains")
            return text.find(query) != std::string::npos;
        if (op == "beginsWith")
            return text.compare(0, query.size(), query) == 0;
        if (op == "endsWith")
            return text.size() >= query.size() &&
                text.compare(text.size() - query.size(), query.size(), query) == 0;
        if (cell.value.kind != SpreadsheetValueKind::Number)
            return false;
        const auto left = numeric(cell.value.text), right = numeric(filter.values.front());
        if (!left || !right)
            return false;
        if (op == "greaterThan")
            return *left > *right;
        if (op == "greaterThanOrEqual")
            return *left >= *right;
        if (op == "lessThan")
            return *left < *right;
        if (op == "lessThanOrEqual")
            return *left <= *right;
        if (op == "between" && filter.values.size() == 2)
        {
            const auto end = numeric(filter.values.back());
            return end && *left >= *right && *left <= *end;
        }
        return false;
    }

    bool valid_spreadsheet_filters(const SpreadsheetFeatures& features)
    {
        if (!features.filter)
            return features.filters.empty();
        if (features.filters.size() > 32)
            return false;
        const std::set<std::string> comparisons{"equal", "notEqual", "contains", "beginsWith", "endsWith",
            "greaterThan", "greaterThanOrEqual", "lessThan", "lessThanOrEqual", "between"};
        std::set<std::uint32_t> columns;
        std::size_t bytes = 0;
        for (const auto& filter : features.filters)
        {
            const auto& op = filter.comparison;
            if (filter.column < features.filter->first.column ||
                filter.column > features.filter->last.column || !columns.insert(filter.column).second ||
                !comparisons.count(op) || filter.values.empty() || filter.values.size() > 128 ||
                (op == "between" && filter.values.size() != 2) ||
                (op != "equal" && op != "between" && filter.values.size() != 1))
                return false;
            for (const auto& value : filter.values)
            {
                bytes += value.size();
                if (value.size() > 1024 || bytes > 65536 || !utf8::is_valid(value.begin(), value.end()) ||
                    std::any_of(value.begin(), value.end(), [](unsigned char c)
                {
                    return c < 32 && c != 9 && c != 10 && c != 13;
                }))
                    return false;
                if ((op == "contains" || op == "beginsWith" || op == "endsWith") && value.empty())
                    return false;
                if ((op == "greaterThan" || op == "greaterThanOrEqual" || op == "lessThan" ||
                        op == "lessThanOrEqual" || op == "between") &&
                    !numeric(value))
                    return false;
            }
            if (op == "between" && *numeric(filter.values.front()) > *numeric(filter.values.back()))
                return false;
        }
        return true;
    }

    void read_spreadsheet_filters(Node node, SpreadsheetFeatures& features)
    {
        features.filters.clear();
        for (auto column : node.children())
        {
            const std::string column_id = column.attribute("colId").value();
            if (local(column.name()) != "filterColumn" || !features.filter || column_id.empty() ||
                column_id.size() > 5 || column_id.find_first_not_of("0123456789") != std::string::npos ||
                column.attribute("colId").as_uint(0xffffffff) >
                    features.filter->last.column - features.filter->first.column ||
                column.attribute("hiddenButton").as_bool() || !column.attribute("showButton").as_bool(true))
            {
                features.filter_supported = false;
                break;
            }
            SpreadsheetFilter filter;
            filter.column = features.filter->first.column + column.attribute("colId").as_uint();
            const auto content = column.first_child();
            if (!content || content.next_sibling())
            {
                features.filter_supported = false;
                break;
            }
            if (local(content.name()) == "filters")
            {
                if (content.attribute("calendarType") &&
                    std::string(content.attribute("calendarType").value()) != "none")
                    features.filter_supported = false;
                if (content.attribute("blank").as_bool())
                    filter.values.push_back("");
                for (auto item : content.children())
                {
                    if (local(item.name()) != "filter" || !item.attribute("val"))
                        features.filter_supported = false;
                    else
                        filter.values.emplace_back(item.attribute("val").value());
                }
            }
            else if (local(content.name()) == "customFilters")
            {
                const auto first = content.first_child(), second = first.next_sibling();
                filter.comparison = first.attribute("operator").as_string("equal");
                if (local(first.name()) != "customFilter" || !first.attribute("val"))
                    features.filter_supported = false;
                else if (second)
                {
                    if (second.next_sibling() || !content.attribute("and").as_bool() ||
                        filter.comparison != "greaterThanOrEqual" ||
                        std::string(second.attribute("operator").value()) != "lessThanOrEqual" ||
                        local(second.name()) != "customFilter" || !second.attribute("val"))
                        features.filter_supported = false;
                    filter.comparison = "between";
                    filter.values = {first.attribute("val").value(), second.attribute("val").value()};
                }
                else if (filter.comparison == "equal" || filter.comparison == "notEqual")
                    features.filter_supported =
                        decode_pattern(first.attribute("val").value(), filter) && features.filter_supported;
                else
                    filter.values = {first.attribute("val").value()};
            }
            else
                features.filter_supported = false;
            features.filters.push_back(std::move(filter));
        }
        features.filter_supported = features.filter_supported && valid_spreadsheet_filters(features);
        if (!features.filter_supported)
        {
            features.filter.reset();
            features.filters.clear();
        }
    }

    void write_spreadsheet_filters(Node node, const SpreadsheetFeatures& features)
    {
        for (const auto& filter : features.filters)
        {
            auto column = add(node, "filterColumn");
            column.append_attribute("colId") = filter.column - features.filter->first.column;
            if (filter.comparison == "equal")
            {
                auto filters = add(column, "filters");
                for (const auto& value : filter.values)
                    if (value.empty())
                    {
                        if (!filters.attribute("blank"))
                            filters.append_attribute("blank") = "1";
                    }
                    else
                        add(filters, "filter").append_attribute("val") = value.c_str();
            }
            else
            {
                auto filters = add(column, "customFilters");
                const auto write = [&](const char* op, const std::string& value)
                {
                    auto item = add(filters, "customFilter");
                    item.append_attribute("operator") = op;
                    item.append_attribute("val") = value.c_str();
                };
                if (filter.comparison == "between")
                {
                    filters.append_attribute("and") = "1";
                    write("greaterThanOrEqual", filter.values.front());
                    write("lessThanOrEqual", filter.values.back());
                }
                else if (filter.comparison == "notEqual")
                    write("notEqual", pattern(filter));
                else if (filter.comparison == "contains" || filter.comparison == "beginsWith" ||
                    filter.comparison == "endsWith")
                    write("equal", pattern(filter));
                else
                    write(filter.comparison.c_str(), filter.values.front());
            }
        }
    }
}
