#pragma once

#include <mirrorfly/word.hpp>

#include <pugixml.hpp>

#include <map>
#include <memory>

namespace mirrorfly::word_detail
{
    class StyleResolver
    {
    public:
        bool load(const std::vector<OfficePart>& parts, std::string& error);
        bool has_styles() const;
        pugi::xml_node paragraph_properties(pugi::xml_node paragraph);
        pugi::xml_node run_properties(pugi::xml_node run);
        std::string theme_color(pugi::xml_node color, bool fill = false) const;
        void table_properties(pugi::xml_node table, pugi::xml_node target) const;
        void cell_properties(pugi::xml_node cell, pugi::xml_node target, bool direct = true);
        void cell_table_properties(pugi::xml_node cell, pugi::xml_node target);

    private:
        struct TableContext
        {
            std::string id;
            std::map<pugi::xml_node, unsigned> regions;
            std::map<pugi::xml_node, unsigned> row_regions;
        };
        std::vector<pugi::xml_node> chain(const std::string& id, const char* type) const;
        pugi::xml_node paragraph_base(const std::string& id, pugi::xml_node paragraph);
        std::string table_id(pugi::xml_node table) const;
        std::pair<std::string, unsigned> table_context(pugi::xml_node node, bool row_wide = false);
        void merge_table_style(pugi::xml_node target, const std::string& id, unsigned regions) const;
        void resolve_theme(pugi::xml_node properties) const;
        void resolve_colors(pugi::xml_node properties) const;
        pugi::xml_document styles_;
        pugi::xml_document theme_;
        pugi::xml_document paragraph_;
        pugi::xml_document run_;
        std::map<std::string, pugi::xml_node> definitions_;
        std::map<std::string, std::unique_ptr<pugi::xml_document>> cache_;
        std::size_t cache_cost_ = 0;
        std::string default_paragraph_;
        std::string default_character_;
        std::string default_table_;
        std::map<pugi::xml_node, TableContext> tables_;
        std::size_t cell_count_ = 0;
        pugi::xml_node run_base_;
    };
}
