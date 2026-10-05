#pragma once

#include <mirrorfly/presentation.hpp>

#include <pugixml.hpp>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace mirrorfly::presentation_parse_package
{
    using Node = pugi::xml_node;

    struct Failure
    {
        PresentationError error;
        std::string message;
    };

    struct Relationship
    {
        std::string type;
        std::string target;
        bool external = false;
        std::string fragment;
    };

    struct Package
    {
        std::map<std::string, PresentationPart*> parts;
        std::map<std::string, std::unique_ptr<pugi::xml_document>> documents;
        std::unordered_map<const pugi::xml_node_struct*, std::string> document_paths;
        std::map<std::string, std::map<std::string, Relationship>> relations;
        std::size_t xml_nodes = 0;
        std::size_t shape_count = 0;
        std::size_t text_bytes = 0;
        std::size_t geometry_bytes = 0;
        std::set<std::string> used_images;
        std::set<std::string> used_media;
    };

    std::string local_name(const char* name);
    Node child(Node parent, const std::string& name);
    pugi::xml_attribute attribute(Node node, const std::string& name);
    double number(pugi::xml_attribute value, double fallback = 0);
    bool ends_with(const std::string& value, const std::string& suffix);
    bool valid_part_path(const std::string& path);
    std::string resolve_target(const std::string& source, const std::string& target);
    Node xml(Package& package, const std::string& path, bool required = true);
    const std::map<std::string, Relationship>& relationships(Package& package, const std::string& source);
    std::string related(Package& package, const std::string& source, const std::string& type);
}
