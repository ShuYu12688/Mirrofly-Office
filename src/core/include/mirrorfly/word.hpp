#pragma once

#include <mirrorfly/office_package.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mirrorfly
{
    constexpr std::size_t maximum_word_text_bytes = 2 * 1024 * 1024;
    constexpr std::size_t maximum_word_paragraphs = 32768;
    constexpr std::size_t maximum_word_paragraph_bytes = 8192;
    constexpr std::size_t maximum_word_archive_bytes = 256 * 1024 * 1024;
    constexpr std::size_t maximum_word_expanded_bytes = 512 * 1024 * 1024;
    constexpr std::size_t maximum_word_part_bytes = 32 * 1024 * 1024;
    constexpr std::size_t maximum_word_xml_bytes = 32 * 1024 * 1024;
    constexpr double maximum_word_indent_points = 504;
    constexpr double maximum_word_spacing_points = 144;

    enum class WordListKind
    {
        None,
        Bullet,
        Numbered
    };

    enum class WordTemplateKind
    {
        SourceRecord,
        MeetingMinutes,
        WeeklyReport
    };

    struct WordRun
    {
        std::string text;
        std::string font = "Microsoft YaHei";
        std::string east_asia_font = "Microsoft YaHei";
        double size = 12;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strike = false;
        // -1: subscript, 0: baseline, 1: superscript.
        int script = 0;
        std::string color;
        std::string background;
        std::string border_color;
        double character_spacing = 0;
        std::string ruby;
        bool outline = false;
        // Package-local image occurrence; zero denotes text, never an external URL.
        std::uint64_t image_id = 0;
        std::uint64_t source_id = 0;
        // Line variants apply only while the corresponding legacy underline/strike flag is enabled.
        bool double_underline = false;
        bool double_strike = false;
    };

    struct WordTabStop
    {
        double position = 0;
        std::string alignment = "left";
        std::string leader = "none";
    };

    struct WordParagraph
    {
        std::vector<WordRun> runs;
        int heading = 0;
        // 0: left, 1: center, 2: right, 3: justified, 4: distributed.
        int alignment = 0;
        // Multiplier when line_spacing_rule is zero; ignored by point-based rules.
        double line_spacing = 1.5;
        WordListKind list = WordListKind::None;
        int list_level = 0;
        // Paragraph indentation and before/after spacing use points (1/72 inch).
        double left_indent = 0;
        double first_line_indent = 0;
        double space_before = 0;
        double space_after = 5;
        std::string background;
        std::string border_color;
        bool border_bottom_only = false;
        bool right_to_left = false;
        std::uint64_t source_id = 0;
        bool page_break_before = false;
        bool keep_with_next = false;
        double right_indent = 0;
        // 0: proportional, 1: exact points, 2: minimum points.
        int line_spacing_rule = 0;
        double line_spacing_points = 0;
        // New paragraphs may inherit package-local formatting from a split source paragraph.
        // Zero source_id denotes a new paragraph; origin_id never identifies a second source node.
        std::uint64_t origin_id = 0;
        // Resolved package-local numbering instance; zero for new, unnumbered paragraphs.
        // An imported instance is retained when only the level changes.
        int list_instance = 0;
        // Resolved common marker: empty uses the list kind's default. Unknown formats retain their name.
        std::string list_marker;
        int list_start = 1;
        // OOXML level text; empty selects the standard marker for this level.
        std::string list_text;
        // Effective custom stops, in ascending point positions; clear overrides are resolved on import.
        std::vector<WordTabStop> tabs;
    };

    struct WordBlock
    {
        enum class Kind
        {
            Paragraph,
            Table
        };
        Kind kind = Kind::Paragraph;
        std::size_t index = 0;
    };

    enum class WordBorderEdge
    {
        Left,
        Top,
        Right,
        Bottom
    };

    struct WordBorder
    {
        // OOXML line style; empty/none inherit, nil explicitly suppresses a border in Word.
        std::string style;
        std::string color = "#000000";
        double width = 0.5;
        // Resolved cell/style border takes precedence over a table border on a shared edge.
        bool cell_specific = false;

        bool operator==(const WordBorder& other) const;
        bool operator!=(const WordBorder& other) const;
    };

    struct WordTableCell
    {
        std::size_t row = 0;
        std::size_t column = 0;
        std::size_t row_span = 1;
        std::size_t column_span = 1;
        std::vector<WordBlock> blocks;
        std::string background;
        int vertical_alignment = 0;
        std::array<double, 4> margins{3.5, 3.5, 3.5, 3.5};
        // Left/top/right/bottom for each physical row of a vertical merge, in source order.
        // Empty for legacy in-memory tables; imported tables have exactly row_span entries.
        std::vector<std::array<WordBorder, 4>> border_rows;
    };

    struct WordTable
    {
        std::uint64_t source_id = 0;
        std::vector<double> column_widths;
        std::vector<WordTableCell> cells;
        std::size_t rows = 0;
        std::size_t header_rows = 0;
        double left_indent = 0;
        std::string border_color = "#808080";
        double border_width = 0.5;
        // Left/top/right/bottom/insideH/insideV. Legacy uniform fields describe the top edge.
        std::array<WordBorder, 6> borders;
        bool right_to_left = false;
        bool border_layout_supported = true;
    };

    struct WordImage
    {
        std::uint64_t id = 0;
        std::string path;
        std::string mime_type;
        std::string description;
        std::shared_ptr<const std::string> bytes;
        // Display box dimensions in points (1/72 inch), independent of source pixel resolution.
        double width = 0;
        double height = 0;
        // Fractions cropped from left, top, right and bottom of the source image.
        std::array<double, 4> crop{};
        // Clockwise degrees; source-axis flips are applied before rotation.
        double rotation = 0;
        bool flip_horizontal = false;
        bool flip_vertical = false;
        bool anchored = false;
        bool behind_text = false;
    };

    struct WordSection
    {
        std::size_t first_paragraph = 0;
        std::size_t paragraph_count = 0;
        double width = 595.3;
        double height = 841.9;
        std::array<double, 4> margins{72, 72, 72, 72};
        bool continuous = false;
    };

    struct WordPackageState;

    struct WordDocument
    {
        std::vector<WordParagraph> paragraphs{{}};
        std::vector<WordBlock> blocks;
        std::vector<WordTable> tables;
        std::vector<WordImage> images;
        std::vector<WordSection> sections;
        std::vector<std::string> warnings;
        std::shared_ptr<const WordPackageState> source_package;
        double default_tab_stop = 36;
    };

    struct WordResult
    {
        bool success = false;
        std::string error;
        WordDocument document;
        std::vector<OfficePart> parts;
        std::string path;
        std::string revision;
    };

    struct WordEditResult
    {
        bool success = false;
        bool changed = false;
        std::string error;
        std::size_t inserted_paragraphs = 0;
    };

    struct WordParagraphCapabilities
    {
        bool split = false;
        bool remove = false;
        std::string reason;
    };

    bool is_word_path(std::string path);
    std::string word_pinyin(std::uint32_t code_point);
    std::string validate_word(const WordDocument& document);
    // Preserved package images must retain their source paragraph and relative image order.
    std::string validate_word_image_placement(const WordDocument& original, const WordDocument& current);
    WordParagraphCapabilities word_paragraph_capabilities(const WordDocument& document, std::size_t index);
    WordEditResult insert_word_template(
        WordDocument& document, std::size_t paragraph_index, WordTemplateKind kind);
    WordEditResult set_word_cell_border(WordDocument& document, std::size_t table_index,
        std::size_t cell_index, WordBorderEdge edge, WordBorder border);
    // Collapsed shared edge, with first/second in document reading order. Does not mutate source borders.
    WordBorder resolve_word_border(const WordBorder& first, const WordBorder& second);
    WordResult parse_word(std::vector<OfficePart> parts);
    WordResult serialize_word(const WordDocument& document);
}
