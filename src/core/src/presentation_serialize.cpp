#include "presentation_edit_common.hpp"
#include "presentation_edit_native.hpp"
#include "presentation_preservation.hpp"
#include "presentation_table_style.hpp"
#include "presentation_theme_package.hpp"
#include "presentation_transition.hpp"

#include <mirrorfly/presentation.hpp>
#include <mirrorfly/presentation_geometry.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    using mirrorfly::presentation_edit_common::finite;
    using mirrorfly::presentation_edit_common::image_extension;
    using mirrorfly::presentation_edit_common::saturated_add;
    using mirrorfly::presentation_edit_common::valid_xml_text;
    using mirrorfly::presentation_edit_native::decompose_transform;
    using mirrorfly::presentation_edit_native::emu;
    using mirrorfly::presentation_edit_native::shape_xml;
    using mirrorfly::presentation_edit_native::solid_fill;
    using mirrorfly::presentation_edit_native::valid_color;

    struct ImageTarget
    {
        std::string source_path;
        std::string package_path;
        std::string mime_type;
        const std::string* bytes = nullptr;
    };

    void add_part(std::vector<mirrorfly::PresentationPart>& parts, std::string path, std::string bytes)
    {
        parts.push_back({std::move(path), std::move(bytes)});
    }

    bool has_suffix(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool is_xml_part(const std::string& path)
    {
        return has_suffix(path, ".xml") || has_suffix(path, ".rels");
    }

}

