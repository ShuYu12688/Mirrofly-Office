#include "presentation_parse_package.hpp"

#include <utf8/checked.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace mirrorfly::presentation_parse_package
{
    constexpr std::size_t maximum_xml_nodes = 300000;

    std::string local_name(const char* name)
    {
        const auto* separator = std::strchr(name, ':');
        return separator ? separator + 1 : name;
    }

    Node child(Node parent, const std::string& name)
    {
        for (auto candidate : parent.children())
        {
            if (local_name(candidate.name()) == name)
            {
                return candidate;
            }
        }
        return {};
    }

    pugi::xml_attribute attribute(Node node, const std::string& name)
    {
        for (auto candidate : node.attributes())
        {
            if (local_name(candidate.name()) == name)
            {
                return candidate;
            }
        }
        return {};
    }

    double number(pugi::xml_attribute value, double fallback)
    {
        if (!value)
        {
            return fallback;
        }
        char* end = nullptr;
        const double parsed = std::strtod(value.value(), &end);
        if (end == value.value() || *end != '\0' || !std::isfinite(parsed) || std::abs(parsed) > 1e12)
        {
            throw Failure{mirrorfly::PresentationError::InvalidXml, "演示文件包含无效数值。"};
        }
        return parsed;
    }

    bool ends_with(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool valid_part_path(const std::string& path)
    {
        if (path.empty() || path.size() > 1024 || path.front() == '/' ||
            path.find_first_of("\\:#?\0", 0, 5) != std::string::npos ||
            !utf8::is_valid(path.begin(), path.end()))
        {
            return false;
        }
        std::size_t start = 0;
        while (start < path.size())
        {
            const auto end = path.find('/', start);
            const auto piece = path.substr(start, end == std::string::npos ? end : end - start);
            if (piece.empty() || piece == "." || piece == "..")
            {
                return false;
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        return path.back() != '/';
    }

    Node xml(Package& package, const std::string& path, bool required)
    {
        if (path.empty())
        {
            return {};
        }
        const auto cached = package.documents.find(path);
        if (cached != package.documents.end())
        {
            return cached->second->document_element();
        }
        const auto found = package.parts.find(path);
        if (found == package.parts.end())
        {
            if (required)
            {
                throw Failure{mirrorfly::PresentationError::MissingPart, "演示文件缺少必要内容：" + path};
            }
            return {};
        }
        const auto& bytes = found->second->bytes;
        if (bytes.size() > mirrorfly::maximum_presentation_xml_bytes)
        {
            throw Failure{mirrorfly::PresentationError::TooLarge, "演示文件的 XML 内容超过限制。"};
        }
        auto document = std::make_unique<pugi::xml_document>();
        const auto loaded =
            document->load_buffer(bytes.data(), bytes.size(), pugi::parse_default | pugi::parse_doctype);
        if (!loaded || !document->document_element())
        {
            throw Failure{mirrorfly::PresentationError::InvalidXml, "无法解析演示文件内容：" + path};
        }
        unsigned roots = 0;
        for (auto top_level : document->children())
        {
            roots += top_level.type() == pugi::node_element ? 1 : 0;
        }
        if (roots != 1)
        {
            throw Failure{mirrorfly::PresentationError::InvalidXml, "XML 内容必须只有一个根元素。"};
        }
        std::vector<std::pair<Node, unsigned>> pending{{*document, 0}};
        while (!pending.empty())
        {
            const auto current = pending.back();
            pending.pop_back();
            if (++package.xml_nodes > maximum_xml_nodes || current.second > 64)
            {
                throw Failure{mirrorfly::PresentationError::TooLarge, "演示文件的 XML 结构过于复杂。"};
            }
            if (current.first.type() == pugi::node_doctype)
            {
                throw Failure{mirrorfly::PresentationError::InvalidXml, "演示文件不允许包含 DTD。"};
            }
            for (auto nested : current.first.children())
            {
                pending.emplace_back(nested, current.second + 1);
            }
        }
        const auto root = document->document_element();
        package.document_paths.emplace(root.root().internal_object(), path);
        package.documents.emplace(path, std::move(document));
        return root;
    }

}
