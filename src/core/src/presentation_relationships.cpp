#include "presentation_parse_package.hpp"

#include <cstring>
#include <utility>
#include <vector>

namespace mirrorfly::presentation_parse_package
{
    namespace
    {
        int hex_digit(unsigned char value)
        {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'A' && value <= 'F')
                return value - 'A' + 10;
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            return -1;
        }

        bool valid_fragment(const std::string& fragment)
        {
            // Keep the leading '#' to distinguish an empty fragment from no fragment.
            for (std::size_t index = 1; index < fragment.size(); ++index)
            {
                const auto value = static_cast<unsigned char>(fragment[index]);
                if (value == '%')
                {
                    if (index + 2 >= fragment.size() || hex_digit(fragment[index + 1]) < 0 ||
                        hex_digit(fragment[index + 2]) < 0)
                        return false;
                    index += 2;
                }
                else if (!(value >= 'a' && value <= 'z') && !(value >= 'A' && value <= 'Z') &&
                    !(value >= '0' && value <= '9') && !std::strchr("-._~!$&'()*+,;=:@/?", value))
                    return false;
            }
            return true;
        }
    }

    std::string resolve_target(const std::string& source, const std::string& target)
    {
        if (target.compare(0, 2, "//") == 0)
            return {};
        std::string decoded;
        for (std::size_t index = 0; index < target.size(); ++index)
        {
            if (target[index] == '%')
            {
                if (index + 2 >= target.size())
                {
                    return {};
                }
                const auto high = hex_digit(target[index + 1]);
                const auto low = hex_digit(target[index + 2]);
                const auto value = high * 16 + low;
                if (high < 0 || low < 0 || value == 0 || value == '/' || value == '\\')
                {
                    return {};
                }
                decoded.push_back(static_cast<char>(value));
                index += 2;
            }
            else
            {
                decoded.push_back(target[index]);
            }
        }
        if (decoded.empty() || decoded.find_first_of("\\:#?\0", 0, 5) != std::string::npos)
        {
            return {};
        }
        const auto separator = source.rfind('/');
        std::string combined = decoded.front() == '/'
            ? decoded.substr(1)
            : (separator == std::string::npos ? "" : source.substr(0, separator + 1)) + decoded;
        std::vector<std::string> pieces;
        for (std::size_t start = 0; start < combined.size();)
        {
            const auto end = combined.find('/', start);
            const auto piece = combined.substr(start, end == std::string::npos ? end : end - start);
            if (piece == "..")
            {
                if (pieces.empty())
                {
                    return {};
                }
                pieces.pop_back();
            }
            else if (piece != "." && !piece.empty())
            {
                pieces.push_back(piece);
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        std::string result;
        for (const auto& piece : pieces)
        {
            result += (result.empty() ? "" : "/") + piece;
        }
        return valid_part_path(result) ? result : std::string{};
    }

    const std::map<std::string, Relationship>& relationships(Package& package, const std::string& source)
    {
        const auto existing = package.relations.find(source);
        if (existing != package.relations.end())
        {
            return existing->second;
        }
        const auto slash = source.rfind('/');
        std::string path = "_rels/.rels";
        if (!source.empty())
        {
            path = (slash == std::string::npos ? "" : source.substr(0, slash + 1)) + "_rels/" +
                source.substr(slash == std::string::npos ? 0 : slash + 1) + ".rels";
        }
        std::map<std::string, Relationship> result;
        const auto root = xml(package, path, false);
        for (auto relation : root.children())
        {
            if (local_name(relation.name()) != "Relationship")
            {
                continue;
            }
            const std::string id = relation.attribute("Id").value();
            Relationship value;
            value.type = relation.attribute("Type").value();
            value.external = std::string(relation.attribute("TargetMode").value()) == "External";
            if (!value.external)
            {
                const std::string target = relation.attribute("Target").value();
                const auto fragment = target.find('#');
                if (ends_with(value.type, "/hyperlink") && fragment != std::string::npos)
                {
                    value.fragment = target.substr(fragment);
                    if (valid_fragment(value.fragment))
                        value.target =
                            fragment == 0 ? source : resolve_target(source, target.substr(0, fragment));
                }
                else
                    value.target = resolve_target(source, target);
            }
            if (id.empty() || result.count(id) || (!value.external && value.target.empty()))
            {
                throw Failure{mirrorfly::PresentationError::InvalidPackage, "演示文件包含无效的内部关联。"};
            }
            result.emplace(id, std::move(value));
        }
        return package.relations.emplace(source, std::move(result)).first->second;
    }

    std::string related(Package& package, const std::string& source, const std::string& type)
    {
        for (const auto& pair : relationships(package, source))
        {
            if (ends_with(pair.second.type, "/" + type) && !pair.second.external)
            {
                return pair.second.target;
            }
        }
        return {};
    }

}