namespace mirrorfly
{
    PresentationPackageResult serialize_presentation(const PresentationScene& scene)
    {
        if (scene.source_package)
            return preserved_presentation_package(scene);
        PresentationPackageResult result;
        if (!scene.native_editable || scene.slides.empty() ||
            scene.slides.size() > maximum_presentation_slides || !finite(scene.width) ||
            !finite(scene.height) || scene.width <= 0 || scene.height <= 0)
        {
            result.error = PresentationError::InvalidPackage;
            result.message = "演示文稿状态无效，不能保存。";
            return result;
        }
        for (const auto& slide : scene.slides)
        {
            for (const auto& shape : slide.shapes)
            {
                if (!shape.geometry_definition.empty() &&
                    !presentation_geometry(
                        shape.geometry, shape.width, shape.height, shape.geometry_definition)
                        .error.empty())
                {
                    result.error = PresentationError::InvalidPackage;
                    result.message = "演示文稿包含无法安全保存的图形定义。";
                    return result;
                }
                if (!decompose_transform(shape))
                {
                    result.error = PresentationError::InvalidPackage;
                    result.message = "演示文稿包含无法安全转换的倾斜或无效对象变换。";
                    return result;
                }
            }
        }

        std::vector<ImageTarget> images;
        std::map<std::string, std::size_t> image_lookup;
        for (const auto& image : scene.images)
        {
            const auto extension = image_extension(image.mime_type);
            if (extension.empty() || !image.bytes || image.bytes->empty() ||
                image.bytes->size() > maximum_presentation_part_bytes || !valid_xml_text(image.path))
            {
                result.error = PresentationError::InvalidImage;
                result.message = "演示文稿包含无法保存的图片。";
                return result;
            }
            if (!image_lookup.count(image.path))
            {
                const auto index = images.size() + 1;
                image_lookup.emplace(image.path, images.size());
                images.push_back({image.path, "ppt/media/image" + std::to_string(index) + "." + extension,
                    image.mime_type, image.bytes.get()});
            }
        }

        const std::array<std::string, 6> fallback_palette{
            "#292724", "#FFFFFF", "#F4F0E9", "#6F7D69", "#B77746", "#9A8978"};
        std::vector<std::array<std::string, 6>> palettes;
        std::vector<std::size_t> slide_themes;
        for (const auto& slide : scene.slides)
        {
            const auto& palette = slide.authored_theme ? slide.authored_palette : fallback_palette;
            if (!std::all_of(palette.begin(), palette.end(), valid_color))
            {
                result.error = PresentationError::InvalidPackage;
                result.message = "模板主题配色无效。";
                return result;
            }
            auto found = std::find(palettes.begin(), palettes.end(), palette);
            if (found == palettes.end())
            {
                palettes.push_back(palette);
                slide_themes.push_back(palettes.size());
            }
            else
            {
                slide_themes.push_back(static_cast<std::size_t>(found - palettes.begin()) + 1);
            }
        }

        // clang-format off: long Open XML fragments use fixed four-space indentation.
        std::ostringstream content_types;
        content_types
            << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" "
            "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
            "<Override PartName=\"/ppt/presentation.xml\" "
            "ContentType=\"application/"
            "vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
            "<Override PartName=\"/ppt/presProps.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presProps+xml\"/>"
            "<Override PartName=\"/ppt/tableStyles.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.tableStyles+xml\"/"
            ">"
            "<Override PartName=\"/docProps/core.xml\" "
            "ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
            "<Override PartName=\"/docProps/app.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
            "<Override PartName=\"/docProps/custom.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.custom-properties+xml\"/>";
        for (std::size_t index = 0; index < palettes.size(); ++index)
        {
            content_types << "<Override PartName=\"/ppt/slideMasters/slideMaster" << index + 1
                        << ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument."
                            "presentationml.slideMaster+xml\"/>";
            content_types << "<Override PartName=\"/ppt/slideLayouts/slideLayout" << index + 1
                        << ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument."
                            "presentationml.slideLayout+xml\"/>";
            content_types << "<Override PartName=\"/ppt/theme/theme" << index + 1
                        << ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>";
        }
        for (std::size_t index = 0; index < scene.slides.size(); ++index)
        {
            content_types
                << "<Override PartName=\"/ppt/slides/slide" << index + 1
                << ".xml\" "
                "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>";
        }
        for (const auto& image : images)
        {
            content_types << "<Override PartName=\"/" << image.package_path << "\" ContentType=\""
                        << image.mime_type << "\"/>";
        }
        content_types << "</Types>";
        add_part(result.parts, "[Content_Types].xml", content_types.str());

        add_part(result.parts, "_rels/.rels",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
            "Target=\"ppt/presentation.xml\"/>"
            "<Relationship Id=\"rId2\" "
            "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
            "Target=\"docProps/core.xml\"/>"
            "<Relationship Id=\"rId3\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
            "extended-properties\" Target=\"docProps/app.xml\"/>"
            "<Relationship Id=\"rId4\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties\" "
            "Target=\"docProps/custom.xml\"/>"
            "</Relationships>");

        std::ostringstream presentation;
        presentation << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                        "<p:presentation xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                        "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
                        "<p:sldMasterIdLst>";
        for (std::size_t index = 0; index < palettes.size(); ++index)
            presentation << "<p:sldMasterId id=\"" << 2147483648ULL + index << "\" r:id=\"rId"
                        << index + 1 << "\"/>";
        presentation << "</p:sldMasterIdLst><p:sldIdLst>";
        for (std::size_t index = 0; index < scene.slides.size(); ++index)
        {
            presentation << "<p:sldId id=\"" << 256 + index << "\" r:id=\"rId"
                        << palettes.size() + index + 1 << "\"/>";
        }
        presentation << "</p:sldIdLst><p:sldSz cx=\"" << emu(scene.width) << "\" cy=\"" << emu(scene.height)
                    << "\" type=\"screen16x9\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/>"
                        "<p:defaultTextStyle/></p:presentation>";
        add_part(result.parts, "ppt/presentation.xml", presentation.str());

        std::ostringstream presentation_rels;
        presentation_rels
            << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
        for (std::size_t index = 0; index < palettes.size(); ++index)
            presentation_rels
                << "<Relationship Id=\"rId" << index + 1
                << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                "slideMaster\" Target=\"slideMasters/slideMaster"
                << index + 1 << ".xml\"/>";
        for (std::size_t index = 0; index < scene.slides.size(); ++index)
        {
            presentation_rels
                << "<Relationship Id=\"rId" << palettes.size() + index + 1
                << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" "
                "Target=\"slides/slide"
                << index + 1 << ".xml\"/>";
        }
        const auto auxiliary = scene.slides.size() + palettes.size() + 1;
        presentation_rels
            << "<Relationship Id=\"rId" << auxiliary
            << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/presProps\" "
            "Target=\"presProps.xml\"/>"
            << "<Relationship Id=\"rId" << auxiliary + 1
            << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/tableStyles\" "
            "Target=\"tableStyles.xml\"/>"
            << "</Relationships>";
        add_part(result.parts, "ppt/_rels/presentation.xml.rels", presentation_rels.str());

        for (std::size_t slide_index = 0; slide_index < scene.slides.size(); ++slide_index)
        {
            const auto& slide = scene.slides[slide_index];
            std::map<std::string, std::string> relations;
            std::size_t relationship_index = 2;
            for (const auto& shape : slide.shapes)
            {
                if (!shape.image_path.empty() && image_lookup.count(shape.image_path) &&
                    !relations.count(shape.image_path))
                {
                    relations.emplace(shape.image_path, "rId" + std::to_string(relationship_index++));
                }
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    if (!paragraph.bullet_image_path.empty() && image_lookup.count(paragraph.bullet_image_path) &&
                        !relations.count(paragraph.bullet_image_path))
                    {
                        relations.emplace(
                            paragraph.bullet_image_path, "rId" + std::to_string(relationship_index++));
                    }
                }
            }
            std::ostringstream slide_xml;
            slide_xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                        "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                        "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"";
            if (slide.hidden)
            {
                slide_xml << " show=\"0\"";
            }
            slide_xml << "><p:cSld>";
            if (!slide.background.color.empty() || !slide.background.stops.empty())
            {
                slide_xml << "<p:bg><p:bgPr>" << solid_fill(slide.background)
                        << "<a:effectLst/></p:bgPr></p:bg>";
            }
            slide_xml << "<p:spTree>" << fixed_group_tree();
            for (std::size_t shape_index = 0; shape_index < slide.shapes.size(); ++shape_index)
            {
                slide_xml << shape_xml(slide.shapes[shape_index], shape_index, relations);
            }
            slide_xml << "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>"
                << presentation_transition_xml(slide.transition) << "</p:sld>";
            add_part(
                result.parts, "ppt/slides/slide" + std::to_string(slide_index + 1) + ".xml", slide_xml.str());

            std::ostringstream rels;
            rels << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                    "<Relationship Id=\"rId1\" "
                    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                    "slideLayout\" Target=\"../slideLayouts/slideLayout"
                << slide_themes[slide_index] << ".xml\"/>";
            for (const auto& relation : relations)
            {
                const auto target = images[image_lookup.at(relation.first)].package_path.substr(4);
                rels << "<Relationship Id=\"" << relation.second
                    << "\" "
                        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                        "Target=\"../"
                    << target << "\"/>";
            }
            rels << "</Relationships>";
            add_part(result.parts, "ppt/slides/_rels/slide" + std::to_string(slide_index + 1) + ".xml.rels",
                rels.str());
        }

