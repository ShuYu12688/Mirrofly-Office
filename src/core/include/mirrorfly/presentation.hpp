#pragma once

#include <mirrorfly/office_package.hpp>
#include <mirrorfly/presentation_animation.hpp>
#include <mirrorfly/presentation_geometry.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mirrorfly
{
    struct PresentationPackageState;

    constexpr std::size_t maximum_presentation_archive_bytes = 256 * 1024 * 1024;
    constexpr std::size_t maximum_presentation_expanded_bytes = 768 * 1024 * 1024;
    constexpr std::size_t maximum_presentation_part_bytes = 256 * 1024 * 1024;
    constexpr std::size_t maximum_presentation_xml_bytes = 8 * 1024 * 1024;
    constexpr std::size_t maximum_presentation_parts = 4096;
    constexpr std::size_t maximum_presentation_slides = 200;
    constexpr std::size_t maximum_presentation_shapes = 10000;
    constexpr std::size_t maximum_presentation_text_bytes = 2 * 1024 * 1024;
    constexpr std::size_t maximum_presentation_editable_image_bytes = 48 * 1024 * 1024;

    enum class PresentationError
    {
        None,
        UnsupportedType,
        ReadFailed,
        TooLarge,
        InvalidArchive,
        EncryptedArchive,
        InvalidPackage,
        InvalidXml,
        MissingPart,
        WriteFailed,
        ChangedOnDisk,
        InvalidImage
    };

    using PresentationPart = OfficePart;
    using PresentationParseProgress = std::function<void(std::size_t completed, std::size_t total)>;

    struct PresentationGradientStop
    {
        double position = 0;
        std::string color = "#FFFFFF";
        double opacity = 1;
    };

    struct PresentationFill
    {
        // Empty color and stops mean no fill; nonempty stops describe a linear gradient.
        std::string color;
        double opacity = 1;
        double angle_degrees = 0;
        std::vector<PresentationGradientStop> stops;
        std::string image_path;
        std::array<double, 4> image_crop{0, 0, 0, 0};
        std::array<double, 4> image_fill_rect{0, 0, 0, 0};
        bool image_tile = false;
        std::array<double, 2> image_scale{1, 1};
        std::array<double, 2> image_offset{0, 0};
        double image_dpi = 0;
        std::string image_alignment = "tl";
        std::string image_flip;
        std::string theme_slot;
        std::string theme_reference_color;
        // A nonempty pattern uses color as its background and the following foreground color.
        std::string pattern;
        std::string pattern_foreground_color;
        double pattern_foreground_opacity = 1;
    };

    struct PresentationTextEffects
    {
        std::string outline_color;
        double outline_opacity = 1;
        double outline_width = 0;
        PresentationFill outline_fill;
        std::string shadow_color;
        double shadow_opacity = 0;
        double shadow_blur = 0;
        double shadow_x = 0;
        double shadow_y = 0;
        std::string glow_color;
        double glow_opacity = 0;
        double glow_radius = 0;
        double reflection_opacity = 0;
        double reflection_offset = 0;
        double reflection_end_opacity = 0;
        double reflection_start_position = 0;
        double reflection_end_position = 0.65;
    };

    struct PresentationClickAction
    {
        // Empty means no supported in-presentation click action.
        std::string kind;
        int target_slide = -1;
        bool from_text = false;
    };

    struct PresentationRun
    {
        std::string text;
        // Fields, breaks, tabs and formula approximations stay searchable but cannot be rewritten as text
        // runs.
        bool plain_text = true;
        std::string font_family = "Arial";
        std::string east_asian_font_family;
        std::string font_theme_slot;
        std::string east_asian_theme_slot;
        std::string font_theme_reference;
        std::string east_asian_theme_reference;
        std::string font_family_source = "application";
        std::string east_asian_font_source = "application";
        std::string font_size_source = "application";
        std::string color_source = "application";
        bool local_font_override = false;
        bool local_size_override = false;
        bool local_color_override = false;
        double font_size = 18;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        std::string color = "#000000";
        std::string color_theme_slot;
        std::string color_theme_reference;
        double opacity = 1;
        PresentationFill fill;
        PresentationTextEffects effects;
        double spacing = 0;
        double baseline = 0;
        bool strike = false;
        PresentationClickAction click_action;
    };

    struct PresentationParagraph
    {
        std::vector<PresentationRun> runs;
        std::string alignment = "left";
        std::string bullet;
        std::string bullet_image_path;
        bool numbered = false;
        int number_start = 1;
        int list_level = 0;
        std::string number_format = "arabicPeriod";
        double margin_left = 0;
        double first_line_indent = 0;
        double line_spacing = 1;
        double fixed_line_spacing = 0;
        double space_before = 0;
        double space_after = 0;
        double space_before_percent = -1;
        double space_after_percent = -1;
        std::string bullet_font;
        std::string bullet_color;
        double bullet_opacity = 1;
        double bullet_size_percent = 1;
        double bullet_size_points = 0;
    };

    struct PresentationText
    {
        std::vector<PresentationParagraph> paragraphs;
        double inset_left = 7.2;
        double inset_right = 7.2;
        double inset_top = 3.6;
        double inset_bottom = 3.6;
        std::string vertical_alignment = "top";
        bool wrap = true;
        bool clip_vertical = false;
        bool clip_horizontal = false;
        double font_scale = 1;
        std::string warp;
        double warp_adjustment = 0.25;
        double rotation = 0;
        std::string vertical;
        bool auto_fit = false;
        double line_spacing_reduction = 0;
    };

    struct PresentationLineEnd
    {
        std::string type = "none";
        std::string width = "med";
        std::string length = "med";
    };

    struct PresentationLineStyle
    {
        std::vector<double> dashes;
        std::string cap = "flat";
        std::string join = "round";
        PresentationLineEnd head;
        PresentationLineEnd tail;
    };

    struct PresentationTableStyleOptions
    {
        bool first_row = false;
        bool last_row = false;
        bool first_column = false;
        bool last_column = false;
        bool band_rows = false;
        bool band_columns = false;
    };

    struct PresentationTableStructureOptions
    {
        bool insert_row = false;
        bool insert_column = false;
        bool delete_row = false;
        bool delete_column = false;
        bool merge_right = false;
        bool merge_down = false;
    };

    struct PresentationTableCell
    {
        struct BorderEdge
        {
            std::string color;
            double opacity = 1;
            double width = 0;
            bool local_override = false;
        };

        std::string frame_id;
        std::size_t row = 0;
        std::size_t column = 0;
        std::size_t xml_cell = 0;
        std::size_t row_count = 0;
        std::size_t column_count = 0;
        bool has_merges = false;
        bool style_available = false;
        PresentationTableStyleOptions style_options;
        PresentationTableStructureOptions structure_options;
        std::size_t row_span = 1;
        std::size_t column_span = 1;
        bool unmergeable = false;
        bool local_fill_override = false;
        PresentationFill inherited_fill;
        bool local_border_override = false;
        std::string border_color;
        double border_width = 0;
        std::array<BorderEdge, 4> border_edges;
    };

    struct PresentationOutlineSnapshot
    {
        std::string color;
        PresentationFill fill;
        double opacity = 1;
        double width = 1;
        PresentationLineStyle line_style;
    };

    struct PresentationPlaceholderInfo
    {
        std::string type;
        unsigned index = 0;
        bool has_layout = false;
        bool has_master = false;
        bool local_fill_override = false;
        std::string fill_source;
        std::string inherited_fill_source;
        PresentationFill inherited_fill;
        bool local_outline_override = false;
        std::string outline_source;
        std::string inherited_outline_source;
        PresentationOutlineSnapshot inherited_outline;
    };

    struct PresentationShape
    {
        std::uint64_t id = 0;
        std::string name;
        std::string source_id;
        std::string source_part;
        bool editable = true;
        std::vector<std::string> source_groups;
        std::optional<PresentationTableCell> table_cell;
        std::optional<PresentationPlaceholderInfo> placeholder;
        // Original group coordinate system; source XML patches map slide-space edits back through it.
        std::array<double, 6> source_parent_transform{1, 0, 0, 1, 0, 0};
        // Drawing coordinates are points. The affine matrix maps local coordinates to the slide.
        std::array<double, 6> transform{1, 0, 0, 1, 0, 0};
        double width = 0;
        double height = 0;
        std::string geometry = "rect";
        std::string geometry_definition;
        std::shared_ptr<const PresentationGeometry> path_geometry;
        PresentationFill fill;
        std::string outline_color;
        PresentationFill outline_fill;
        double outline_opacity = 1;
        double outline_width = 1;
        PresentationText text;
        // A nonempty image_path refers to one shared resource in PresentationScene::images.
        std::string image_path;
        std::array<double, 4> image_crop{0, 0, 0, 0};
        double image_opacity = 1;
        std::string media_path;
        PresentationLineStyle line_style;
        PresentationTextEffects effects;
        // Parsed OOXML effect source: direct shape effects take precedence over a theme effectRef.
        std::string effects_source = "none";
        std::uint32_t theme_effect_style_index = 0;
        double soft_edge_radius = 0;
        bool inner_shadow = false;
        bool approximate_3d = false;
        PresentationClickAction click_action;
    };

    struct PresentationImage
    {
        std::string path;
        std::string mime_type;
        std::shared_ptr<const std::string> bytes;

        PresentationImage() = default;
        PresentationImage(std::string image_path, std::string image_mime_type, std::string image_bytes)
        {
            path = std::move(image_path);
            mime_type = std::move(image_mime_type);
            bytes = std::make_shared<const std::string>(std::move(image_bytes));
        }
        PresentationImage(std::string image_path, std::string image_mime_type,
            std::shared_ptr<const std::string> image_bytes)
        {
            path = std::move(image_path);
            mime_type = std::move(image_mime_type);
            bytes = std::move(image_bytes);
        }
    };

    struct PresentationGroupFrame
    {
        std::string source_id;
        std::string source_part;
        std::array<double, 6> transform{1, 0, 0, 1, 0, 0};
        double width = 0;
        double height = 0;
        bool editable = false;
        bool ungroupable = false;
        bool extensible = false;
        int layer_index = -1;
        int layer_count = 0;
    };

    struct PresentationGroupLayerOptions
    {
        bool back = false;
        bool backward = false;
        bool forward = false;
        bool front = false;
    };

    PresentationGroupLayerOptions presentation_group_layer_options(const PresentationGroupFrame& group);

    struct PresentationTransition
    {
        std::string type;
        std::string direction;
        std::string orientation;
        double duration = 0.5;
        bool advance_on_click = true;
        double advance_after = -1;
        bool approximate = false;
        bool editable = true;
    };

    struct PresentationSlide
    {
        std::string title;
        // Authored slides retain their palette until serialized into a linked OOXML theme.
        std::array<std::string, 6> authored_palette;
        bool authored_theme = false;
        bool theme_available = false;
        int theme_index = -1;
        std::string speaker_notes;
        std::string source_part;
        std::string section_id;
        bool hidden = false;
        PresentationFill background{"#FFFFFF"};
        PresentationTransition transition;
        std::vector<PresentationShape> shapes;
        std::vector<PresentationGroupFrame> groups;
        std::vector<PresentationAnimation> animations;
        std::vector<PresentationMediaCue> media_cues;
        std::vector<std::string> warnings;
    };

    struct PresentationMedia
    {
        std::string path;
        std::string mime_type;
        std::shared_ptr<const std::string> bytes;
    };

    struct PresentationEmbeddedFont
    {
        std::string family;
        std::string style;
        std::string path;
        std::shared_ptr<const std::string> bytes;
    };

    struct PresentationSection
    {
        std::string id;
        std::string name;
    };

    struct PresentationThemeDefinition
    {
        std::string name;
        std::map<std::string, std::string> colors;
        std::map<std::string, std::string> fonts;
        std::set<std::string> editable_color_slots;
        std::set<std::string> editable_font_slots;
    };

    struct PresentationScene
    {
        double width = 720;
        double height = 540;
        std::vector<PresentationSlide> slides;
        std::vector<PresentationSection> sections;
        // Parsed themes are shared by slide index; package resources are not loaded again for inspection.
        std::vector<PresentationThemeDefinition> themes;
        bool section_structure_locked = false;
        std::vector<PresentationImage> images;
        std::vector<PresentationMedia> media;
        std::vector<PresentationEmbeddedFont> embedded_fonts;
        std::vector<std::string> warnings;
        std::uint64_t next_shape_id = 1;
        bool native_editable = false;
        // Immutable package snapshots share unchanged parts across edits and undo history.
        std::shared_ptr<const PresentationPackageState> source_package;
    };

    struct PresentationThemeState
    {
        bool available = false;
        bool authored = false;
        std::string name;
        std::map<std::string, std::string> colors;
        std::map<std::string, std::string> fonts;
        std::set<std::string> editable_color_slots;
        std::set<std::string> editable_font_slots;
        std::size_t linked_slide_count = 0;
    };

    PresentationThemeState presentation_theme_state(const PresentationScene& scene, std::size_t slide_index);

    struct PresentationNavigationResult
    {
        bool handled = false;
        bool end_show = false;
        int target_slide = -1;
    };

    PresentationNavigationResult resolve_presentation_click(const PresentationScene& scene,
        std::size_t current_slide, std::size_t shape_index, int last_viewed_slide = -1);

    struct PresentationTextMatch
    {
        std::uint64_t shape_id = 0;
        std::size_t slide_index = 0;
        std::size_t shape_index = 0;
        std::size_t paragraph_index = 0;
        std::size_t start_byte = 0;
        std::size_t length_bytes = 0;
        bool replaceable = false;
        std::string excerpt;
    };

    // Offsets are UTF-8 byte offsets within one paragraph; results are in slide and text order.
    std::vector<PresentationTextMatch> find_presentation_text(const PresentationScene& scene,
        const std::string& query, bool case_sensitive = false, std::size_t max_results = 10000);

    struct PresentationResult
    {
        PresentationError error = PresentationError::None;
        std::string message;
        PresentationScene scene;
        std::string path;
        std::string revision;
        std::string source_bytes;
    };

    enum class PresentationSlideLayout
    {
        Title,
        TitleContent,
        TwoColumns,
        Blank,
        ReportOutline,
        ResearchPlan,
        Comparison,
        References,
        Conclusion,
        ProjectStatus,
        MeetingSummary,
        Milestones,
        ResearchStudio,
        EvidenceBoard,
        ProjectDashboard,
        DeliveryRoadmap
    };

    enum class PresentationEditAction
    {
        AddSlide,
        DuplicateSlide,
        DeleteSlide,
        MoveSlide,
        AddText,
        AddShape,
        AddImage,
        UpdateText,
        FormatText,
        FormatTextStyle,
        FormatParagraph,
        FormatTextBox,
        FormatShape,
        FormatImage,
        ReplaceImage,
        TransformShape,
        DeleteShape,
        DuplicateShape,
        MoveShape,
        SetBackground,
        AlignShape,
        SetSlideHidden,
        FormatTableCell,
        ResetTableCellFill,
        MoveGroup,
        ApplyTheme,
        InsertTable,
        InsertTableRow,
        InsertTableColumn,
        MergeTableCell,
        FormatTableBorder,
        ResetTableBorder,
        GroupAdjacent,
        AddToGroup,
        Ungroup,
        CreateSection,
        RenameSection,
        RemoveSection,
        SetClickAction,
        ApplyFormat,
        ReplaceTextMatches,
        ResetPlaceholderFill,
        DeleteTableRow,
        DeleteTableColumn,
        ResetPlaceholderOutline,
        UnmergeTableCell,
        ResetTextInheritance,
        FormatTableStyle,
        SetSlideTransition,
        ReorderGroup
    };

    enum class PresentationTextProperty
    {
        FontFamily,
        FontSize,
        Color
    };

    enum class PresentationEditError
    {
        None,
        ReadOnly,
        InvalidIndex,
        InvalidValue,
        TooLarge
    };

    // Object capabilities do not grant document write permission; imports still require an editable copy.
    std::vector<PresentationEditAction> presentation_edit_capabilities(const PresentationShape& shape);

    struct PresentationTextLocalOverrides
    {
        bool font_family = false;
        bool font_size = false;
        bool color = false;
    };

    PresentationTextLocalOverrides presentation_text_local_overrides(const PresentationShape& shape);

    struct PresentationTemplatePalette
    {
        std::string ink = "#222222";
        std::string paper = "#FFFFFF";
        std::string card = "#FFFFFF";
        std::string muted = "#666666";
        std::string accent = "#444444";
        std::string soft = "#F0F0F0";
    };

    struct PresentationOutlineStyle
    {
        PresentationFill fill;
        double width = 1;
    };

    struct PresentationShadowStyle
    {
        std::string color = "#000000";
        double opacity = 0.45;
        double blur = 3;
        double x = 2;
        double y = 2;
    };

    struct PresentationGlowStyle
    {
        std::string color = "#4B8CFF";
        double opacity = 0.45;
        double radius = 3;
    };

    struct PresentationReflectionStyle
    {
        double opacity = 0.5;
        double offset = 0;
        double end_opacity = 0;
        double start_position = 0;
        double end_position = 0.65;
    };

    // Each supplied group replaces that property for the whole text object; other groups stay intact.
    struct PresentationTextStylePatch
    {
        std::optional<PresentationFill> fill;
        std::optional<PresentationOutlineStyle> outline;
        std::optional<PresentationShadowStyle> shadow;
        std::optional<PresentationGlowStyle> glow;
        std::optional<PresentationReflectionStyle> reflection;
        std::optional<std::string> warp;
        std::optional<double> warp_adjustment;
        std::optional<double> rotation;
        std::optional<std::string> vertical;
        std::optional<bool> clip_vertical;
        std::optional<bool> clip_horizontal;
    };

    struct PresentationEditCommand
    {
        PresentationEditAction action = PresentationEditAction::UpdateText;
        std::size_t slide_index = 0;
        std::size_t shape_index = 0;
        std::size_t format_source_slide = 0;
        std::size_t format_source_shape = 0;
        std::string find_query;
        std::string find_replacement;
        bool find_case_sensitive = false;
        bool find_all = false;
        std::uint64_t find_shape_id = 0;
        std::size_t find_paragraph_index = 0;
        std::size_t find_start_byte = 0;
        PresentationSlideLayout layout = PresentationSlideLayout::Blank;
        PresentationTemplatePalette template_palette;
        int offset = 0;
        std::optional<std::size_t> target_index;
        std::string text;
        std::string geometry;
        std::string group_id;
        std::string group_layer_position;
        std::string section_name;
        std::string click_kind;
        std::optional<std::size_t> click_target_slide;
        std::map<std::string, std::string> theme_colors;
        std::map<std::string, std::string> theme_fonts;
        int table_rows = 0;
        int table_columns = 0;
        std::optional<PresentationTableStyleOptions> table_style_options;
        std::optional<PresentationTransition> slide_transition;
        std::string table_direction;
        std::string table_edge = "all";
        std::string image_path;
        std::string image_mime_type;
        std::string image_bytes;
        std::optional<std::string> font_family;
        std::optional<PresentationTextProperty> reset_text_property;
        PresentationTextStylePatch text_style;
        std::optional<double> font_size;
        std::optional<bool> bold;
        std::optional<bool> italic;
        std::optional<bool> underline;
        std::optional<bool> strike;
        std::optional<double> character_spacing;
        std::optional<double> baseline;
        std::optional<std::string> text_color;
        std::optional<std::string> alignment;
        std::optional<bool> bullet;
        std::optional<bool> numbered;
        std::optional<int> number_start;
        std::optional<int> list_level;
        std::optional<int> paragraph_index;
        std::optional<double> paragraph_margin_left;
        std::optional<double> first_line_indent;
        std::optional<double> line_spacing;
        std::optional<double> space_before;
        std::optional<double> space_after;
        std::optional<double> inset_left;
        std::optional<double> inset_right;
        std::optional<double> inset_top;
        std::optional<double> inset_bottom;
        std::optional<std::string> vertical_alignment;
        std::optional<bool> wrap;
        std::optional<bool> auto_fit;
        std::optional<std::string> fill_color;
        std::optional<double> fill_opacity;
        std::optional<std::string> gradient_start_color;
        std::optional<std::string> gradient_end_color;
        std::optional<double> gradient_angle;
        std::optional<std::string> fill_pattern;
        std::optional<std::string> pattern_foreground_color;
        std::optional<std::string> pattern_background_color;
        std::optional<std::string> outline_color;
        std::optional<double> outline_opacity;
        std::optional<double> outline_width;
        std::optional<std::string> line_dash;
        std::optional<std::string> line_head;
        std::optional<std::string> line_tail;
        std::optional<bool> shadow_enabled;
        std::optional<std::string> shadow_color;
        std::optional<double> shadow_opacity;
        std::optional<double> shadow_blur;
        std::optional<double> shadow_x;
        std::optional<double> shadow_y;
        std::optional<bool> glow_enabled;
        std::optional<std::string> glow_color;
        std::optional<double> glow_opacity;
        std::optional<double> glow_radius;
        std::optional<double> x;
        std::optional<double> y;
        std::optional<double> width;
        std::optional<double> height;
        std::optional<double> rotation;
        std::optional<bool> flip_horizontal;
        std::optional<bool> flip_vertical;
        std::optional<bool> preserve_aspect;
        std::optional<double> image_crop_left;
        std::optional<double> image_crop_top;
        std::optional<double> image_crop_right;
        std::optional<double> image_crop_bottom;
        std::optional<double> image_opacity;
        std::optional<std::string> background_color;
        std::optional<bool> hidden;
    };

    struct PresentationEditResult
    {
        PresentationEditError error = PresentationEditError::None;
        std::string message;
        std::size_t slide_index = 0;
        std::optional<std::size_t> shape_index;
    };

    struct PresentationPackageResult
    {
        PresentationError error = PresentationError::None;
        std::string message;
        std::vector<PresentationPart> parts;
    };

    bool is_presentation_path(std::string path);
    const std::vector<std::string>& presentation_pattern_presets();
    bool presentation_pattern_supported(const std::string& pattern);
    const std::vector<std::string>& presentation_line_dash_presets();
    std::vector<double> presentation_line_dash_pattern(const std::string& preset);
    std::string presentation_line_dash_name(const std::vector<double>& pattern);
    PresentationResult parse_presentation(
        std::vector<PresentationPart> parts, const PresentationParseProgress& progress = {});
    PresentationScene make_presentation(PresentationSlideLayout layout = PresentationSlideLayout::Title,
        const PresentationTemplatePalette& palette = {});
    // Updates the logical model only; package preservation can be committed asynchronously afterwards.
    PresentationEditResult apply_presentation_model_edit(
        PresentationScene& scene, const PresentationEditCommand& command);
    PresentationEditResult apply_presentation_edit(
        PresentationScene& scene, const PresentationEditCommand& command);
    PresentationPackageResult serialize_presentation(const PresentationScene& scene);
    std::size_t presentation_source_metadata_bytes(const PresentationScene& scene);

}
