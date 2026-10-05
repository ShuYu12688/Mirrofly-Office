cmake_minimum_required(VERSION 3.21)

project(MirrorflyOffice VERSION 1.1.10 LANGUAGES C CXX)

set(MIRRORFLY_VERSION_SUFFIX "beta")
set(MIRRORFLY_DISPLAY_VERSION "${PROJECT_VERSION}${MIRRORFLY_VERSION_SUFFIX}")

configure_file(config/build_version.hpp.in generated/mirrorfly/build_version.hpp @ONLY)
file(READ third_party/poi/presetShapeDefinitions.xml MIRRORFLY_GEOMETRY_XML)
string(LENGTH "${MIRRORFLY_GEOMETRY_XML}" MIRRORFLY_GEOMETRY_LENGTH)
set(MIRRORFLY_GEOMETRY_CHUNKS "")
foreach(offset RANGE 0 ${MIRRORFLY_GEOMETRY_LENGTH} 8192)
    string(SUBSTRING "${MIRRORFLY_GEOMETRY_XML}" ${offset} 8192 chunk)
    string(APPEND MIRRORFLY_GEOMETRY_CHUNKS "R\"mfgeom(${chunk})mfgeom\",\n")
endforeach()
configure_file(config/presentation_geometry_data.hpp.in generated/presentation_geometry_data.hpp @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(MIRRORFLY_BUILD_UI "Build the Qt Quick application" ON)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(mirrorfly_md4c STATIC third_party/md4c/md4c.c third_party/md4c/entity.c)
target_include_directories(mirrorfly_md4c SYSTEM PUBLIC third_party/md4c)
target_compile_definitions(mirrorfly_md4c PUBLIC md_parse=mirrorfly_md_parse
    entity_lookup=mirrorfly_md_entity_lookup)

add_library(mirrorfly_pugixml STATIC third_party/pugixml/pugixml.cpp)
target_include_directories(mirrorfly_pugixml SYSTEM PUBLIC third_party/pugixml)
target_compile_definitions(mirrorfly_pugixml PUBLIC PUGIXML_NO_XPATH)

add_library(mirrorfly_core STATIC src/core/src/core.cpp src/core/src/text.cpp src/core/src/markdown_break.cpp src/core/src/markdown_rule.cpp src/core/src/markdown_rule.hpp src/core/src/markdown_break.hpp
    src/core/src/markdown.cpp src/core/src/markdown_heading.cpp src/core/src/markdown_heading.hpp src/core/src/markdown_quote.cpp src/core/src/markdown_blocks.cpp src/core/src/markdown_list_source.cpp src/core/src/markdown_list_source.hpp src/core/src/markdown_table.cpp src/core/src/spreadsheet_formula.cpp src/core/src/word_pinyin.cpp src/core/src/spreadsheet_filters.cpp src/core/src/spreadsheet_tables.cpp src/core/src/spreadsheet_structure.cpp src/core/src/spreadsheet_structure_xml.cpp src/core/src/spreadsheet_workbook.cpp
    src/core/src/presentation.cpp src/core/src/presentation_table.cpp src/core/src/presentation_animation.cpp src/core/src/presentation_transition.cpp src/core/src/presentation_geometry.cpp src/core/src/presentation_edit.cpp src/core/src/presentation_format_brush.cpp src/core/src/presentation_find_replace.cpp src/core/src/presentation_preservation.cpp src/core/src/presentation_sections.cpp src/core/src/presentation_navigation.cpp src/core/src/presentation_templates.cpp src/core/src/presentation_theme_package.cpp src/core/src/spreadsheet.cpp src/core/src/spreadsheet_format.cpp src/core/src/spreadsheet_features.cpp src/core/src/spreadsheet_move.cpp src/core/src/word.cpp src/core/src/pdf.cpp src/core/src/mindmap.cpp src/core/src/mindmap_graph.cpp src/core/src/mindmap_placement.cpp src/core/src/mindmap_layout.cpp)
target_include_directories(mirrorfly_core PUBLIC src/core/include)
target_sources(mirrorfly_core PRIVATE src/core/src/markdown_structure.cpp src/core/src/markdown_structure.hpp
    src/core/src/markdown_link.cpp src/core/src/markdown_link.hpp
    src/core/src/markdown_link_parser.cpp src/core/src/markdown_link_parser.hpp
    src/core/src/markdown_parse_text.cpp src/core/src/markdown_code_source.cpp src/core/src/markdown_code_source.hpp
    src/core/src/markdown_parse_blocks.cpp src/core/src/markdown_parse_context.hpp
    src/core/src/markdown_image.cpp src/core/src/markdown_image.hpp
    src/core/src/markdown_link_format.cpp src/core/src/markdown_link_format.hpp
    src/core/src/markdown_inline.cpp src/core/src/markdown_inline_literal.cpp src/core/src/markdown_inline_literal.hpp
    src/core/src/markdown_emphasis_source.cpp src/core/src/markdown_emphasis_source.hpp src/core/src/markdown_paragraph.cpp
    src/core/src/markdown_decode.cpp src/core/src/markdown_decode.hpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_chart.cpp src/core/src/presentation_notes.cpp src/core/src/presentation_group_edit.cpp src/core/src/presentation_table_style.cpp src/core/src/presentation_table_structure.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_math.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_parse_package.cpp src/core/src/presentation_parse_package.hpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_relationships.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_parse_style.cpp src/core/src/presentation_parse_style.hpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_edit_common.cpp src/core/src/presentation_edit_common.hpp src/core/src/presentation_serialize.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_edit_layout.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_edit_native.cpp src/core/src/presentation_edit_native.hpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_group_layer.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_theme_state.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_text_style.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/presentation_style_presets.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/word_package.cpp
    src/core/src/word_table_borders.cpp src/core/src/word_cell_preservation.cpp
    src/core/src/word_border_conflict.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/word_images.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/word_preservation.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/word_numbering.cpp
    src/core/src/word_tabs.cpp src/core/src/word_tabs.hpp
    src/core/src/word_paragraph_layout.cpp src/core/src/word_paragraph_layout.hpp
    src/core/src/word_numbering_read.cpp src/core/src/word_numbering_read.hpp
    src/core/src/word_numbering_variants.cpp src/core/src/word_numbering_variants.hpp)
target_sources(mirrorfly_core PRIVATE src/core/src/word_style_merge.cpp src/core/src/word_table_styles.cpp src/core/src/word_styles.cpp src/core/src/word_style_colors.cpp
    src/core/src/office_color.cpp src/core/src/spreadsheet_colors.cpp)
target_sources(mirrorfly_core PRIVATE src/core/src/word_property_patch.cpp)
target_include_directories(mirrorfly_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(mirrorfly_core SYSTEM PRIVATE third_party/utfcpp/source third_party/unihan)
target_link_libraries(mirrorfly_core PRIVATE mirrorfly_pugixml mirrorfly_md4c)

if(BUILD_TESTING)
    add_executable(mirrorfly_office_ai_header_tests tests/office_ai_header_tests.cpp)
    target_include_directories(mirrorfly_office_ai_header_tests PRIVATE src/ui/include)
    add_test(NAME office_ai_public_header COMMAND mirrorfly_office_ai_header_tests)
    add_executable(mirrorfly_presentation_text_style_tests tests/presentation_text_style_tests.cpp)
    target_link_libraries(mirrorfly_presentation_text_style_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_text_style COMMAND mirrorfly_presentation_text_style_tests)
    add_executable(mirrorfly_presentation_structure_tests tests/presentation_structure_tests.cpp)
    target_link_libraries(mirrorfly_presentation_structure_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_structure COMMAND mirrorfly_presentation_structure_tests)
    add_executable(mirrorfly_presentation_section_tests tests/presentation_section_tests.cpp)
    target_link_libraries(mirrorfly_presentation_section_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_sections COMMAND mirrorfly_presentation_section_tests)
    add_executable(mirrorfly_presentation_navigation_tests tests/presentation_navigation_tests.cpp)
    target_link_libraries(mirrorfly_presentation_navigation_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_navigation COMMAND mirrorfly_presentation_navigation_tests)
    add_executable(mirrorfly_presentation_format_brush_tests tests/presentation_format_brush_tests.cpp)
    target_link_libraries(mirrorfly_presentation_format_brush_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_format_brush COMMAND mirrorfly_presentation_format_brush_tests)
    add_executable(mirrorfly_presentation_find_replace_tests tests/presentation_find_replace_tests.cpp)
    target_link_libraries(mirrorfly_presentation_find_replace_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_find_replace COMMAND mirrorfly_presentation_find_replace_tests)
    add_executable(mirrorfly_presentation_edit_dispatch_tests tests/presentation_edit_dispatch_tests.cpp)
    target_link_libraries(mirrorfly_presentation_edit_dispatch_tests PRIVATE mirrorfly_core)
    add_test(NAME presentation_edit_dispatch COMMAND mirrorfly_presentation_edit_dispatch_tests)
    set_tests_properties(presentation_edit_dispatch PROPERTIES TIMEOUT 30)
    add_executable(mirrorfly_presentation_graphics_tests tests/presentation_graphics_tests.cpp)
    target_link_libraries(mirrorfly_presentation_graphics_tests PRIVATE mirrorfly_core)
    add_test(NAME presentation_graphics COMMAND mirrorfly_presentation_graphics_tests)
    add_executable(mirrorfly_presentation_table_tests tests/presentation_table_tests.cpp)
    target_link_libraries(mirrorfly_presentation_table_tests PRIVATE mirrorfly_core)
    add_test(NAME presentation_table COMMAND mirrorfly_presentation_table_tests)
    add_executable(mirrorfly_presentation_animation_tests tests/presentation_animation_tests.cpp)
    target_link_libraries(mirrorfly_presentation_animation_tests PRIVATE mirrorfly_core)
    add_test(NAME presentation_animation COMMAND mirrorfly_presentation_animation_tests)
    add_executable(mirrorfly_presentation_transition_tests tests/presentation_transition_tests.cpp)
    target_link_libraries(mirrorfly_presentation_transition_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_transition COMMAND mirrorfly_presentation_transition_tests)
    add_executable(mirrorfly_presentation_group_layer_tests tests/presentation_group_layer_tests.cpp)
    target_link_libraries(mirrorfly_presentation_group_layer_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_group_layer COMMAND mirrorfly_presentation_group_layer_tests)
    add_executable(mirrorfly_presentation_preservation_tests tests/presentation_preservation_tests.cpp)
    target_link_libraries(mirrorfly_presentation_preservation_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME presentation_preservation COMMAND mirrorfly_presentation_preservation_tests)
    add_executable(mirrorfly_presentation_geometry_tests tests/presentation_geometry_tests.cpp)
    target_link_libraries(mirrorfly_presentation_geometry_tests PRIVATE mirrorfly_core)
    add_test(NAME presentation_geometry COMMAND mirrorfly_presentation_geometry_tests)
    add_executable(mirrorfly_spreadsheet_formula_tests tests/spreadsheet_formula_tests.cpp)
    target_link_libraries(mirrorfly_spreadsheet_formula_tests PRIVATE mirrorfly_core)
    add_test(NAME spreadsheet_formula COMMAND mirrorfly_spreadsheet_formula_tests)
    set_tests_properties(spreadsheet_formula PROPERTIES TIMEOUT 30)
    foreach(module pdf mindmap)
        add_executable(mirrorfly_${module}_tests tests/${module}_tests.cpp)
        target_link_libraries(mirrorfly_${module}_tests PRIVATE mirrorfly_core)
        add_test(NAME ${module}_core COMMAND mirrorfly_${module}_tests)
        set_tests_properties(${module}_core PROPERTIES TIMEOUT 30)
    endforeach()
    add_executable(mirrorfly_spreadsheet_format_tests tests/spreadsheet_format_tests.cpp)
    target_link_libraries(mirrorfly_spreadsheet_format_tests PRIVATE mirrorfly_core)
    add_test(NAME spreadsheet_format COMMAND mirrorfly_spreadsheet_format_tests)
    add_executable(mirrorfly_spreadsheet_filter_tests tests/spreadsheet_filter_tests.cpp)
    target_link_libraries(mirrorfly_spreadsheet_filter_tests PRIVATE mirrorfly_core)
    add_test(NAME spreadsheet_filter COMMAND mirrorfly_spreadsheet_filter_tests)
    add_executable(mirrorfly_spreadsheet_table_tests tests/spreadsheet_table_tests.cpp)
    target_link_libraries(mirrorfly_spreadsheet_table_tests PRIVATE mirrorfly_core)
    add_test(NAME spreadsheet_table COMMAND mirrorfly_spreadsheet_table_tests)
    add_executable(mirrorfly_spreadsheet_structure_tests tests/spreadsheet_structure_tests.cpp)
    target_link_libraries(mirrorfly_spreadsheet_structure_tests PRIVATE mirrorfly_core)
    add_test(NAME spreadsheet_structure COMMAND mirrorfly_spreadsheet_structure_tests)
    add_executable(mirrorfly_word_tests tests/word_tests.cpp)
    add_executable(mirrorfly_word_package_tests tests/word_package_tests.cpp)
    target_link_libraries(mirrorfly_word_package_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME word_package_preservation COMMAND mirrorfly_word_package_tests)
    add_executable(mirrorfly_word_tab_core_tests tests/word_tab_core_tests.cpp)
    target_link_libraries(mirrorfly_word_tab_core_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME word_tab_preservation COMMAND mirrorfly_word_tab_core_tests)
    add_executable(mirrorfly_word_numbering_tests tests/word_numbering_tests.cpp)
    target_link_libraries(mirrorfly_word_numbering_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME word_numbering_preservation COMMAND mirrorfly_word_numbering_tests)
    add_executable(mirrorfly_word_style_tests tests/word_style_tests.cpp)
    target_link_libraries(mirrorfly_word_style_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME word_style_preservation COMMAND mirrorfly_word_style_tests)
    add_executable(mirrorfly_word_table_style_tests tests/word_table_style_tests.cpp)
    target_link_libraries(mirrorfly_word_table_style_tests PRIVATE mirrorfly_core)
    add_test(NAME word_table_styles COMMAND mirrorfly_word_table_style_tests)
    add_executable(mirrorfly_word_table_border_tests tests/word_table_border_tests.cpp)
    target_link_libraries(mirrorfly_word_table_border_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME word_table_borders COMMAND mirrorfly_word_table_border_tests)
    add_executable(mirrorfly_word_image_geometry_tests tests/word_image_geometry_tests.cpp)
    target_link_libraries(mirrorfly_word_image_geometry_tests PRIVATE mirrorfly_core mirrorfly_pugixml)
    add_test(NAME word_image_geometry COMMAND mirrorfly_word_image_geometry_tests)
    target_link_libraries(mirrorfly_word_tests PRIVATE mirrorfly_core)
    add_test(NAME word_core COMMAND mirrorfly_word_tests)
    add_executable(mirrorfly_spreadsheet_tests tests/spreadsheet_tests.cpp)
    target_link_libraries(mirrorfly_spreadsheet_tests PRIVATE mirrorfly_core)
    add_test(NAME spreadsheet_core COMMAND mirrorfly_spreadsheet_tests)

    add_executable(mirrorfly_core_tests tests/core_tests.cpp)
    target_link_libraries(mirrorfly_core_tests PRIVATE mirrorfly_core)
    add_test(NAME core_contract COMMAND mirrorfly_core_tests)

    add_executable(mirrorfly_text_tests tests/text_tests.cpp)
    target_link_libraries(mirrorfly_text_tests PRIVATE mirrorfly_core)
    add_test(NAME text_codec COMMAND mirrorfly_text_tests)

    add_executable(mirrorfly_markdown_inline_tests tests/markdown_inline_tests.cpp)
    target_link_libraries(mirrorfly_markdown_inline_tests PRIVATE mirrorfly_core)
    add_test(NAME markdown_inline_labels COMMAND mirrorfly_markdown_inline_tests)

    add_executable(mirrorfly_markdown_tests tests/markdown_tests.cpp)
    target_link_libraries(mirrorfly_markdown_tests PRIVATE mirrorfly_core)
    add_test(NAME markdown_operations COMMAND mirrorfly_markdown_tests)

    add_executable(mirrorfly_presentation_tests tests/presentation_tests.cpp)
    target_link_libraries(mirrorfly_presentation_tests PRIVATE mirrorfly_core)
    add_test(NAME presentation_scene COMMAND mirrorfly_presentation_tests)
endif()

if(MIRRORFLY_BUILD_UI)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Concurrent Gui Network Qml Quick QuickControls2 Svg Widgets)
    qt_policy(SET QTP0001 NEW)

    add_library(mirrorfly_miniz STATIC third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c
        third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
    target_include_directories(mirrorfly_miniz SYSTEM PUBLIC third_party/miniz)
    target_compile_definitions(mirrorfly_miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME
        MINIZ_NO_ZLIB_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

    set(MIRRORFLY_PDFIUM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium" CACHE PATH "PDFium binary distribution")
    if(NOT WIN32)
        message(FATAL_ERROR "Provide and validate a platform-specific PDFium distribution before building the UI on this platform.")
    endif()
    add_library(mirrorfly_pdfium SHARED IMPORTED)
    set_target_properties(mirrorfly_pdfium PROPERTIES
        IMPORTED_IMPLIB "${MIRRORFLY_PDFIUM_ROOT}/lib/pdfium.dll.lib"
        IMPORTED_LOCATION "${MIRRORFLY_PDFIUM_ROOT}/bin/pdfium.dll")
    add_library(mirrorfly_platform STATIC src/platform/src/platform.cpp src/platform/src/text_storage.cpp
        src/platform/src/credential_protection.cpp
        src/platform/src/presentation_storage.cpp src/platform/src/presentation_media.cpp
        src/platform/src/image_decode.cpp src/platform/src/embedded_font.cpp src/platform/src/archive_storage.cpp
        src/platform/src/spreadsheet_storage.cpp src/platform/src/word_storage.cpp src/platform/src/pdf_storage.cpp src/platform/src/mindmap_storage.cpp)
    target_include_directories(mirrorfly_platform PUBLIC src/platform/include)
    target_include_directories(mirrorfly_platform SYSTEM PRIVATE "${MIRRORFLY_PDFIUM_ROOT}/include")
    target_link_libraries(mirrorfly_platform PRIVATE mirrorfly_pdfium)
    add_custom_command(TARGET mirrorfly_platform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:mirrorfly_pdfium>" "${CMAKE_CURRENT_BINARY_DIR}/pdfium.dll")
    target_link_libraries(mirrorfly_platform PUBLIC mirrorfly_core PRIVATE
        Qt6::Core Qt6::Gui Qt6::Svg mirrorfly_miniz)
    if(WIN32)
        target_link_libraries(mirrorfly_platform PRIVATE
            mfplat mfuuid d3d11 gdiplus ole32 oleaut32 t2embed windowscodecs crypt32)
        if(BUILD_TESTING)
            add_executable(mirrorfly_presentation_media_tests tests/presentation_media_tests.cpp)
            target_link_libraries(mirrorfly_presentation_media_tests PRIVATE mirrorfly_platform Qt6::Core mfreadwrite)
            add_test(NAME presentation_media COMMAND mirrorfly_presentation_media_tests)
            set_tests_properties(presentation_media PROPERTIES TIMEOUT 30)
        endif()
    endif()

    add_library(mirrorfly_ui STATIC
        src/ui/src/presentation_fill_renderer.cpp src/ui/src/presentation_fill_renderer.hpp
        src/ui/src/presentation_line_renderer.cpp src/ui/src/presentation_line_renderer.hpp
        src/ui/src/startup_request.cpp
        src/ui/src/document_view.cpp src/ui/src/document_view.hpp
        src/ui/src/pdf_stamp.cpp
        src/ui/src/pdf_export_bridge.cpp src/ui/src/pdf_export_bridge.hpp src/ui/src/pdf_export_writer.cpp
        src/ui/src/presentation_image_export.cpp src/ui/src/presentation_image_export.hpp
        src/ui/src/presentation_image_export_bridge.cpp src/ui/src/presentation_image_export_bridge.hpp
        src/ui/src/automation_bridge.cpp src/ui/src/automation_bridge.hpp
        src/ui/src/automation_contract.cpp src/ui/src/automation_contract.hpp
        src/ui/src/office_ai_agent.cpp src/ui/src/office_ai_agent.hpp
        src/ui/src/office_ai_configuration.cpp
        src/ui/src/office_ai_context.cpp src/ui/src/office_ai_context_saves.cpp src/ui/src/office_ai_context.hpp
        src/ui/src/office_ai_context_formats.cpp
        src/ui/src/office_ai_file_policy.cpp src/ui/src/office_ai_file_policy.hpp
        src/ui/src/office_ai_actions.cpp src/ui/src/office_ai_schema.cpp src/ui/src/office_ai_dispatch.cpp
        src/ui/src/office_ai_tools.cpp src/ui/src/office_ai_tools.hpp
        src/ui/src/office_ai_prompt.cpp src/ui/src/office_ai_guides.cpp src/ui/src/office_ai_compose_guides.cpp
        src/ui/src/office_ai_style.cpp src/ui/src/office_ai_style.hpp
        src/ui/src/office_ai_image_library.cpp src/ui/src/office_ai_image_library.hpp
        src/ui/src/office_ai_provider.cpp src/ui/src/office_ai_provider.hpp
        src/ui/src/office_ai_model_loop.cpp
        src/ui/src/office_ai_resume.cpp
        src/ui/src/office_ai_toolbox.cpp src/ui/src/office_ai_toolbox.hpp
        src/ui/src/office_ai_run.cpp src/ui/src/office_ai_run.hpp
        src/ui/src/office_ai_diagnostics.cpp
        src/ui/src/office_ai_workflow.cpp src/ui/src/office_ai_save_workflow.cpp
        src/ui/src/office_ai_pages.cpp
        src/ui/src/office_ai_page_layout.cpp src/ui/src/office_ai_page_layout.hpp
        src/ui/src/office_ai_structured.cpp src/ui/src/office_ai_structured.hpp
        src/ui/src/office_ai_recovery.cpp src/ui/src/office_ai_recovery.hpp
        src/ui/src/office_ai_contract_adapter.cpp src/ui/src/office_ai_contract_adapter.hpp
        src/ui/src/office_ai_slide_style.cpp src/ui/src/office_ai_slide_style.hpp
        src/ui/src/office_ai_stream.cpp src/ui/src/office_ai_stream.hpp
        src/ui/src/office_ai_workspace.cpp src/ui/src/office_ai_workspace.hpp
        src/ui/src/office_ai_timeline.cpp src/ui/src/office_ai_timeline.hpp
        src/ui/src/canvas_schema.cpp
        src/ui/src/canvas_read.cpp src/ui/src/document_read.hpp
        src/ui/src/presentation_read.cpp src/ui/src/spreadsheet_read.cpp
        src/ui/src/spreadsheet_interface.cpp
        src/ui/src/office_ai_layout_catalog.cpp src/ui/src/office_ai_layout_catalog.hpp
        src/ui/src/office_ai_permissions.cpp src/ui/src/office_ai_tool_definitions.hpp
        src/ui/src/office_ai_document_tools.cpp src/ui/src/office_ai_compose_tools.cpp src/ui/src/office_ai_task.cpp
        src/ui/src/office_ai_completion.cpp
        src/ui/src/office_ai_request.cpp src/ui/src/office_ai_request.hpp
        src/ui/src/office_ai_log.cpp src/ui/src/office_ai_log.hpp
        src/ui/src/office_ai_sequence.cpp src/ui/src/office_ai_sequence.hpp
        src/ui/src/canvas_bridge.cpp src/ui/src/canvas_bridge.hpp src/ui/src/canvas_connection.cpp src/ui/src/canvas_commands.cpp src/ui/src/canvas_paint.cpp
        src/ui/src/canvas_routes.cpp src/ui/src/canvas_routes.hpp
        src/ui/src/word_bridge.cpp src/ui/src/word_bridge.hpp
        src/ui/src/word_tools.cpp src/ui/src/word_clipboard.cpp src/ui/src/word_annotations.cpp src/ui/src/word_distribution.cpp
        src/ui/src/word_document.cpp src/ui/src/word_document.hpp
        src/ui/src/word_character.cpp src/ui/src/word_character.hpp
        src/ui/src/word_decorations.cpp
        src/ui/src/word_tabs.cpp src/ui/src/word_tabs.hpp
        src/ui/src/word_tab_leaders.cpp
        src/ui/src/word_paragraph_format.cpp src/ui/src/word_paragraph_format.hpp
        src/ui/src/word_lists.cpp src/ui/src/word_lists.hpp
        src/ui/src/word_cell_borders.cpp src/ui/src/word_cell_borders.hpp
        src/ui/src/word_interface.cpp src/ui/src/word_read.cpp
        src/ui/src/word_editor_document.cpp src/ui/src/word_editor_document.hpp
        src/ui/src/word_document_structure.cpp src/ui/src/word_document_structure.hpp
        src/ui/src/word_table_gaps.cpp src/ui/src/word_table_gaps.hpp
        src/ui/src/word_image_resources.cpp src/ui/src/word_image_resources.hpp
        src/ui/src/spreadsheet_input.cpp
        src/ui/src/spreadsheet_tools.cpp src/ui/src/spreadsheet_table_tools.cpp src/ui/src/spreadsheet_structure_tools.cpp src/ui/src/spreadsheet_start_tools.cpp src/ui/src/spreadsheet_clipboard.cpp
        src/ui/src/spreadsheet_input.hpp
        src/ui/src/spreadsheet_bridge.cpp
        src/ui/src/spreadsheet_bridge.hpp
        src/ui/src/spreadsheet_model.cpp
        src/ui/src/spreadsheet_model.hpp
        src/ui/src/bridge.cpp
        src/ui/src/bridge.hpp
        src/ui/src/runtime.cpp
        src/ui/src/window_decoration.cpp
        src/ui/src/desktop_resident.cpp src/ui/src/desktop_resident.hpp
        src/ui/src/office_ai_settings.cpp src/ui/src/office_ai_settings.hpp
        src/ui/src/ai_island_host.cpp src/ui/src/ai_island_host.hpp
        src/ui/src/ai_island_protocol.hpp
        src/ui/src/ai_island_trace.cpp src/ui/src/ai_island_trace.hpp
        src/ui/src/theme.cpp
        src/ui/src/text_bridge.cpp src/ui/src/text_content.cpp src/ui/src/text_search.cpp src/ui/src/text_search.hpp
        src/ui/src/text_bridge.hpp
        src/ui/src/editor_tools.cpp
        src/ui/src/editor_tools.hpp
        src/ui/src/markdown_inline.cpp src/ui/src/markdown_inline.hpp
        src/ui/src/markdown_inline_serialization.cpp src/ui/src/markdown_inline_code_serialization.cpp src/ui/src/markdown_inline_serialization.hpp
        src/ui/src/markdown_table.cpp src/ui/src/markdown_table_serialization.cpp src/ui/src/markdown_table_format.hpp src/ui/src/markdown_table.hpp
        src/ui/src/markdown_blocks.cpp src/ui/src/markdown_list_tree.cpp src/ui/src/markdown_list_move.cpp src/ui/src/markdown_list_tree.hpp src/ui/src/markdown_block_style.cpp src/ui/src/markdown_heading_style.cpp src/ui/src/markdown_fragment.cpp src/ui/src/markdown_paragraph.cpp src/ui/src/markdown_paragraph_export.cpp src/ui/src/markdown_paragraph.hpp src/ui/src/markdown_rule.cpp src/ui/src/markdown_rule.hpp src/ui/src/markdown_blocks.hpp
        src/ui/src/markdown_code.cpp src/ui/src/markdown_code_import.cpp src/ui/src/markdown_code_serialization.cpp src/ui/src/markdown_code_serialization.hpp src/ui/src/markdown_code.hpp
        src/ui/src/markdown_link.cpp src/ui/src/markdown_link.hpp
        src/ui/src/markdown_link_import.cpp
        src/ui/src/markdown_link_range.cpp src/ui/src/markdown_link_range.hpp
        src/ui/src/markdown_link_serialization.cpp src/ui/src/markdown_link_serialization.hpp
        src/ui/src/markdown_break.cpp src/ui/src/markdown_break.hpp
        src/ui/src/markdown_input.cpp src/ui/src/markdown_input_conversion.cpp src/ui/src/markdown_input.hpp
        src/ui/src/markdown_image.cpp src/ui/src/markdown_image.hpp src/ui/src/markdown_image_resource.cpp src/ui/src/markdown_image_serialization.cpp
        src/ui/src/markdown_document.cpp src/ui/src/markdown_load.cpp src/ui/src/markdown_context.cpp
        src/ui/src/markdown_document.hpp
        src/ui/src/markdown_validation.cpp src/ui/src/markdown_serialization.cpp
        src/ui/src/markdown_container.cpp src/ui/src/markdown_container_state.cpp src/ui/src/markdown_container_import.cpp src/ui/src/markdown_container_tokens.cpp src/ui/src/markdown_container.hpp
        src/ui/src/document_routing.cpp
        src/ui/src/document_routing.hpp
        src/ui/src/presentation_bridge.cpp
        src/ui/src/presentation_bridge_state.cpp
        src/ui/src/presentation_bridge.hpp
        src/ui/src/presentation_guide_settings.cpp
        src/ui/src/presentation_guide_settings.hpp
        src/ui/src/presentation_edit_adapter.cpp
        src/ui/src/presentation_option_validation.cpp
        src/ui/src/presentation_edit_adapter.hpp
        src/ui/src/presentation_scene.cpp
        src/ui/src/presentation_text_style_adapter.cpp
        src/ui/src/presentation_semantics.cpp
        src/ui/src/presentation_edit_layers.cpp
        src/ui/src/presentation_scene.hpp
        src/ui/src/presentation_render_scheduler.cpp
        src/ui/src/presentation_render_scheduler.hpp
        src/ui/src/presentation_geometry_renderer.cpp
        src/ui/src/presentation_geometry_renderer.hpp
        src/ui/src/presentation_media_session.cpp
        src/ui/src/presentation_media_session.hpp
        src/ui/src/presentation_playback.cpp
        src/ui/src/presentation_playback.hpp
        src/ui/src/presentation_animation_painter.cpp
        src/ui/src/presentation_animation_painter.hpp
        src/ui/src/presentation_text_renderer.cpp
        src/ui/src/presentation_text_renderer.hpp
    )
    set_target_properties(mirrorfly_ui PROPERTIES AUTOMOC ON)
    qt_add_qml_module(mirrorfly_ui
        URI Mirrorfly.Native
        VERSION 1.0
        DEPENDENCIES QtQuick
        NO_PLUGIN
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Mirrorfly/Native"
        SOURCES src/ui/src/slide_renderer.cpp src/ui/src/slide_renderer.hpp
            src/ui/src/slide_renderer_paint.cpp src/ui/src/slide_renderer_paint.hpp
            src/ui/src/slide_renderer_playback.cpp src/ui/src/slide_renderer_state.cpp
            src/ui/src/slide_thumbnail_renderer.cpp
            src/ui/src/presentation_screens.cpp src/ui/src/presentation_screens.hpp
            src/ui/src/word_viewport.cpp src/ui/src/word_viewport.hpp src/ui/src/word_viewport_paint.cpp
            src/ui/src/canvas_renderer.cpp src/ui/src/canvas_renderer.hpp
            src/ui/src/spreadsheet_border_renderer.cpp src/ui/src/spreadsheet_border_renderer.hpp
            src/ui/src/spreadsheet_text_layout.cpp src/ui/src/spreadsheet_text_renderer.cpp src/ui/src/spreadsheet_text_renderer.hpp
    )
    target_include_directories(mirrorfly_ui PRIVATE src/ui/src)
    target_include_directories(mirrorfly_ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_include_directories(mirrorfly_ui PUBLIC src/ui/include)
    target_link_libraries(mirrorfly_ui PRIVATE
        mirrorfly_core mirrorfly_platform
        Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::Widgets
    )
    if(WIN32)
        target_link_libraries(mirrorfly_ui PRIVATE user32 dwmapi)
    endif()

    add_library(mirrorfly_app STATIC src/app/src/app.cpp)
    target_include_directories(mirrorfly_app PUBLIC src/app/include)
    target_link_libraries(mirrorfly_app PRIVATE mirrorfly_ui)

    qt_add_executable(MirrorflyOffice WIN32 MACOSX_BUNDLE src/main.cpp)
    if(WIN32)
        set_source_files_properties(assets/app.rc PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/mirrorfly/build_version.hpp")
        target_sources(MirrorflyOffice PRIVATE assets/app.rc)
        target_include_directories(MirrorflyOffice PRIVATE assets)
        target_include_directories(MirrorflyOffice PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()
    target_link_libraries(MirrorflyOffice PRIVATE mirrorfly_app)
    set_target_properties(MirrorflyOffice PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mirrorfly.office"
        MACOSX_BUNDLE_BUNDLE_NAME "Mirrorfly Office"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )

    file(GLOB_RECURSE MIRRORFLY_UI_FILES CONFIGURE_DEPENDS ui/*.qml)
    qt_add_resources(MirrorflyOffice mirrorfly_assets
        PREFIX "/mirrorfly"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${MIRRORFLY_UI_FILES} Logo/logo1.svg assets/mirrorfly-mark.svg
            assets/mirrorfly-mark-light.svg config/theme.json
    )

    add_library(mirrorfly_ai_island_ui STATIC
        src/ui/src/ai_island_runtime.cpp
        src/ui/src/ai_island_client.cpp src/ui/src/ai_island_client.hpp
        src/ui/src/ai_island_protocol.hpp
        src/ui/src/theme.cpp src/ui/src/theme.hpp)
    set_target_properties(mirrorfly_ai_island_ui PROPERTIES AUTOMOC ON)
    target_include_directories(mirrorfly_ai_island_ui PUBLIC src/ui/include PRIVATE src/ui/src)
    target_link_libraries(mirrorfly_ai_island_ui PRIVATE
        Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)

    add_library(mirrorfly_ai_island_app STATIC src/app/src/ai_island_app.cpp)
    target_include_directories(mirrorfly_ai_island_app PUBLIC src/app/include)
    target_link_libraries(mirrorfly_ai_island_app PRIVATE mirrorfly_ai_island_ui)

    qt_add_executable(MirrorflyAiIsland WIN32 src/ai_island_main.cpp)
    if(WIN32)
        target_sources(MirrorflyAiIsland PRIVATE assets/app.rc)
        target_include_directories(MirrorflyAiIsland PRIVATE assets "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()
    target_link_libraries(MirrorflyAiIsland PRIVATE mirrorfly_ai_island_app)
    qt_add_resources(MirrorflyAiIsland mirrorfly_island_assets
        PREFIX "/mirrorfly"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ui/AiIslandWindow.qml ui/AiIslandPanel.qml ui/AiIslandEffortControl.qml ui/ScreenGlowWindow.qml
            ui/AiIslandDragArea.qml
            assets/mirrorfly-mark-light.svg config/theme.json)

    add_custom_command(TARGET MirrorflyOffice POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MirrorflyOffice>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/theme.json"
            "$<TARGET_FILE_DIR:MirrorflyOffice>/config/theme.json"
    )

    if(BUILD_TESTING)
        add_executable(mirrorfly_ai_island_tests tests/ai_island_tests.cpp
            src/ui/src/ai_island_trace.cpp src/ui/src/ai_island_trace.hpp
            src/ui/src/ai_island_client.cpp src/ui/src/ai_island_client.hpp)
        set_target_properties(mirrorfly_ai_island_tests PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_ai_island_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_ai_island_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
        if(WIN32)
            target_link_libraries(mirrorfly_ai_island_tests PRIVATE user32)
        endif()
        add_test(NAME ai_island_transport COMMAND mirrorfly_ai_island_tests)
        set_tests_properties(ai_island_transport PROPERTIES TIMEOUT 15)
        add_executable(mirrorfly_ai_island_launch_tests tests/ai_island_launch_tests.cpp)
        target_compile_definitions(mirrorfly_ai_island_launch_tests PRIVATE
            MIRRORFLY_AI_ISLAND_EXECUTABLE="$<TARGET_FILE:MirrorflyAiIsland>")
        target_link_libraries(mirrorfly_ai_island_launch_tests PRIVATE Qt6::Core Qt6::Network)
        add_dependencies(mirrorfly_ai_island_launch_tests MirrorflyAiIsland)
        add_test(NAME ai_island_hidden_launch COMMAND mirrorfly_ai_island_launch_tests)
        set_tests_properties(ai_island_hidden_launch PROPERTIES TIMEOUT 15)

        add_executable(mirrorfly_ai_island_glow_tests tests/ai_island_glow_tests.cpp)
        target_compile_definitions(mirrorfly_ai_island_glow_tests PRIVATE
            MIRRORFLY_GLOW_QML="${CMAKE_CURRENT_SOURCE_DIR}/ui/ScreenGlowWindow.qml")
        target_link_libraries(mirrorfly_ai_island_glow_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME ai_island_glow_geometry COMMAND mirrorfly_ai_island_glow_tests)
        set_tests_properties(ai_island_glow_geometry PROPERTIES TIMEOUT 15)

        add_executable(mirrorfly_ai_island_focus_tests tests/ai_island_focus_tests.cpp)
        target_compile_definitions(mirrorfly_ai_island_focus_tests PRIVATE
            MIRRORFLY_ISLAND_QML="${CMAKE_CURRENT_SOURCE_DIR}/ui/AiIslandWindow.qml"
            MIRRORFLY_THEME_JSON="${CMAKE_CURRENT_SOURCE_DIR}/config/theme.json")
        target_link_libraries(mirrorfly_ai_island_focus_tests PRIVATE
            Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
        add_test(NAME ai_island_input_focus COMMAND mirrorfly_ai_island_focus_tests)
        set_tests_properties(ai_island_input_focus PROPERTIES TIMEOUT 15)

        add_executable(mirrorfly_ai_settings_tests tests/ai_settings_tests.cpp)
        target_include_directories(mirrorfly_ai_settings_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_ai_settings_tests PRIVATE mirrorfly_ui Qt6::Core Qt6::Network)
        add_test(NAME ai_configuration_persistence COMMAND mirrorfly_ai_settings_tests)

        add_executable(mirrorfly_desktop_resident_tests tests/desktop_resident_tests.cpp)
        target_include_directories(mirrorfly_desktop_resident_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_desktop_resident_tests PRIVATE mirrorfly_ui Qt6::Gui Qt6::Quick)
        add_test(NAME desktop_resident_lifecycle COMMAND mirrorfly_desktop_resident_tests)
        set_tests_properties(desktop_resident_lifecycle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_startup_tests tests/startup_request_tests.cpp src/ui/src/startup_request.cpp)
        add_executable(mirrorfly_office_ai_image_library_tests tests/office_ai_image_library_tests.cpp)
        target_include_directories(mirrorfly_office_ai_image_library_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_office_ai_image_library_tests PRIVATE mirrorfly_ui Qt6::Gui Qt6::Network)
        add_test(NAME office_ai_image_library COMMAND mirrorfly_office_ai_image_library_tests)
        set_tests_properties(office_ai_image_library PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_office_ai_runtime_tests tests/office_ai_runtime_tests.cpp)
        add_executable(mirrorfly_spreadsheet_text_layout_tests tests/spreadsheet_text_layout_tests.cpp)
        target_include_directories(mirrorfly_spreadsheet_text_layout_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_spreadsheet_text_layout_tests PRIVATE mirrorfly_ui Qt6::Gui Qt6::Quick)
        add_test(NAME spreadsheet_text_layout COMMAND mirrorfly_spreadsheet_text_layout_tests)
        set_tests_properties(spreadsheet_text_layout PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        set_target_properties(mirrorfly_office_ai_runtime_tests PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_office_ai_runtime_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_office_ai_runtime_tests PRIVATE mirrorfly_ui)
        add_test(NAME office_ai_runtime COMMAND mirrorfly_office_ai_runtime_tests)
        set_tests_properties(office_ai_runtime PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_office_ai_tests tests/office_ai_tests.cpp)
        set_target_properties(mirrorfly_office_ai_tests PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_office_ai_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_office_ai_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Core Qt6::Gui Qt6::Quick)
        add_test(NAME office_ai_workflow COMMAND mirrorfly_office_ai_tests)
        set_tests_properties(office_ai_workflow PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_office_ai_agent_tests tests/office_ai_agent_tests.cpp)
        add_executable(mirrorfly_office_ai_context_tests tests/office_ai_context_tests.cpp)
        target_include_directories(mirrorfly_office_ai_context_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_office_ai_context_tests PRIVATE mirrorfly_ui mirrorfly_core Qt6::Network)
        add_test(NAME office_ai_context COMMAND mirrorfly_office_ai_context_tests)
        set_tests_properties(office_ai_context PROPERTIES TIMEOUT 30)
        add_executable(mirrorfly_office_ai_file_policy_tests tests/office_ai_file_policy_tests.cpp)
        set_target_properties(mirrorfly_office_ai_file_policy_tests PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_office_ai_file_policy_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_office_ai_file_policy_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Gui Qt6::Network)
        add_test(NAME office_ai_file_policy COMMAND mirrorfly_office_ai_file_policy_tests)
        set_tests_properties(office_ai_file_policy PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        set_target_properties(mirrorfly_office_ai_agent_tests PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_office_ai_agent_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_office_ai_agent_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Gui Qt6::Network Qt6::Quick)
        add_test(NAME office_ai_agent_protocol COMMAND mirrorfly_office_ai_agent_tests)
        set_tests_properties(office_ai_agent_protocol PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_office_ai_live_probe tests/office_ai_live_probe.cpp)
        set_target_properties(mirrorfly_office_ai_live_probe PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_office_ai_live_probe PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_office_ai_live_probe PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Gui Qt6::Network Qt6::Quick)
        add_executable(mirrorfly_office_ai_document_live_probe tests/office_ai_document_live_probe.cpp)
        target_include_directories(mirrorfly_office_ai_document_live_probe PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_office_ai_document_live_probe PRIVATE MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_office_ai_document_live_probe PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick)
        qt_add_resources(mirrorfly_office_ai_document_live_probe mirrorfly_document_probe_assets PREFIX "/mirrorfly" BASE "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES ${MIRRORFLY_UI_FILES} Logo/logo1.svg assets/mirrorfly-mark.svg
                assets/mirrorfly-mark-light.svg config/theme.json)
        add_executable(mirrorfly_office_ai_resilience_tests tests/office_ai_resilience_tests.cpp)
        set_target_properties(mirrorfly_office_ai_resilience_tests PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_office_ai_resilience_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_office_ai_resilience_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Gui Qt6::Network Qt6::Quick)
        add_test(NAME office_ai_resilience COMMAND mirrorfly_office_ai_resilience_tests)
        set_tests_properties(office_ai_resilience PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_canvas_routes_tests tests/canvas_routes_tests.cpp)
        target_include_directories(mirrorfly_canvas_routes_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_canvas_routes_tests PRIVATE mirrorfly_ui Qt6::Gui Qt6::Quick)
        add_test(NAME mindmap_routing COMMAND mirrorfly_canvas_routes_tests)
        set_tests_properties(mindmap_routing PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        target_include_directories(mirrorfly_startup_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_startup_tests PRIVATE mirrorfly_core Qt6::Core)
        add_test(NAME startup_file_request COMMAND mirrorfly_startup_tests)
        add_executable(mirrorfly_word_document_tests tests/word_document_tests.cpp)
        add_executable(mirrorfly_word_cell_border_tests tests/word_cell_border_tests.cpp)
        target_include_directories(mirrorfly_word_cell_border_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_word_cell_border_tests PRIVATE mirrorfly_ui mirrorfly_core Qt6::Gui)
        add_test(NAME word_cell_borders COMMAND mirrorfly_word_cell_border_tests)
        set_tests_properties(word_cell_borders PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_word_viewport_tests tests/word_viewport_tests.cpp)
        target_include_directories(mirrorfly_word_viewport_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_word_viewport_tests PRIVATE mirrorfly_ui mirrorfly_core Qt6::Gui Qt6::Quick Qt6::Qml)
        add_test(NAME word_viewport_repaint COMMAND mirrorfly_word_viewport_tests 1.0)
        set_tests_properties(word_viewport_repaint PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        foreach(scale IN ITEMS 0.75 1.25 1.5 2.0)
            add_test(NAME word_viewport_repaint_${scale} COMMAND mirrorfly_word_viewport_tests ${scale})
            set_tests_properties(word_viewport_repaint_${scale} PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
            if(WIN32)
                set_tests_properties(word_viewport_repaint_${scale} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
            endif()
        endforeach()
        add_executable(mirrorfly_word_image_tests tests/word_image_tests.cpp)
        target_include_directories(mirrorfly_word_image_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_word_image_tests PRIVATE mirrorfly_ui mirrorfly_core Qt6::Gui Qt6::Concurrent)
        add_test(NAME word_image_resources COMMAND mirrorfly_word_image_tests)
        set_tests_properties(word_image_resources PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        if(WIN32)
            add_executable(mirrorfly_word_image_render_tests tests/word_image_render_tests.cpp)
            target_include_directories(mirrorfly_word_image_render_tests PRIVATE src/ui/src)
            target_link_libraries(mirrorfly_word_image_render_tests PRIVATE mirrorfly_ui mirrorfly_core mirrorfly_platform Qt6::Gui Qt6::Quick Qt6::Qml d3d11)
            add_test(NAME word_image_quick_render COMMAND mirrorfly_word_image_render_tests)
            set_tests_properties(word_image_quick_render PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=windows")
        endif()
        add_executable(mirrorfly_word_corpus_probe tests/word_corpus_probe.cpp)
        target_include_directories(mirrorfly_word_corpus_probe PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_word_corpus_probe PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Gui Qt6::Quick Qt6::Qml)
        target_include_directories(mirrorfly_word_document_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_word_document_tests PRIVATE mirrorfly_ui mirrorfly_core Qt6::Core Qt6::Gui Qt6::Quick)
        add_test(NAME word_document_transactions COMMAND mirrorfly_word_document_tests)
        add_executable(mirrorfly_word_tab_tests tests/word_tab_tests.cpp)
        target_include_directories(mirrorfly_word_tab_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_word_tab_tests PRIVATE MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_word_tab_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Quick Qt6::Qml)
        add_test(NAME word_tab_editing COMMAND mirrorfly_word_tab_tests)
        set_tests_properties(word_tab_editing PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_word_list_tests tests/word_list_tests.cpp)
        target_include_directories(mirrorfly_word_list_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_word_list_tests PRIVATE MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_word_list_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Quick Qt6::Qml)
        add_test(NAME word_list_editing COMMAND mirrorfly_word_list_tests)
        set_tests_properties(word_list_editing PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_word_decoration_tests tests/word_decoration_tests.cpp)
        target_include_directories(mirrorfly_word_decoration_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_word_decoration_tests PRIVATE MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_word_decoration_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Quick Qt6::Qml)
        add_test(NAME word_character_decorations COMMAND mirrorfly_word_decoration_tests)
        add_executable(mirrorfly_word_unicode_tests tests/word_unicode_tests.cpp)
        target_include_directories(mirrorfly_word_unicode_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_word_unicode_tests PRIVATE MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_word_unicode_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Quick Qt6::Qml)
        add_test(NAME word_unicode_interop COMMAND mirrorfly_word_unicode_tests)
        set_tests_properties(word_unicode_interop PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        set_tests_properties(word_character_decorations PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        set_tests_properties(word_document_transactions PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_pdf_export_tests tests/pdf_export_tests.cpp)
        target_include_directories(mirrorfly_pdf_export_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_pdf_export_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Core Qt6::Gui Qt6::Quick)
        add_test(NAME pdf_export_pipeline COMMAND mirrorfly_pdf_export_tests)
        set_tests_properties(pdf_export_pipeline PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_automation_tests tests/automation_tests.cpp src/ui/src/automation_bridge.cpp src/ui/src/automation_bridge.hpp src/ui/src/automation_contract.cpp)
        set_target_properties(mirrorfly_automation_tests PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_automation_tests PRIVATE src/ui/src src/ui/include)
        target_link_libraries(mirrorfly_automation_tests PRIVATE Qt6::Core Qt6::Qml)
        add_test(NAME automation_contract COMMAND mirrorfly_automation_tests)
        set_tests_properties(automation_contract PROPERTIES TIMEOUT 30)
        add_executable(mirrorfly_canvas_ui_tests tests/canvas_ui_tests.cpp tests/support/office_ai_qml_scenario.cpp tests/support/office_ai_multiformat_scenario.cpp)
        target_include_directories(mirrorfly_canvas_ui_tests PRIVATE src/ui/src tests)
        target_compile_definitions(mirrorfly_canvas_ui_tests PRIVATE MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_canvas_ui_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
        qt_add_resources(mirrorfly_canvas_ui_tests mirrorfly_canvas_test_assets PREFIX "/mirrorfly" BASE "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES ${MIRRORFLY_UI_FILES} Logo/logo1.svg assets/mirrorfly-mark.svg
                assets/mirrorfly-mark-light.svg config/theme.json)
        add_test(NAME canvas_workbenches COMMAND mirrorfly_canvas_ui_tests)
        add_test(NAME office_ai_qml_multiformat COMMAND mirrorfly_canvas_ui_tests --ai-multiformat)
        set_tests_properties(office_ai_qml_multiformat PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_test(NAME office_ai_qml_complex COMMAND mirrorfly_canvas_ui_tests --ai-scenario)
        add_test(NAME office_ai_qml_batched COMMAND mirrorfly_canvas_ui_tests --ai-batched)
        set_tests_properties(office_ai_qml_batched PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        set_tests_properties(office_ai_qml_complex PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        set_tests_properties(canvas_workbenches PROPERTIES TIMEOUT 40 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        foreach(module pdf mindmap)
            add_executable(mirrorfly_${module}_storage_tests tests/${module}_storage_tests.cpp)
            target_link_libraries(mirrorfly_${module}_storage_tests PRIVATE mirrorfly_platform Qt6::Core)
            add_test(NAME ${module}_storage COMMAND mirrorfly_${module}_storage_tests)
            set_tests_properties(${module}_storage PROPERTIES TIMEOUT 30)
        endforeach()
        add_executable(mirrorfly_word_ui_tests tests/word_ui_tests.cpp)
        target_include_directories(mirrorfly_word_ui_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_word_ui_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
            MIRRORFLY_TEST_BUILD_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(mirrorfly_word_ui_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME word_workbench COMMAND mirrorfly_word_ui_tests)
        set_tests_properties(word_workbench PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_spreadsheet_interop_tests tests/spreadsheet_interop_tests.cpp)
        add_executable(mirrorfly_spreadsheet_corpus_probe tests/spreadsheet_corpus_probe.cpp)
        target_link_libraries(mirrorfly_spreadsheet_corpus_probe PRIVATE mirrorfly_platform Qt6::Core)
        target_compile_definitions(mirrorfly_spreadsheet_interop_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
            MIRRORFLY_TEST_BUILD_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(mirrorfly_spreadsheet_interop_tests PRIVATE mirrorfly_platform Qt6::Core)
        add_test(NAME spreadsheet_interop COMMAND mirrorfly_spreadsheet_interop_tests)
        set_tests_properties(spreadsheet_interop PROPERTIES TIMEOUT 30)

        add_executable(mirrorfly_spreadsheet_ui_tests tests/spreadsheet_ui_tests.cpp)
        target_include_directories(mirrorfly_spreadsheet_ui_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_spreadsheet_ui_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_spreadsheet_ui_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME spreadsheet_workbench COMMAND mirrorfly_spreadsheet_ui_tests)
        set_tests_properties(spreadsheet_workbench PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_spreadsheet_storage_tests tests/spreadsheet_storage_tests.cpp)
        target_link_libraries(mirrorfly_spreadsheet_storage_tests PRIVATE mirrorfly_platform Qt6::Core)
        add_test(NAME spreadsheet_storage COMMAND mirrorfly_spreadsheet_storage_tests)
        set_tests_properties(spreadsheet_core spreadsheet_storage PROPERTIES TIMEOUT 30)

        add_executable(mirrorfly_platform_tests tests/platform_tests.cpp)
        target_link_libraries(mirrorfly_platform_tests PRIVATE mirrorfly_platform Qt6::Core)
        add_test(NAME platform_persistence COMMAND mirrorfly_platform_tests)

        add_executable(mirrorfly_text_storage_tests tests/text_storage_tests.cpp)
        target_link_libraries(mirrorfly_text_storage_tests PRIVATE mirrorfly_platform Qt6::Core)
        add_test(NAME text_storage COMMAND mirrorfly_text_storage_tests)

        add_executable(mirrorfly_presentation_storage_tests tests/presentation_storage_tests.cpp)
        target_compile_definitions(mirrorfly_presentation_storage_tests PRIVATE
            MIRRORFLY_TEST_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        target_link_libraries(mirrorfly_presentation_storage_tests PRIVATE mirrorfly_platform Qt6::Core)
        add_test(NAME presentation_package COMMAND mirrorfly_presentation_storage_tests)
        set_tests_properties(presentation_scene presentation_package PROPERTIES TIMEOUT 30)

        add_executable(mirrorfly_text_bridge_tests
            tests/text_bridge_tests.cpp src/ui/src/text_bridge.cpp src/ui/src/text_content.cpp src/ui/src/text_search.cpp src/ui/src/text_search.hpp src/ui/src/text_bridge.hpp
            src/ui/src/document_view.cpp src/ui/src/document_view.hpp)
        set_target_properties(mirrorfly_text_bridge_tests PROPERTIES AUTOMOC ON)
        target_include_directories(mirrorfly_text_bridge_tests PRIVATE src/ui/src src/ui/include)
        target_link_libraries(mirrorfly_text_bridge_tests PRIVATE mirrorfly_platform Qt6::Core Qt6::Concurrent)
        add_test(NAME text_editor_state COMMAND mirrorfly_text_bridge_tests)
        set_tests_properties(text_codec text_storage text_editor_state PROPERTIES TIMEOUT 30)

        add_executable(mirrorfly_markdown_bridge_tests
            tests/markdown_bridge_tests.cpp tests/support/markdown_block_container_tests.cpp tests/support/markdown_block_container_tests.hpp tests/support/markdown_ordered_tests.cpp tests/support/markdown_ordered_tests.hpp tests/support/markdown_container_tests.cpp
            tests/support/markdown_container_tests.hpp tests/support/markdown_paragraph_styles_tests.cpp
            tests/support/markdown_paragraph_styles_tests.hpp tests/support/markdown_code_span_tests.cpp tests/support/markdown_whitespace_tests.cpp tests/support/markdown_code_span_tests.hpp tests/support/markdown_image_tests.cpp tests/support/markdown_image_tests.hpp tests/support/markdown_import_tests.cpp tests/support/markdown_import_tests.hpp tests/support/markdown_heading_tests.cpp tests/support/markdown_heading_tests.hpp tests/support/markdown_link_styles_tests.cpp
            tests/support/markdown_link_styles_tests.hpp src/ui/src/editor_tools.cpp src/ui/src/editor_tools.hpp
            src/ui/src/markdown_inline.cpp src/ui/src/markdown_inline.hpp
            src/ui/src/markdown_inline_serialization.cpp src/ui/src/markdown_inline_code_serialization.cpp src/ui/src/markdown_inline_serialization.hpp
            src/ui/src/markdown_table.cpp src/ui/src/markdown_table_serialization.cpp src/ui/src/markdown_table_format.hpp src/ui/src/markdown_table.hpp
            src/ui/src/markdown_blocks.cpp src/ui/src/markdown_list_tree.cpp src/ui/src/markdown_list_move.cpp src/ui/src/markdown_list_tree.hpp src/ui/src/markdown_block_style.cpp src/ui/src/markdown_heading_style.cpp src/ui/src/markdown_fragment.cpp src/ui/src/markdown_paragraph.cpp src/ui/src/markdown_paragraph_export.cpp src/ui/src/markdown_paragraph.hpp src/ui/src/markdown_rule.cpp src/ui/src/markdown_rule.hpp src/ui/src/markdown_blocks.hpp
            src/ui/src/markdown_code.cpp src/ui/src/markdown_code_import.cpp src/ui/src/markdown_code_serialization.cpp src/ui/src/markdown_code_serialization.hpp src/ui/src/markdown_code.hpp
            src/ui/src/markdown_link.cpp src/ui/src/markdown_link.hpp
            src/ui/src/markdown_link_import.cpp
            src/ui/src/markdown_link_range.cpp src/ui/src/markdown_link_range.hpp
            src/ui/src/markdown_link_serialization.cpp src/ui/src/markdown_link_serialization.hpp
            src/ui/src/markdown_break.cpp src/ui/src/markdown_break.hpp
        src/ui/src/markdown_input.cpp src/ui/src/markdown_input_conversion.cpp src/ui/src/markdown_input.hpp
        src/ui/src/markdown_image.cpp src/ui/src/markdown_image.hpp src/ui/src/markdown_image_resource.cpp src/ui/src/markdown_image_serialization.cpp
        src/ui/src/markdown_document.cpp src/ui/src/markdown_load.cpp src/ui/src/markdown_context.cpp src/ui/src/markdown_document.hpp
        src/ui/src/markdown_validation.cpp src/ui/src/markdown_serialization.cpp src/ui/src/markdown_container.cpp src/ui/src/markdown_container_state.cpp src/ui/src/markdown_container_import.cpp src/ui/src/markdown_container_tokens.cpp src/ui/src/markdown_container.hpp)
        set_target_properties(mirrorfly_markdown_bridge_tests PROPERTIES AUTOMOC ON)
        target_compile_definitions(mirrorfly_markdown_bridge_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_include_directories(mirrorfly_markdown_bridge_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_markdown_bridge_tests PRIVATE mirrorfly_core
            Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME markdown_editor_transactions COMMAND mirrorfly_markdown_bridge_tests)
        set_tests_properties(markdown_operations markdown_editor_transactions PROPERTIES
            TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_presentation_ui_tests tests/presentation_ui_tests.cpp)
        add_executable(mirrorfly_presentation_image_export_tests tests/presentation_image_export_tests.cpp)
        target_include_directories(mirrorfly_presentation_image_export_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_presentation_image_export_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Quick)
        add_test(NAME presentation_image_export COMMAND mirrorfly_presentation_image_export_tests)
        set_tests_properties(presentation_image_export PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_presentation_corpus_probe tests/presentation_corpus_probe.cpp)
        add_executable(mirrorfly_presentation_edit_performance_tests tests/presentation_edit_performance_tests.cpp)
        add_executable(mirrorfly_presentation_edit_render_tests tests/presentation_edit_render_tests.cpp)
        target_include_directories(mirrorfly_presentation_edit_render_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_presentation_edit_render_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
            MIRRORFLY_TEST_BUILD_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(mirrorfly_presentation_edit_render_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME presentation_edit_render COMMAND mirrorfly_presentation_edit_render_tests)
        set_tests_properties(presentation_edit_render PROPERTIES TIMEOUT 40 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_presentation_pattern_tests tests/presentation_pattern_tests.cpp)
        target_include_directories(mirrorfly_presentation_pattern_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_presentation_pattern_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
            MIRRORFLY_TEST_BUILD_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(mirrorfly_presentation_pattern_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME presentation_patterns_and_art COMMAND mirrorfly_presentation_pattern_tests)
        set_tests_properties(presentation_patterns_and_art PROPERTIES TIMEOUT 90
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        target_include_directories(mirrorfly_presentation_edit_performance_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_presentation_edit_performance_tests PRIVATE mirrorfly_ui mirrorfly_platform Qt6::Core Qt6::Gui Qt6::Quick)
        target_include_directories(mirrorfly_presentation_corpus_probe PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_presentation_corpus_probe PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Gui Qt6::Quick)
        target_link_libraries(mirrorfly_presentation_ui_tests PRIVATE mfreadwrite)
        target_include_directories(mirrorfly_presentation_ui_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_presentation_ui_tests PRIVATE
            MIRRORFLY_TEST_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        target_link_libraries(mirrorfly_presentation_ui_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME presentation_display COMMAND mirrorfly_presentation_ui_tests)
        set_tests_properties(presentation_display PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_document_routing_tests tests/document_routing_tests.cpp)
        target_include_directories(mirrorfly_document_routing_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_document_routing_tests PRIVATE
            MIRRORFLY_TEST_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        target_link_libraries(mirrorfly_document_routing_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME document_mode_handoff COMMAND mirrorfly_document_routing_tests)
        set_tests_properties(document_mode_handoff PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_presentation_workbench_tests tests/presentation_workbench_tests.cpp)
        find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
        target_include_directories(mirrorfly_presentation_workbench_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_presentation_workbench_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
            MIRRORFLY_TEST_BUILD_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(mirrorfly_presentation_workbench_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test d3d11)
        add_test(NAME presentation_workbench_layout COMMAND mirrorfly_presentation_workbench_tests)
        set_tests_properties(presentation_workbench_layout PROPERTIES TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_presentation_canvas_tests tests/presentation_canvas_tests.cpp)
        target_include_directories(mirrorfly_presentation_canvas_tests PRIVATE src/ui/src)
        target_compile_definitions(mirrorfly_presentation_canvas_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
            MIRRORFLY_TEST_BUILD_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(mirrorfly_presentation_canvas_tests PRIVATE mirrorfly_ui mirrorfly_platform
            Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME presentation_canvas_editing COMMAND mirrorfly_presentation_canvas_tests)
        set_tests_properties(presentation_canvas_editing PROPERTIES TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_theme_tests tests/theme_tests.cpp src/ui/src/theme.cpp)
        target_include_directories(mirrorfly_theme_tests PRIVATE src/ui/src)
        target_link_libraries(mirrorfly_theme_tests PRIVATE Qt6::Core Qt6::Gui)
        qt_add_resources(mirrorfly_theme_tests mirrorfly_test_theme
            PREFIX "/mirrorfly"
            BASE "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES config/theme.json
        )
        add_test(NAME theme_configuration COMMAND mirrorfly_theme_tests)
        set_tests_properties(theme_configuration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_frontend_polish_tests tests/frontend_polish_tests.cpp)
        target_compile_definitions(mirrorfly_frontend_polish_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_frontend_polish_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
        add_test(NAME frontend_polish_behavior COMMAND mirrorfly_frontend_polish_tests)
        set_tests_properties(frontend_polish_behavior PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(mirrorfly_home_workspace_tests tests/home_workspace_tests.cpp)
        target_compile_definitions(mirrorfly_home_workspace_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_home_workspace_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
        add_test(NAME home_workspace_layout COMMAND mirrorfly_home_workspace_tests)
        set_tests_properties(home_workspace_layout PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(mirrorfly_ai_endpoint_tests tests/ai_endpoint_tests.cpp)
        target_include_directories(mirrorfly_ai_endpoint_tests PRIVATE src/ui/src tests)
        target_link_libraries(mirrorfly_ai_endpoint_tests PRIVATE mirrorfly_ui Qt6::Core Qt6::Network)
        add_test(NAME ai_custom_endpoints COMMAND mirrorfly_ai_endpoint_tests)
        set_tests_properties(ai_custom_endpoints PROPERTIES TIMEOUT 15)
        add_executable(mirrorfly_feature_intro_tests tests/feature_intro_tests.cpp)
        target_compile_definitions(mirrorfly_feature_intro_tests PRIVATE
            MIRRORFLY_TEST_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(mirrorfly_feature_intro_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
        add_test(NAME feature_intro_sequence COMMAND mirrorfly_feature_intro_tests)
        set_tests_properties(feature_intro_sequence PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        if(WIN32)
            set_tests_properties(presentation_patterns_and_art word_tab_editing word_list_editing pdf_export_pipeline word_workbench word_document_transactions word_character_decorations word_viewport_repaint spreadsheet_workbench markdown_editor_transactions presentation_display document_mode_handoff
                presentation_workbench_layout presentation_canvas_editing theme_configuration feature_intro_sequence
                PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
            add_executable(mirrorfly_markdown_render_tests tests/markdown_render_tests.cpp
                src/ui/src/editor_tools.cpp src/ui/src/editor_tools.hpp
                src/ui/src/markdown_inline.cpp src/ui/src/markdown_inline.hpp
                src/ui/src/markdown_inline_serialization.cpp src/ui/src/markdown_inline_code_serialization.cpp src/ui/src/markdown_inline_serialization.hpp
                src/ui/src/markdown_table.cpp src/ui/src/markdown_table_serialization.cpp src/ui/src/markdown_table_format.hpp src/ui/src/markdown_table.hpp
                src/ui/src/markdown_blocks.cpp src/ui/src/markdown_list_tree.cpp src/ui/src/markdown_list_move.cpp src/ui/src/markdown_list_tree.hpp src/ui/src/markdown_block_style.cpp src/ui/src/markdown_heading_style.cpp src/ui/src/markdown_fragment.cpp src/ui/src/markdown_paragraph.cpp src/ui/src/markdown_paragraph_export.cpp src/ui/src/markdown_paragraph.hpp src/ui/src/markdown_rule.cpp src/ui/src/markdown_rule.hpp src/ui/src/markdown_blocks.hpp
                src/ui/src/markdown_code.cpp src/ui/src/markdown_code_import.cpp src/ui/src/markdown_code_serialization.cpp src/ui/src/markdown_code_serialization.hpp src/ui/src/markdown_code.hpp
                src/ui/src/markdown_link.cpp src/ui/src/markdown_link.hpp
                src/ui/src/markdown_link_import.cpp
                src/ui/src/markdown_link_range.cpp src/ui/src/markdown_link_range.hpp
                src/ui/src/markdown_link_serialization.cpp src/ui/src/markdown_link_serialization.hpp
                src/ui/src/markdown_break.cpp src/ui/src/markdown_break.hpp
        src/ui/src/markdown_input.cpp src/ui/src/markdown_input_conversion.cpp src/ui/src/markdown_input.hpp
        src/ui/src/markdown_image.cpp src/ui/src/markdown_image.hpp src/ui/src/markdown_image_resource.cpp src/ui/src/markdown_image_serialization.cpp
        src/ui/src/markdown_document.cpp src/ui/src/markdown_load.cpp src/ui/src/markdown_context.cpp src/ui/src/markdown_document.hpp
        src/ui/src/markdown_validation.cpp src/ui/src/markdown_serialization.cpp src/ui/src/markdown_container.cpp src/ui/src/markdown_container_state.cpp src/ui/src/markdown_container_import.cpp src/ui/src/markdown_container_tokens.cpp src/ui/src/markdown_container.hpp)
            set_target_properties(mirrorfly_markdown_render_tests PROPERTIES AUTOMOC ON)
            target_include_directories(mirrorfly_markdown_render_tests PRIVATE src/ui/src)
            target_link_libraries(mirrorfly_markdown_render_tests PRIVATE
                mirrorfly_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick d3d11)
            add_test(NAME markdown_render_scene COMMAND mirrorfly_markdown_render_tests)
            set_tests_properties(markdown_render_scene PROPERTIES TIMEOUT 60
                ENVIRONMENT "QT_QPA_PLATFORM=windows")
        endif()
    endif()

    install(TARGETS MirrorflyOffice MirrorflyAiIsland BUNDLE DESTINATION . RUNTIME DESTINATION .)
    install(FILES config/theme.json DESTINATION config)
    install(FILES THIRD_PARTY_NOTICES.md DESTINATION licenses)
    install(FILES LICENSE COMMERCIAL.md QT-LICENSING.md DESTINATION .)
    install(DIRECTORY licenses/qt/ DESTINATION licenses/qt)
    install(FILES "${MIRRORFLY_PDFIUM_ROOT}/bin/pdfium.dll" DESTINATION .)
    install(FILES "${MIRRORFLY_PDFIUM_ROOT}/LICENSE" "${MIRRORFLY_PDFIUM_ROOT}/ORIGIN.md"
        DESTINATION licenses/pdfium)
    install(DIRECTORY "${MIRRORFLY_PDFIUM_ROOT}/licenses/" DESTINATION licenses/pdfium/dependencies)
    install(FILES third_party/md4c/LICENSE.txt third_party/md4c/SOURCE.md
        third_party/md4c/mirrorfly_source_ranges.patch DESTINATION licenses/md4c)
    install(FILES third_party/utfcpp/LICENSE DESTINATION licenses/utfcpp)
    install(FILES third_party/utfcpp/ORIGIN.md DESTINATION licenses/utfcpp)
    install(FILES third_party/miniz/LICENSE third_party/miniz/ORIGIN.md DESTINATION licenses/miniz)
    install(FILES third_party/pugixml/LICENSE.md third_party/pugixml/ORIGIN.md DESTINATION licenses/pugixml)
    install(FILES third_party/unihan/LICENSE.txt third_party/unihan/ORIGIN.md DESTINATION licenses/unihan)
    install(FILES third_party/poi/LICENSE third_party/poi/NOTICE third_party/poi/ORIGIN.md DESTINATION licenses/poi)
endif()