        if (!append_authored_theme_parts(result.parts, palettes))
        {
            result.error = PresentationError::InvalidXml;
            result.message = "内置主题无法写入。";
            return result;
        }
        add_part(result.parts, "ppt/presProps.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><p:presentationPr "
            "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"/>");
        add_part(result.parts, "ppt/tableStyles.xml", authored_table_styles_xml());
        add_part(result.parts, "docProps/core.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><cp:coreProperties "
            "xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
            "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
            "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dc:title>Mirrorfly Office "
            "演示文稿</dc:title><cp:lastModifiedBy>Mirrorfly Office</cp:lastModifiedBy></cp:coreProperties>");
        add_part(result.parts, "docProps/app.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Properties "
            "xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\" "
            "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/"
            "docPropsVTypes\"><Application>Mirrorfly "
            "Office</Application><AppVersion>0.7.0</AppVersion><PresentationFormat>宽屏</"
            "PresentationFormat><Slides>" +
                std::to_string(scene.slides.size()) +
                "</Slides><Notes>0</Notes><HiddenSlides>0</HiddenSlides></Properties>");
        add_part(result.parts, "docProps/custom.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Properties "
            "xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/custom-properties\" "
            "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\"><property "
            "fmtid=\"{D5CDD505-2E9C-101B-9397-08002B2CF9AE}\" pid=\"2\" "
            "name=\"MirrorflyOfficeEditable\"><vt:lpwstr>1</vt:lpwstr></property><property "
            "fmtid=\"{D5CDD505-2E9C-101B-9397-08002B2CF9AE}\" pid=\"3\" "
            "name=\"MirrorflyOfficeSchema\"><vt:lpwstr>1</vt:lpwstr></property></Properties>");
        for (const auto& image : images)
        {
            add_part(result.parts, image.package_path, *image.bytes);
        }

        // clang-format on
        std::size_t expanded = 0;
        for (const auto& part : result.parts)
        {
            expanded = saturated_add(expanded, part.bytes.size());
            const auto part_limit =
                is_xml_part(part.path) ? maximum_presentation_xml_bytes : maximum_presentation_part_bytes;
            if (part.bytes.size() > part_limit || expanded > maximum_presentation_expanded_bytes ||
                result.parts.size() > maximum_presentation_parts)
            {
                result.parts.clear();
                result.error = PresentationError::TooLarge;
                result.message = "保存内容超过演示文稿限制。";
                return result;
            }
        }
        return result;
    }
}
