#include "presentation_animation_painter.hpp"
#include "presentation_bridge.hpp"
#include "presentation_fill_renderer.hpp"
#include "presentation_geometry_renderer.hpp"
#include "presentation_graphics_fixture.hpp"
#include "presentation_media_fixture.hpp"
#include "presentation_scene.hpp"
#include "presentation_semantics.hpp"
#include "presentation_table_fixture.hpp"
#include "presentation_text_renderer.hpp"
#include "slide_renderer.hpp"

#include <mirrorfly/image_decode.hpp>

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    }

    bool test_list_spacing_surface()
    {
        using namespace mirrorfly;
        const auto render = [](double spacing)
        {
            auto scene =
                std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
            scene->width = 240;
            scene->height = 220;
            PresentationShape shape;
            shape.width = 240;
            shape.height = 220;
            shape.text.inset_left = shape.text.inset_right = 0;
            shape.text.inset_top = shape.text.inset_bottom = 0;
            PresentationRun run;
            run.text = "Visible text";
            run.font_family = "Arial";
            run.font_size = 18;
            run.color = "#0000FF";
            PresentationParagraph paragraph;
            paragraph.runs.push_back(run);
            paragraph.bullet = "•";
            paragraph.bullet_font = "Arial";
            paragraph.bullet_color = "#FF0000";
            paragraph.bullet_size_percent = 1.5;
            paragraph.margin_left = 30;
            paragraph.first_line_indent = -20;
            shape.text.paragraphs.push_back(paragraph);
            auto empty = paragraph;
            empty.runs[0].text.clear();
            shape.text.paragraphs.push_back(empty);
            paragraph.space_before_percent = spacing;
            shape.text.paragraphs.push_back(paragraph);
            scene->slides[0].shapes.push_back(shape);
            QImage image(240, 220, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            paint_presentation_slide(painter, prepare_presentation(scene), 0, {}, image.rect());
            return image;
        };
        const auto compact = render(0);
        const auto spaced = render(2);
        const auto last_blue_row = [](const QImage& image)
        {
            int last = -1;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                {
                    const auto color = image.pixelColor(x, y);
                    if (color.blue() > 200 && color.red() < 50 && color.green() < 50)
                        last = y;
                }
            return last;
        };
        bool passed = check(last_blue_row(spaced) - last_blue_row(compact) > 35,
            "percentage paragraph spacing visibly separates list items");
        int bullet_rows = 0;
        bool previous = false;
        for (int y = 0; y < spaced.height(); ++y)
        {
            bool red = false;
            for (int x = 0; x < 30; ++x)
            {
                const auto color = spaced.pixelColor(x, y);
                red = red || (color.red() > 200 && color.green() < 50 && color.blue() < 50);
            }
            if (red && !previous)
                ++bullet_rows;
            previous = red;
        }
        return check(bullet_rows == 2, "bullet color differs from text and empty paragraphs omit bullets") &&
            passed;
    }

    bool test_numbering_format_surface()
    {
        using namespace mirrorfly;
        const auto render = [](const std::string& format)
        {
            auto scene =
                std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
            scene->width = 180;
            scene->height = 80;
            PresentationShape shape;
            shape.width = scene->width;
            shape.height = scene->height;
            shape.text.inset_left = shape.text.inset_right = 0;
            shape.text.inset_top = shape.text.inset_bottom = 0;
            PresentationParagraph paragraph;
            paragraph.numbered = true;
            paragraph.number_start = 1;
            paragraph.number_format = format;
            paragraph.margin_left = 36;
            paragraph.first_line_indent = -24;
            PresentationRun run;
            run.text = "Numbered text";
            run.font_family = "Arial";
            run.font_size = 24;
            paragraph.runs.push_back(run);
            shape.text.paragraphs.push_back(paragraph);
            scene->slides[0].shapes.push_back(shape);
            QImage image(180, 80, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            paint_presentation_slide(painter, prepare_presentation(scene), 0, {}, image.rect());
            return image;
        };
        bool passed = check(render("arabicPeriod") != render("arabicParenR"),
            "right-parenthesis numbering changes rendered pixels");
        const auto render_levels = [](const std::vector<int>& levels)
        {
            auto scene =
                std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
            scene->width = 240;
            scene->height = 160;
            PresentationShape shape;
            shape.width = scene->width;
            shape.height = scene->height;
            shape.text.inset_left = shape.text.inset_right = 0;
            shape.text.inset_top = shape.text.inset_bottom = 0;
            for (const int level : levels)
            {
                PresentationParagraph paragraph;
                paragraph.numbered = true;
                paragraph.list_level = level;
                paragraph.margin_left = 36;
                paragraph.first_line_indent = -24;
                PresentationRun run;
                run.text = "Text";
                run.font_family = "Arial";
                run.font_size = 20;
                paragraph.runs.push_back(run);
                shape.text.paragraphs.push_back(paragraph);
            }
            scene->slides[0].shapes.push_back(shape);
            QImage image(240, 160, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            paint_presentation_slide(painter, prepare_presentation(scene), 0, {}, image.rect());
            return image;
        };
        return check(render_levels({0, 1, 0}) != render_levels({0, 0, 0}),
                   "nested numbering keeps a separate counter at each list level") &&
            passed;
    }

    bool test_picture_bullet_surface()
    {
        using namespace mirrorfly;
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        scene->width = 220;
        scene->height = 70;
        QImage bullet(8, 8, QImage::Format_ARGB32);
        bullet.fill(Qt::blue);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        bullet.save(&buffer, "PNG");
        scene->images.push_back({"bullet.png", "image/png", bytes.toStdString()});
        PresentationShape shape;
        shape.width = scene->width;
        shape.height = scene->height;
        shape.text.inset_left = shape.text.inset_right = 0;
        shape.text.inset_top = shape.text.inset_bottom = 0;
        PresentationParagraph paragraph;
        paragraph.margin_left = 40;
        paragraph.first_line_indent = -30;
        paragraph.bullet_image_path = "bullet.png";
        PresentationRun run;
        run.text = "Picture bullet";
        run.font_family = "Arial";
        run.font_size = 24;
        paragraph.runs.push_back(run);
        shape.text.paragraphs.push_back(paragraph);
        scene->slides[0].shapes.push_back(shape);
        QImage image(220, 70, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        paint_presentation_slide(painter, prepare_presentation(scene), 0, {}, image.rect());
        painter.end();
        int blue = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < 40; ++x)
            {
                const auto color = image.pixelColor(x, y);
                if (color.blue() > 200 && color.red() < 50 && color.green() < 50)
                {
                    ++blue;
                }
            }
        }
        return check(blue > 50, "embedded picture bullet renders before paragraph text");
    }

    bool test_math_approximation_surface()
    {
        using namespace mirrorfly;
        auto source = make_presentation(PresentationSlideLayout::Blank);
        source.width = 360;
        source.height = 120;
        auto package = serialize_presentation(source);
        const std::string formula = "<mc:AlternateContent xmlns:mc='urn:compat' xmlns:a14='urn:drawing14' "
                                    "xmlns:m='urn:math'><mc:Choice Requires='a14'>"
                                    "<p:sp><p:nvSpPr><p:cNvPr id='91' name='Math'/><p:nvPr/></p:nvSpPr>"
                                    "<p:spPr><a:xfrm><a:off x='0' y='0'/><a:ext cx='4572000' cy='1524000'/>"
                                    "</a:xfrm><a:prstGeom prst='rect'><a:avLst/></a:prstGeom><a:noFill/>"
                                    "<a:ln><a:noFill/></a:ln></p:spPr><p:txBody>"
                                    "<a:bodyPr lIns='0' tIns='0' rIns='0' bIns='0'/><a:p><a14:m><m:oMath>"
                                    "<m:f><m:num><m:r><m:t>x+1</m:t></m:r></m:num><m:den><m:r><m:t>2</m:t>"
                                    "</m:r></m:den></m:f></m:oMath></a14:m></a:p></p:txBody></p:sp>"
                                    "</mc:Choice></mc:AlternateContent>";
        for (auto& part : package.parts)
        {
            if (part.path == "ppt/slides/slide1.xml")
            {
                part.bytes.insert(part.bytes.find("</p:spTree>"), formula);
            }
        }
        auto parsed = parse_presentation(std::move(package.parts));
        if (!check(parsed.error == PresentationError::None && !parsed.scene.slides.empty() &&
                    parsed.scene.slides[0].shapes.size() == 1,
                "formula approximation reaches the presentation scene"))
        {
            return false;
        }
        const auto& runs = parsed.scene.slides[0].shapes[0].text.paragraphs[0].runs;
        bool passed = check(runs.size() == 1 && runs[0].text == "(x+1)/(2)",
            "formula approximation reaches the public text model");
        QImage image(360, 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        paint_presentation_slide(painter,
            prepare_presentation(std::make_shared<PresentationScene>(std::move(parsed.scene))), 0, {},
            image.rect());
        painter.end();
        int dark = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const auto color = image.pixelColor(x, y);
                dark += color.red() < 180 && color.green() < 180 && color.blue() < 180;
            }
        }
        return check(dark > 20, "formula approximation produces visible pixels") && passed;
    }

    bool test_group_coordinate_units()
    {
        using namespace mirrorfly;
        const auto render = [](int units, bool rotated)
        {
            auto scene = make_presentation(PresentationSlideLayout::Blank);
            scene.width = 240;
            scene.height = 140;
            scene.slides[0].background.color = "#000000";
            auto package = serialize_presentation(scene);
            const std::string rotation = rotated ? " rot='1020000' flipH='1'" : "";
            const auto width = std::to_string(160 * units);
            const auto height = std::to_string(60 * units);
            const std::string group = "<p:grpSp><p:nvGrpSpPr><p:cNvPr id='10' name='Group'/></p:nvGrpSpPr>"
                                      "<p:grpSpPr><a:xfrm><a:off x='381000' y='381000'/>"
                                      "<a:ext cx='2032000' cy='762000'/><a:chOff x='0' y='0'/>"
                                      "<a:chExt cx='" +
                width + "' cy='" + height +
                "'/></a:xfrm></p:grpSpPr>"
                "<p:sp><p:nvSpPr><p:cNvPr id='11' name='Physical styles'/></p:nvSpPr>"
                "<p:spPr><a:xfrm" +
                rotation + "><a:off x='0' y='0'/><a:ext cx='" + width + "' cy='" + height +
                "'/></a:xfrm><a:prstGeom prst='rect'/><a:noFill/>"
                "<a:ln w='25400'><a:solidFill><a:srgbClr val='FFFFFF'/></a:solidFill></a:ln>"
                "</p:spPr><p:txBody><a:bodyPr lIns='127000' tIns='127000' rIns='0' bIns='0'/>"
                "<a:p><a:r><a:rPr sz='1800'><a:solidFill><a:srgbClr val='FF0000'/>"
                "</a:solidFill><a:latin typeface='Arial'/></a:rPr><a:t>WAVE</a:t></a:r></a:p>"
                "</p:txBody></p:sp></p:grpSp>";
            for (auto& part : package.parts)
                if (part.path == "ppt/slides/slide1.xml")
                    part.bytes.insert(part.bytes.find("</p:spTree>"), group);
            auto parsed = parse_presentation(std::move(package.parts));
            if (parsed.error != PresentationError::None)
                return QImage{};
            SlideRenderer surface;
            surface.setSize(QSizeF(240, 140));
            surface.setDocument(QVariant::fromValue(
                prepare_presentation(std::make_shared<PresentationScene>(std::move(parsed.scene)))));
            QImage image(240, 140, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::black);
            QPainter painter(&image);
            surface.paint(&painter);
            return image;
        };
        bool passed = true;
        for (const bool rotated : {false, true})
        {
            const auto reference = render(12700, rotated);
            for (const int units : {8, 50800})
            {
                const auto actual = render(units, rotated);
                passed = check(!reference.isNull() && actual == reference,
                             "group coordinate units do not scale physical fonts, insets or outlines") &&
                    passed;
            }
            if (reference.isNull() || rotated)
                continue;
            int white = 0;
            int red = 0;
            for (int y = 0; y < reference.height(); ++y)
                for (int x = 0; x < reference.width(); ++x)
                {
                    const auto color = reference.pixelColor(x, y);
                    if (color.red() > 200 && color.green() > 200 && color.blue() > 200)
                        ++white;
                    if (color.red() > 150 && color.green() < 50 && color.blue() < 50)
                        ++red;
                }
            passed = check(white > 600 && white < 1000 && red > 100 && red < 450 &&
                             reference.pixelColor(10, 10) == QColor(Qt::black),
                         "two point group outline and eighteen point text remain visible without flooding "
                         "slide") &&
                passed;
        }
        return passed;
    }

    bool test_rotated_picture_ungroup_pixels()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.width = 400;
        scene.height = 300;
        QImage pattern(24, 24, QImage::Format_ARGB32);
        pattern.fill(Qt::red);
        QPainter pattern_painter(&pattern);
        pattern_painter.fillRect(12, 0, 12, 24, Qt::blue);
        pattern_painter.end();
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!pattern.save(&buffer, "PNG"))
            return check(false, "rotated picture fixture encodes PNG");
        PresentationEditCommand add;
        add.action = PresentationEditAction::AddImage;
        add.image_mime_type = "image/png";
        add.image_bytes = bytes.toStdString();
        add.image_path = "rotated-group.png";
        add.x = 60;
        add.y = 70;
        add.width = 48;
        add.height = 36;
        bool passed = check(apply_presentation_edit(scene, add).error == PresentationEditError::None,
            "rotated group fixture inserts first image");
        add.x = 150;
        add.y = 80;
        passed = check(apply_presentation_edit(scene, add).error == PresentationEditError::None,
                     "rotated group fixture inserts second image") &&
            passed;
        if (!passed)
            return false;
        PresentationEditCommand group;
        group.action = PresentationEditAction::GroupAdjacent;
        group.shape_index = 0;
        group.target_index = 1;
        if (!check(apply_presentation_edit(scene, group).error == PresentationEditError::None,
                "rotated group fixture creates a native group"))
            return false;
        auto package = serialize_presentation(scene);
        for (auto& part : package.parts)
        {
            if (part.path != "ppt/slides/slide1.xml")
                continue;
            const auto group_marker = part.bytes.find("<p:grpSp>");
            const auto properties = part.bytes.find("<p:grpSpPr>", group_marker);
            const auto transform = part.bytes.find("<a:xfrm", properties);
            const auto extent = part.bytes.find("<a:ext", transform);
            if (group_marker == std::string::npos || properties == std::string::npos ||
                transform == std::string::npos || extent == std::string::npos)
                return check(false, "rotated picture group transform exists in fixture");
            const auto multiply_extent = [&](const char* attribute, long long factor)
            {
                const auto key = std::string(attribute) + "=\"";
                const auto position = part.bytes.find(key, extent);
                if (position == std::string::npos)
                    return false;
                const auto start = position + key.size();
                const auto end = part.bytes.find('"', start);
                if (end == std::string::npos)
                    return false;
                const auto value = std::stoll(part.bytes.substr(start, end - start));
                part.bytes.replace(start, end - start, std::to_string(value * factor));
                return true;
            };
            if (!multiply_extent("cx", 2) || !multiply_extent("cy", 3))
                return check(false, "rotated picture group extents can be scaled");
            part.bytes.insert(part.bytes.find('>', transform), " rot=\"5400000\"");
        }
        auto imported = parse_presentation(std::move(package.parts));
        if (!check(
                imported.error == PresentationError::None && imported.scene.slides[0].groups[0].ungroupable,
                "rotated picture group is editable after import"))
            return false;
        imported.scene.native_editable = true;
        const auto render = [](const PresentationScene& value)
        {
            auto document = prepare_presentation(std::make_shared<PresentationScene>(value));
            QImage image(400, 300, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            paint_presentation_slide(painter, document, 0, {}, image.rect());
            return image;
        };
        const auto before = render(imported.scene);
        PresentationEditCommand ungroup;
        ungroup.action = PresentationEditAction::Ungroup;
        ungroup.group_id = imported.scene.slides[0].groups[0].source_id;
        if (!check(apply_presentation_edit(imported.scene, ungroup).error == PresentationEditError::None,
                "rotated picture group can be flattened"))
            return false;
        const auto after = render(imported.scene);
        int visible = 0;
        int changed = 0;
        for (int y = 0; y < before.height(); ++y)
            for (int x = 0; x < before.width(); ++x)
            {
                const auto first = before.pixelColor(x, y);
                const auto second = after.pixelColor(x, y);
                visible += first.red() < 240 || first.green() < 240 || first.blue() < 240;
                changed += std::abs(first.red() - second.red()) + std::abs(first.green() - second.green()) +
                        std::abs(first.blue() - second.blue()) >
                    30;
            }
        if (changed >= 100)
            std::cerr << "Rotated ungroup pixel drift: " << changed << "/" << visible << '\n';
        return check(visible > 500 && changed < 100,
            "rotated picture pixels remain stable after ungrouping without a window");
    }

    bool test_picture_fill_surface()
    {
        using namespace mirrorfly;
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        scene->width = 120;
        scene->height = 60;
        QImage texture(20, 10, QImage::Format_ARGB32);
        texture.fill(Qt::red);
        QPainter pattern(&texture);
        pattern.fillRect(10, 0, 10, 10, Qt::blue);
        pattern.end();
        const auto asset = [&](const std::string& path, const QImage& image)
        {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            scene->images.push_back({path, "image/png", bytes.toStdString()});
        };
        asset("texture.png", texture);
        texture.fill(Qt::green);
        asset("green.png", texture);
        auto& slide = scene->slides[0];
        slide.background.image_path = "texture.png";
        PresentationShape picture;
        picture.width = picture.height = 40;
        picture.transform = {1, 0, 0, 1, 20, 15};
        picture.image_path = "green.png";
        picture.geometry = "ellipse";
        picture.path_geometry =
            std::make_shared<PresentationGeometry>(presentation_geometry("ellipse", 40, 40));
        slide.shapes.push_back(picture);
        PresentationShape filled;
        filled.width = 40;
        filled.height = 20;
        filled.transform = {1, 0, 0, 1, 75, 10};
        filled.fill.image_path = "texture.png";
        filled.fill.image_tile = true;
        filled.fill.image_dpi = 72;
        filled.fill.image_flip = "x";
        slide.shapes.push_back(filled);
        SlideRenderer surface;
        surface.setSize(QSizeF(120, 60));
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        QImage image(120, 60, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        surface.paint(&painter);
        painter.end();
        bool passed =
            check(image.pixelColor(5, 5) == QColor(Qt::red) && image.pixelColor(115, 5) == QColor(Qt::blue),
                "background stretches embedded image fill");
        passed = check(image.pixelColor(21, 16) == QColor(Qt::red) &&
                         image.pixelColor(40, 35) == QColor(Qt::green),
                     "picture pixels clip to actual preset geometry") &&
            passed;
        passed = check(image.pixelColor(80, 15) == QColor(Qt::red) &&
                         image.pixelColor(90, 15) == QColor(Qt::blue) &&
                         image.pixelColor(100, 15) == QColor(Qt::blue) &&
                         image.pixelColor(110, 15) == QColor(Qt::red),
                     "tiled image honors physical scale and alternating flip") &&
            passed;
        auto opacity_scene =
            std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        opacity_scene->width = opacity_scene->height = 20;
        QImage red(4, 4, QImage::Format_ARGB32);
        red.fill(Qt::red);
        QByteArray red_bytes;
        QBuffer red_buffer(&red_bytes);
        red_buffer.open(QIODevice::WriteOnly);
        red.save(&red_buffer, "PNG");
        opacity_scene->images.push_back({"red.png", "image/png", red_bytes.toStdString()});
        PresentationShape translucent;
        translucent.width = translucent.height = 20;
        translucent.image_path = "red.png";
        translucent.image_opacity = 0.5;
        opacity_scene->slides[0].shapes.push_back(translucent);
        QImage blended(20, 20, QImage::Format_ARGB32_Premultiplied);
        blended.fill(Qt::white);
        QPainter opacity_painter(&blended);
        paint_presentation_slide(opacity_painter, prepare_presentation(opacity_scene), 0, {}, blended.rect());
        opacity_painter.end();
        const auto mixed = blended.pixelColor(10, 10);
        passed = check(mixed.red() == 255 && std::abs(mixed.green() - 127) <= 1 &&
                         std::abs(mixed.blue() - 127) <= 1,
                     "picture alpha modulation changes rendered opacity") &&
            passed;
        const auto paint_fill = [&](const PresentationFill& fill)
        {
            QImage output(64, 32, QImage::Format_ARGB32_Premultiplied);
            output.fill(Qt::white);
            QPainter brush_painter(&output);
            brush_painter.fillRect(
                output.rect(), presentation_image_fill(prepare_presentation(scene), fill, output.rect()));
            return output;
        };
        PresentationFill inset;
        inset.image_path = "texture.png";
        inset.image_fill_rect = {0.25, 0.25, 0.25, 0.25};
        const auto padded = paint_fill(inset);
        passed = check(padded.pixelColor(2, 16) == QColor(Qt::white) &&
                         padded.pixelColor(22, 16) == QColor(Qt::red) &&
                         padded.pixelColor(42, 16) == QColor(Qt::blue) &&
                         padded.pixelColor(60, 16) == QColor(Qt::white),
                     "stretch fill rectangle leaves transparent margins without repeating texture") &&
            passed;
        QImage large(4096, 8, QImage::Format_ARGB32);
        large.fill(Qt::red);
        QPainter large_painter(&large);
        large_painter.fillRect(2048, 0, 2048, 8, Qt::blue);
        large_painter.end();
        asset("large.png", large);
        PresentationFill large_fill;
        large_fill.image_path = "large.png";
        large_fill.image_tile = true;
        large_fill.image_dpi = 72;
        large_fill.image_scale = {1.0 / 128, 4};
        const auto tiled = paint_fill(large_fill);
        passed = check(tiled.pixelColor(5, 10) == QColor(Qt::red) &&
                         tiled.pixelColor(21, 10) == QColor(Qt::blue) &&
                         tiled.pixelColor(37, 10) == QColor(Qt::red) &&
                         tiled.pixelColor(53, 10) == QColor(Qt::blue),
                     "decoder downsampling preserves the original physical tile dimensions") &&
            passed;
        return passed;
    }

    bool test_lazy_image_preparation()
    {
        using namespace mirrorfly;
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        QImage source(4096, 8, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::red);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "PNG");
        scene->images.push_back({"wide.png", "image/png", bytes.toStdString()});
        QByteArray large_jpeg;
        {
            QImage large_source(5000, 3500, QImage::Format_RGB888);
            large_source.fill(QColor(32, 96, 176));
            QBuffer jpeg_buffer(&large_jpeg);
            jpeg_buffer.open(QIODevice::WriteOnly);
            large_source.save(&jpeg_buffer, "JPEG", 70);
        }
        scene->images.push_back({"large.jpeg", "image/jpeg", large_jpeg.toStdString()});
        for (int index = 0; index < 5; ++index)
        {
            scene->images.push_back(
                {"cache-" + std::to_string(index) + ".jpeg", "image/jpeg", large_jpeg.toStdString()});
        }
        PresentationShape picture;
        picture.image_path = "wide.png";
        picture.width = scene->width;
        picture.height = scene->height;
        scene->slides[0].shapes.push_back(std::move(picture));

        std::size_t completed = 0;
        std::size_t total = 0;
        PresentationPrepareOptions options;
        options.environment = presentation_render_environment();
        options.eager_image_analysis = false;
        options.progress = [&completed, &total](std::size_t current, std::size_t maximum)
        {
            completed = current;
            total = maximum;
        };
        const auto document = prepare_presentation(scene, {}, options);
        bool passed = check(document->image_index.contains(QStringLiteral("wide.png")) &&
                document->image_sizes.isEmpty() && completed == 1 && total == 1,
            "lazy preparation indexes images without decoding or inspecting every asset");
        scene->slides[0].shapes[0].image_path = "large.jpeg";
        const auto deferred_document = prepare_presentation(scene, {}, options);
        SlideRenderer deferred;
        deferred.setDeferredFrames(true);
        deferred.setSize(QSizeF(320, 180));
        deferred.setDocument(QVariant::fromValue(deferred_document));
        QImage first_paint(320, 180, QImage::Format_ARGB32_Premultiplied);
        first_paint.fill(Qt::transparent);
        QElapsedTimer first_paint_time;
        first_paint_time.start();
        {
            QPainter painter(&first_paint);
            deferred.paint(&painter);
        }
        passed = check(first_paint_time.elapsed() < 250,
                     "image-rich editor page returns a lightweight first paint") &&
            passed;
        QElapsedTimer frame_wait;
        frame_wait.start();
        while (cached_presentation_frame(deferred_document, 0, {}, QSize(320, 180)).isNull() &&
            frame_wait.elapsed() < 10000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
        const auto deferred_frame = cached_presentation_frame(deferred_document, 0, {}, QSize(320, 180));
        passed = check(!deferred_frame.isNull() && deferred_frame.pixelColor(160, 90).blue() > 120,
                     "deferred editor frame completes on the foreground worker") &&
            passed;
        auto deletion_scene =
            std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        deletion_scene->width = 320;
        deletion_scene->height = 180;
        deletion_scene->images.push_back({"delete.jpeg", "image/jpeg", large_jpeg.toStdString()});
        PresentationShape deletion_picture;
        deletion_picture.image_path = "delete.jpeg";
        deletion_picture.width = 160;
        deletion_picture.height = 90;
        deletion_picture.transform[4] = 80;
        deletion_picture.transform[5] = 45;
        deletion_scene->slides[0].shapes.push_back(deletion_picture);
        deletion_scene->slides.push_back(deletion_scene->slides.front());
        const auto deletion_before = prepare_presentation(deletion_scene, {}, options);
        QImage old_frame(320, 180, QImage::Format_ARGB32_Premultiplied);
        old_frame.fill(Qt::blue);
        cache_presentation_frame(deletion_before, 0, {}, old_frame.size(), old_frame);
        auto deleted_scene = std::make_shared<PresentationScene>(*deletion_scene);
        deleted_scene->slides[0].shapes.clear();
        PresentationPrepareOptions deleted_options;
        deleted_options.eager_image_analysis = false;
        deleted_options.edited_slide = 0;
        deleted_options.edited_shape = 0;
        deleted_options.edit_layer_change = PresentationEditLayerChange::Remove;
        const auto deletion_after = prepare_presentation(deleted_scene, deletion_before, deleted_options);
        passed =
            check(deletion_after->frame_cache == deletion_before->frame_cache &&
                    cached_presentation_frame(deletion_after, 0, {}, old_frame.size()).isNull(),
                "an edited slide cannot reuse the previous visual revision from the shared frame cache") &&
            passed;
        SlideRenderer deletion_renderer;
        deletion_renderer.setDeferredFrames(true);
        deletion_renderer.setSize(QSizeF(320, 180));
        deletion_renderer.setDocument(QVariant::fromValue(deletion_before));
        deletion_renderer.setDocument(QVariant::fromValue(deletion_after));
        QElapsedTimer deletion_wait;
        deletion_wait.start();
        double maximum_foreground_ms = 0;
        QImage deletion_pixels(320, 180, QImage::Format_ARGB32_Premultiplied);
        do
        {
            deletion_pixels.fill(Qt::transparent);
            QElapsedTimer paint_time;
            paint_time.start();
            QPainter painter(&deletion_pixels);
            deletion_renderer.paint(&painter);
            painter.end();
            maximum_foreground_ms = std::max(maximum_foreground_ms, paint_time.nsecsElapsed() / 1e6);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        } while (cached_presentation_frame(deletion_after, 0, {}, old_frame.size()).isNull() &&
            deletion_wait.elapsed() < 10000);
        passed = check(deletion_pixels.pixelColor(160, 90) != QColor(Qt::blue),
                     "cold deletion removes the object from the immediate fallback frame") &&
            passed;
        passed = check(maximum_foreground_ms < 1000 &&
                         !cached_presentation_frame(deletion_after, 0, {}, old_frame.size()).isNull(),
                     "every foreground paint stays below one second while the edited frame loads") &&
            passed;
        deletion_renderer.setSlideIndex(1);
        QImage switched_pixels(320, 180, QImage::Format_ARGB32_Premultiplied);
        switched_pixels.fill(Qt::transparent);
        QElapsedTimer switch_paint_time;
        switch_paint_time.start();
        {
            QPainter painter(&switched_pixels);
            deletion_renderer.paint(&painter);
        }
        passed = check(switch_paint_time.elapsed() < 250,
                     "switching away from an edited image slide keeps the lightweight first paint") &&
            passed;
        QElapsedTimer switch_wait;
        switch_wait.start();
        while (cached_presentation_frame(deletion_after, 1, {}, old_frame.size()).isNull() &&
            switch_wait.elapsed() < 10000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
        passed = check(!cached_presentation_frame(deletion_after, 1, {}, old_frame.size()).isNull(),
                     "the page selected after an edit completes on the frame workers") &&
            passed;
        SlideThumbnailRenderer thumbnail_switch;
        thumbnail_switch.setSize(QSizeF(128, 72));
        thumbnail_switch.setDocument(QVariant::fromValue(deletion_after));
        thumbnail_switch.setSlideIndex(1);
        QElapsedTimer thumbnail_wait;
        thumbnail_wait.start();
        while (
            cached_presentation_thumbnail(deletion_after, presentation_thumbnail_key(deletion_after, 1, {}))
                .isNull() &&
            thumbnail_wait.elapsed() < 10000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
        passed = check(!cached_presentation_thumbnail(
                           deletion_after, presentation_thumbnail_key(deletion_after, 1, {}))
                             .isNull() &&
                         cached_presentation_thumbnail(
                             deletion_after, presentation_thumbnail_key(deletion_after, 0, {}))
                             .isNull(),
                     "thumbnail bindings coalesce before rendering and skip the temporary default page") &&
            passed;
        auto preview_document = prepare_presentation(deleted_scene, {}, options);
        QImage thumbnail_hint(32, 18, QImage::Format_ARGB32_Premultiplied);
        thumbnail_hint.fill(QColor(128, 32, 192));
        cache_presentation_thumbnail(
            preview_document, presentation_thumbnail_key(preview_document, 1, {}), thumbnail_hint);
        SlideRenderer thumbnail_preview;
        thumbnail_preview.setDeferredFrames(true);
        thumbnail_preview.setSize(QSizeF(320, 180));
        thumbnail_preview.setDocument(QVariant::fromValue(preview_document));
        thumbnail_preview.setSlideIndex(1);
        QImage preview_pixels(320, 180, QImage::Format_ARGB32_Premultiplied);
        preview_pixels.fill(Qt::transparent);
        QElapsedTimer preview_time;
        preview_time.start();
        {
            QPainter painter(&preview_pixels);
            thumbnail_preview.paint(&painter);
        }
        passed =
            check(preview_time.elapsed() < 250 && preview_pixels.pixelColor(160, 90) == QColor(128, 32, 192),
                "the main canvas reuses the selected thumbnail as an immediate complete-page preview") &&
            passed;
        scene->slides[0].shapes[0].image_path = "wide.png";
        const auto full = presentation_image(document, "wide.png");
        passed = check(!full.isNull() && full.width() == document->image_decode_side &&
                         presentation_image_source_size(document, "wide.png") == QSize(4096, 8),
                     "first full-slide use decodes within the main texture budget and records source size") &&
            passed;
        const auto thumbnail_document = presentation_thumbnail_document(document);
        const auto thumbnail = presentation_image(thumbnail_document, "wide.png");
        passed =
            check(!thumbnail.isNull() && thumbnail.width() == 256 &&
                    thumbnail_document->image_decode_side == 256 &&
                    thumbnail == full.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation) &&
                    presentation_image_source_size(thumbnail_document, "wide.png") == QSize(4096, 8) &&
                    presentation_image(thumbnail_document, "wide.png").constBits() == thumbnail.constBits(),
                "thumbnail rendering scales an existing full image once into its own bounded cache") &&
            passed;
        const auto cold_thumbnail = presentation_image(thumbnail_document, "large.jpeg");
        passed =
            check(!cold_thumbnail.isNull() && cold_thumbnail.width() <= 256 &&
                    cold_thumbnail.height() <= 256 &&
                    presentation_image_source_size(thumbnail_document, "large.jpeg") == QSize(5000, 3500),
                "a thumbnail without a full image still decodes at its small budget") &&
            passed;
        const auto large = presentation_image(document, "large.jpeg");
        passed = check(!large.isNull() && large.width() <= document->image_decode_side &&
                         large.height() <= document->image_decode_side &&
                         presentation_image_source_size(document, "large.jpeg") == QSize(5000, 3500) &&
                         large.pixelColor(large.width() / 2, large.height() / 2).blue() > 150,
                     "large scalable JPEG decodes directly into the bounded texture size") &&
            passed;
        const auto first_cached = presentation_image(document, "cache-0.jpeg");
        const auto* first_pixels = first_cached.constBits();
        for (int index = 1; index < 5; ++index)
        {
            passed = check(!presentation_image(document, "cache-" + std::to_string(index) + ".jpeg").isNull(),
                         "image-rich slide resources decode into the bounded shared cache") &&
                passed;
        }
        passed = check(presentation_image(document, "cache-0.jpeg").constBits() == first_pixels,
                     "the main image cache retains a five-picture slide without decode thrashing") &&
            passed;
        QImage cached(16, 9, QImage::Format_ARGB32_Premultiplied);
        cached.fill(Qt::blue);
        cache_presentation_thumbnail(document, QStringLiteral("slide:0"), cached);
        return check(cached_presentation_thumbnail(document, QStringLiteral("slide:0")) == cached,
                   "rasterized slide thumbnails are retained in the bounded thumbnail cache") &&
            passed;
    }

    bool test_vertical_text_surface()
    {
        using namespace mirrorfly;
        const auto render = [](const std::string& direction)
        {
            auto scene =
                std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
            const bool vertical = !direction.empty();
            scene->width = vertical ? 36 : 240;
            scene->height = vertical ? 240 : 36;
            PresentationShape shape;
            shape.width = scene->width;
            shape.height = scene->height;
            shape.text.inset_left = shape.text.inset_right = 0;
            shape.text.inset_top = shape.text.inset_bottom = 0;
            shape.text.vertical = direction;
            shape.text.vertical_alignment = "center";
            PresentationRun run;
            run.font_size = 16;
            run.text = "BUSINESS ANALYSIS REPORT";
            PresentationParagraph paragraph;
            paragraph.runs = {run};
            shape.text.paragraphs = {paragraph};
            scene->slides[0].shapes = {shape};
            QImage image(qRound(scene->width), qRound(scene->height), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            paint_presentation_slide(painter, prepare_presentation(scene), 0, {}, image.rect());
            return image;
        };
        const auto horizontal = render("");
        bool passed = true;
        for (const auto& direction : {"vert", "vert270", "eaVert"})
        {
            const auto vertical = render(direction);
            const auto expected =
                horizontal.transformed(QTransform().rotate(std::string(direction) == "vert270" ? 270 : 90));
            int expected_ink = 0, actual_ink = 0, matching = 0;
            for (int y = 0; y < vertical.height(); ++y)
                for (int x = 0; x < vertical.width(); ++x)
                {
                    const bool actual = vertical.pixelColor(x, y).red() < 200;
                    const bool reference = expected.pixelColor(x, y).red() < 200;
                    actual_ink += actual;
                    expected_ink += reference;
                    bool nearby = false;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (expected.rect().contains(x + dx, y + dy))
                                nearby = nearby || expected.pixelColor(x + dx, y + dy).red() < 200;
                    matching += actual && nearby;
                }
            passed =
                check(expected_ink > 400 && actual_ink > expected_ink * 0.9 && matching > actual_ink * 0.95,
                    "vertical Latin text lays out along the tall dimension before rotation") &&
                passed;
        }
        return passed;
    }

    bool test_text_autofit()
    {
        QTextDocument source;
        source.setDocumentMargin(0);
        source.setTextWidth(180);
        QTextCursor cursor(&source);
        QFont font;
        font.setPixelSize(26);
        QTextCharFormat format;
        format.setFont(font);
        for (int index = 0; index < 4; ++index)
        {
            if (index)
                cursor.insertBlock();
            cursor.setBlockCharFormat(format);
            cursor.insertText(QStringLiteral("A complete classroom discussion question."), format);
        }
        cursor.insertBlock();
        format.setForeground(Qt::red);
        cursor.insertText(QStringLiteral("LAST LINE"), format);
        std::unique_ptr<QTextDocument> fitted(source.clone()), cached(source.clone());
        auto& document = *fitted;
        const auto original_height = document.size().height();
        mirrorfly::fit_presentation_text(document, QSizeF(180, 140));
        mirrorfly::fit_presentation_text(*cached, QSizeF(180, 140));
        const auto fitted_font = document.begin().begin().fragment().charFormat().font();
        bool passed = check(original_height > 140 && fitted_font.pixelSize() == 26 &&
                fitted_font.stretch() >= 92 && fitted_font.stretch() <= 100,
            "normal autofit preserves point size and uses at most eight percent horizontal compensation");
        passed = check(document.lastBlock().begin().fragment().charFormat().foreground().color() ==
                             QColor(Qt::red) &&
                         document.toHtml() == cached->toHtml(),
                     "autofit preserves run formatting and cache returns identical layout") &&
            passed;
        return check(document.toPlainText().endsWith(QStringLiteral("LAST LINE")),
                   "bounded metric compensation never removes overflowing text") &&
            passed;
    }

    bool test_line_surface()
    {
        using namespace mirrorfly;
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        scene->width = 140;
        scene->height = 130;
        int row = 0;
        for (const auto* type : {"triangle", "stealth", "arrow", "diamond", "oval"})
        {
            PresentationShape shape;
            shape.width = 100;
            shape.height = 0;
            shape.geometry = "line";
            shape.transform = {1, 0, 0, 1, 20, 15.0 + row++ * 25};
            shape.outline_color = "#000000";
            shape.outline_width = 3;
            shape.line_style.head.type = type;
            shape.line_style.tail.type = type;
            shape.line_style.dashes = {4, 3};
            scene->slides[0].shapes.push_back(shape);
        }
        QImage image(140, 130, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        paint_presentation_slide(painter, prepare_presentation(scene), 0, {}, image.rect());
        painter.end();
        bool passed = true;
        for (int index = 0; index < 5; ++index)
        {
            const int cy = 15 + index * 25;
            for (int cx : {20, 120})
            {
                int outside_line = 0;
                for (int y = cy - 7; y <= cy + 7; ++y)
                    for (int x = cx - 12; x <= cx + 12; ++x)
                        if (std::abs(y - cy) >= 2 && image.pixelColor(x, y).red() < 100)
                            ++outside_line;
                passed = check(outside_line > 4, "connector ends have visible off-axis decoration") && passed;
            }
            passed = check(image.pixelColor(45, cy).red() < 100 && image.pixelColor(36, cy).red() > 240,
                         "custom line dash produces visible strokes and gaps") &&
                passed;
        }
        return passed;
    }

    bool test_chart_surface()
    {
        using namespace mirrorfly;
        bool passed = true;
        for (const auto* kind : {"barChart", "lineChart", "areaChart", "pieChart", "doughnutChart",
                 "scatterChart", "bubbleChart", "radarChart", "surfaceChart", "surface3DChart", "stockChart",
                 "ofPieChart", "chartEx:waterfall", "chartEx:funnel", "chartEx:treemap", "chartEx:sunburst",
                 "chartEx:boxWhisker", "chartEx:histogram", "chartEx:paretoLine", "chartEx:regionMap",
                 "volumeStockHlc", "volumeStockOhlc", "diagram", "diagramData"})
        {
            const std::string family = kind;
            std::vector<PresentationPart> parts;
            if (family == "diagram")
            {
                parts = test_fixture::diagram_package();
            }
            else if (family == "diagramData")
            {
                parts = test_fixture::diagram_data_package();
            }
            else if (family == "stockChart")
            {
                parts = test_fixture::stock_chart_package();
            }
            else if (family == "volumeStockHlc" || family == "volumeStockOhlc")
            {
                parts = test_fixture::volume_stock_chart_package(family == "volumeStockOhlc");
            }
            else if (family.rfind("chartEx:", 0) == 0)
            {
                parts = test_fixture::chart_ex_package(family.substr(8));
            }
            else
            {
                parts = test_fixture::chart_package(kind);
            }
            const auto parsed = parse_presentation(parts);
            if (parsed.error != PresentationError::None)
                return check(false, "chart surface input");
            auto scene = std::make_shared<PresentationScene>(parsed.scene);
            SlideRenderer surface;
            surface.setSize(QSizeF(420, 260));
            surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
            QImage image(420, 260, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            surface.paint(&painter);
            painter.end();
            int colored = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                {
                    const auto color = image.pixelColor(x, y);
                    if (std::abs(color.red() - color.blue()) > 40)
                        ++colored;
                }
            passed = check(colored > 150, "chart family paints cached data with theme or explicit colors") &&
                passed;
            if (std::string(kind) == "barChart")
                passed = check(image.pixelColor(210, 60) == QColor("#2244AA") &&
                                 image.pixelColor(110, 195) == QColor("#2244AA") &&
                                 image.pixelColor(230, 150) == QColor("#CC2200"),
                             "positive negative and second-series bars paint at numeric positions") &&
                    passed;
            if (std::string(kind) == "doughnutChart")
                passed =
                    check(image.pixelColor(220, 124) == QColor(Qt::white), "doughnut center stays empty") &&
                    passed;
            if (std::string(kind) == "diagram")
                passed = check(image.pixelColor(40, 50) == QColor("#2244AA") &&
                                 image.pixelColor(140, 60) == QColor(Qt::white),
                             "SmartArt cached drawing scales and translates into slide frame") &&
                    passed;
            if (std::string(kind) == "diagramData")
                passed = check(colored > 600, "SmartArt data fallback paints visible nodes and connectors") &&
                    passed;
        }
        return passed;
    }

    bool test_table_surface()
    {
        using namespace mirrorfly;
        const auto parsed = parse_presentation(test_fixture::table_package());
        if (parsed.error != PresentationError::None)
            return check(false, "table surface input");
        auto scene = std::make_shared<PresentationScene>(parsed.scene);
        SlideRenderer surface;
        surface.setSize(QSizeF(240, 160));
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        QImage image(240, 160, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        surface.paint(&painter);
        painter.end();
        bool passed = check(image.pixelColor(190, 28) == QColor("#2244AA") &&
                image.pixelColor(80, 80) == QColor("#FF0000") &&
                image.pixelColor(190, 80) == QColor("#EFEFEF"),
            "table paints header body and explicit merged cell backgrounds");
        passed =
            check(image.pixelColor(50, 100) == QColor("#FF0000") && image.pixelColor(160, 100).red() < 80,
                "row merge removes internal border while unmerged cells retain separator") &&
            passed;
        return passed;
    }

    bool test_wordart_surface()
    {
        using namespace mirrorfly;
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        scene->width = 360;
        scene->height = 220;
        PresentationShape shape;
        shape.width = 300;
        shape.height = 90;
        shape.transform = {1, 0, 0, 1, 30, 30};
        shape.text.inset_left = shape.text.inset_right = shape.text.inset_top = shape.text.inset_bottom = 0;
        PresentationParagraph paragraph;
        PresentationRun run;
        run.text = "Mirrorfly";
        run.font_size = 44;
        run.bold = true;
        paragraph.runs.push_back(run);
        shape.text.paragraphs.push_back(paragraph);
        scene->slides[0].shapes.push_back(shape);
        const auto paint = [](const std::shared_ptr<PresentationScene>& value)
        {
            SlideRenderer surface;
            surface.setSize(QSizeF(360, 220));
            surface.setDocument(QVariant::fromValue(prepare_presentation(value)));
            QImage image(360, 220, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            surface.paint(&painter);
            return image;
        };
        const auto plain = paint(scene);
        auto decorated = std::make_shared<PresentationScene>(*scene);
        auto& styled = decorated->slides[0].shapes[0].text.paragraphs[0].runs[0];
        styled.fill.stops = {{0, "#FF0000", 1}, {1, "#0000FF", 1}};
        styled.effects.outline_color = "#0000FF";
        styled.effects.outline_width = 1;
        styled.effects.shadow_color = "#000000";
        styled.effects.shadow_opacity = 0.8;
        styled.effects.shadow_x = styled.effects.shadow_y = 7;
        styled.effects.shadow_blur = 2;
        styled.effects.glow_color = "#00FF00";
        styled.effects.glow_opacity = 0.8;
        styled.effects.glow_radius = 4;
        styled.effects.reflection_opacity = 0.6;
        styled.effects.reflection_offset = 4;
        const auto rendered = paint(decorated);
        int colored = 0;
        int reflection = 0;
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x)
            {
                const auto pixel = rendered.pixelColor(x, y);
                if (std::max({pixel.red(), pixel.green(), pixel.blue()}) -
                        std::min({pixel.red(), pixel.green(), pixel.blue()}) >
                    30)
                    ++colored;
                if (y > 100 && pixel != QColor(Qt::white))
                    ++reflection;
            }
        bool passed = check(plain != rendered && colored > 200 && reflection > 20,
            "WordArt paints colored fill outline glow and a visible reflected layer");
        passed = check(paint(decorated) == rendered, "cached WordArt layer preserves pixels") && passed;
        auto recolored = std::make_shared<PresentationScene>(*decorated);
        recolored->slides[0].shapes[0].text.paragraphs[0].runs[0].fill.stops = {
            {0, "#FFFF00", 1}, {1, "#FF00FF", 1}};
        passed = check(paint(recolored) != rendered, "WordArt cache distinguishes changed gradient styles") &&
            passed;
        for (const std::string warp :
            {"textWave1", "textArchUp", "textCircle", "textInflate", "textSlantDown", "textFadeRight"})
        {
            auto warped = std::make_shared<PresentationScene>(*scene);
            warped->slides[0].shapes[0].text.warp = warp;
            const auto result = paint(warped);
            int painted = 0;
            for (int y = 0; y < result.height(); ++y)
                for (int x = 0; x < result.width(); ++x)
                    if (result.pixelColor(x, y).red() < 200)
                        ++painted;
            passed = check(result != plain && painted > 50,
                         ("WordArt warp paints transformed glyphs: " + warp).c_str()) &&
                passed;
        }
        return passed;
    }

    bool test_animation_surface()
    {
        using namespace mirrorfly;
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        scene->width = 200;
        scene->height = 100;
        PresentationShape shape;
        shape.width = 100;
        shape.height = 60;
        shape.transform = {1, 0, 0, 1, 50, 20};
        shape.fill.color = "#FF0000";
        shape.source_id = "2";
        scene->slides[0].shapes.push_back(shape);
        scene->slides.emplace_back();
        PresentationAnimation fade;
        fade.target = "2";
        fade.category = "entr";
        fade.filter = "fade";
        fade.click = 1;
        fade.duration = 1;
        scene->slides[0].animations.push_back(fade);
        SlideRenderer surface;
        surface.setSize(QSizeF(200, 100));
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        const auto paint = [&surface]
        {
            QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            surface.paint(&painter);
            return image;
        };
        bool passed =
            check(paint().pixelColor(100, 50).green() < 20, "static preview shows unanimated content");
        surface.setAnimationEnabled(true);
        passed =
            check(paint().pixelColor(100, 50) == QColor(Qt::white), "playback hides entrance until click") &&
            passed;
        passed = check(surface.advanceAnimation(), "first click triggers animation") && passed;
        surface.seekAnimation(0.5);
        const auto middle = paint().pixelColor(100, 50);
        passed = check(middle.red() > 240 && middle.green() > 100 && middle.green() < 160,
                     "actual slide painter interpolates fade opacity") &&
            passed;
        surface.seekAnimation(2);
        passed = check(paint().pixelColor(100, 50).green() < 20 && !surface.advanceAnimation(),
                     "completed entrance visible and no extra click groups") &&
            passed;
        surface.restartAnimation();
        passed = check(paint().pixelColor(100, 50) == QColor(Qt::white), "restart resets click visibility") &&
            passed;
        surface.setSlideIndex(1);
        passed = check(surface.animationState().value("effects").toInt() == 0 && !surface.advanceAnimation(),
                     "changing slide clears previous timeline") &&
            passed;
        surface.setSlideIndex(0);
        surface.setAnimationEnabled(false);
        passed = check(paint().pixelColor(100, 50).green() < 20,
                     "leaving playback restores full static preview") &&
            passed;
        for (const std::string filter : {"wipe(right)", "wipe(up)", "blinds(horizontal)",
                 "checkerboard(across)", "wheel(4)", "barn(outVertical)", "box(out)", "circle(in)",
                 "diamond(out)", "plus(out)", "randomBars(horizontal)", "dissolve", "strips(downRight)"})
        {
            int counts[3]{};
            for (int stage = 0; stage < 3; ++stage)
            {
                QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::white);
                QPainter painter(&image);
                PresentationAnimationState state;
                state.clip = filter;
                state.reveal = stage * 0.5;
                apply_presentation_animation(painter, shape, state);
                painter.fillRect(QRectF(50, 20, 100, 60), Qt::red);
                painter.end();
                for (int y = 20; y < 80; ++y)
                    for (int x = 50; x < 150; ++x)
                        if (image.pixelColor(x, y).green() < 128)
                            ++counts[stage];
            }
            passed = check(counts[0] == 0 && counts[1] > 0 && counts[1] < 6000 && counts[2] == 6000,
                         ("reveal family progressively clips actual pixels: " + filter).c_str()) &&
                passed;
        }
        auto& grouped_slide = scene->slides[0];
        shape.width = shape.height = 20;
        shape.transform = {1, 0, 0, 1, 50, 40};
        shape.source_groups = {"group"};
        grouped_slide.shapes = {shape};
        shape.source_id = "3";
        shape.fill.color = "#0000FF";
        shape.transform[4] = 130;
        grouped_slide.shapes.push_back(shape);
        grouped_slide.groups = {{"group", "", {1, 0, 0, 1, 50, 20}, 100, 60}};
        PresentationAnimation group_animation;
        group_animation.target = "group";
        group_animation.category = "emph";
        group_animation.filter = "spin";
        group_animation.rotation = 180;
        group_animation.duration = 1;
        grouped_slide.animations = {group_animation};
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        surface.setAnimationEnabled(true);
        surface.seekAnimation(2);
        auto grouped_image = paint();
        passed = check(grouped_image.pixelColor(60, 50) == QColor(Qt::blue) &&
                         grouped_image.pixelColor(140, 50) == QColor(Qt::red),
                     "group rotates child positions around the shared frame center") &&
            passed;
        group_animation.filter = "wipe(right)";
        group_animation.rotation = 0;
        group_animation.category = "entr";
        grouped_slide.animations = {group_animation};
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        surface.seekAnimation(0.5);
        grouped_image = paint();
        passed = check(grouped_image.pixelColor(60, 50) == QColor(Qt::red) &&
                         grouped_image.pixelColor(140, 50) == QColor(Qt::white),
                     "group reveal uses a common mask instead of exposing every child") &&
            passed;

        PresentationShape trigger_shape;
        trigger_shape.source_id = "button";
        trigger_shape.width = 40;
        trigger_shape.height = 40;
        trigger_shape.transform = {1, 0, 0, 1, 10, 30};
        trigger_shape.fill.color = "#FF0000";
        PresentationShape target_shape = trigger_shape;
        target_shape.source_id = "target";
        target_shape.transform[4] = 120;
        target_shape.fill.color = "#0000FF";
        PresentationAnimation interactive;
        interactive.target = "target";
        interactive.trigger = "button";
        interactive.category = "entr";
        interactive.filter = "fade";
        interactive.duration = 1;
        grouped_slide.shapes = {trigger_shape, target_shape};
        grouped_slide.groups.clear();
        grouped_slide.animations = {interactive};
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        surface.setAnimationEnabled(true);
        passed = check(paint().pixelColor(140, 50) == QColor(Qt::white) && !surface.advanceAnimation(),
                     "object-triggered entrance stays hidden and does not consume a page click") &&
            passed;
        passed = check(surface.triggerAnimationAt(20, 50) &&
                         surface.animationState().value("triggers").toInt() == 1,
                     "clicking the trigger shape starts its interactive sequence") &&
            passed;
        surface.seekAnimation(0.5);
        const QColor triggered = paint().pixelColor(140, 50);
        passed = check(triggered.blue() > 240 && triggered.red() > 100 && triggered.red() < 160 &&
                         !surface.triggerAnimationAt(140, 50),
                     "interactive fade paints independently and unrelated shapes do not trigger it") &&
            passed;
        surface.restartAnimation();
        passed = check(surface.animationState().value("triggers").toInt() == 0 &&
                         paint().pixelColor(140, 50) == QColor(Qt::white),
                     "restarting a slide clears interactive trigger state") &&
            passed;

        PresentationShape text_shape;
        text_shape.source_id = "text";
        text_shape.width = 180;
        text_shape.height = 90;
        text_shape.transform = {1, 0, 0, 1, 10, 5};
        PresentationRun text_run;
        text_run.font_size = 18;
        text_run.text = "FIRST";
        PresentationParagraph first_paragraph;
        first_paragraph.runs = {text_run};
        first_paragraph.space_after = 24;
        text_run.text = "SECOND";
        PresentationParagraph second_paragraph;
        second_paragraph.runs = {text_run};
        text_shape.text.paragraphs = {first_paragraph, second_paragraph};
        PresentationAnimation paragraph_fade;
        paragraph_fade.target = "text";
        paragraph_fade.category = "entr";
        paragraph_fade.filter = "fade";
        paragraph_fade.click = 1;
        paragraph_fade.duration = 1;
        paragraph_fade.paragraph_start = 0;
        paragraph_fade.paragraph_end = 0;
        grouped_slide.shapes = {text_shape};
        grouped_slide.animations = {paragraph_fade};
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        surface.setAnimationEnabled(true);
        const auto dark_pixels = [](const QImage& image, int top, int bottom)
        {
            int count = 0;
            for (int y = top; y < bottom; ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).red() < 220)
                        ++count;
            return count;
        };
        const QImage before_paragraph = paint();
        passed =
            check(dark_pixels(before_paragraph, 5, 40) == 0 && dark_pixels(before_paragraph, 40, 95) > 20,
                "paragraph entrance hides only its targeted text range") &&
            passed;
        surface.advanceAnimation();
        surface.seekAnimation(0.5);
        const QImage middle_paragraph = paint();
        passed =
            check(dark_pixels(middle_paragraph, 5, 40) > 10 && dark_pixels(middle_paragraph, 40, 95) > 20,
                "paragraph fade reveals targeted text without hiding later paragraphs") &&
            passed;

        text_run.text = "FIRST          SECOND";
        first_paragraph.runs = {text_run};
        first_paragraph.space_after = 0;
        text_shape.text.paragraphs = {first_paragraph};
        PresentationAnimation character_fade = paragraph_fade;
        character_fade.character_start = 0;
        character_fade.character_end = 5;
        character_fade.paragraph_start = -1;
        character_fade.paragraph_end = -1;
        grouped_slide.shapes = {text_shape};
        grouped_slide.animations = {character_fade};
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        surface.setAnimationEnabled(true);
        const int before_characters = dark_pixels(paint(), 5, 40);
        surface.advanceAnimation();
        surface.seekAnimation(0.5);
        const int middle_characters = dark_pixels(paint(), 5, 40);
        surface.seekAnimation(2);
        const int after_characters = dark_pixels(paint(), 5, 40);
        passed = check(before_characters > 10 && middle_characters > before_characters &&
                         after_characters > middle_characters,
                     "character fade changes only the targeted text while later characters stay visible") &&
            passed;

        character_fade.character_start = -1;
        character_fade.character_end = -1;
        character_fade.iterate_type = "wd";
        character_fade.iterate_interval = 0.5;
        character_fade.iterate_interval_percent = true;
        character_fade.iterate_count = 2;
        grouped_slide.animations = {character_fade};
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        surface.setAnimationEnabled(true);
        surface.advanceAnimation();
        surface.seekAnimation(0.25);
        const QImage first_word = paint();
        int first_word_pixels = 0;
        int second_word_pixels = 0;
        for (int y = 5; y < 40; ++y)
            for (int x = 0; x < first_word.width(); ++x)
                if (first_word.pixelColor(x, y).red() < 220)
                {
                    if (x < 90)
                        ++first_word_pixels;
                    else
                        ++second_word_pixels;
                }
        surface.seekAnimation(0.9);
        const QImage second_word = paint();
        int revealed_second_word_pixels = 0;
        for (int y = 5; y < 40; ++y)
            for (int x = 90; x < second_word.width(); ++x)
                if (second_word.pixelColor(x, y).red() < 220)
                    ++revealed_second_word_pixels;
        passed = check(first_word_pixels > 10 && second_word_pixels == 0 && revealed_second_word_pixels > 10,
                     "word iteration staggers independently painted word ranges") &&
            passed;
        return passed;
    }

    bool test_embedded_media_surface()
    {
        using namespace mirrorfly;
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const auto runtime = MFStartup(MF_VERSION);
        if (FAILED(com) || FAILED(runtime))
            return check(false, "media surface runtime initialization");
        QTemporaryDir directory;
        const auto path = directory.filePath("embedded.mp4");
        bool passed = check(SUCCEEDED(test_fixture::write_video(path)), "surface movie fixture");
        {
            QFile input(path);
            passed = check(input.open(QIODevice::ReadOnly), "read embedded movie") && passed;
            const auto bytes = input.readAll();
            auto scene =
                std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
            scene->width = 200;
            scene->height = 100;
            PresentationShape shape;
            shape.width = 200;
            shape.height = 100;
            shape.media_path = "ppt/media/embedded.mp4";
            shape.source_id = "video";
            scene->slides[0].shapes.push_back(shape);
            scene->slides.push_back({});
            scene->media.push_back({shape.media_path, "video/mp4",
                std::make_shared<const std::string>(
                    bytes.constData(), static_cast<std::size_t>(bytes.size()))});
            SlideRenderer renderer;
            renderer.setWidth(200);
            renderer.setHeight(100);
            renderer.setDocument(QVariant::fromValue(prepare_presentation(scene)));
            passed = check(renderer.mediaItems().size() == 1 && !renderer.mediaCommand(0, "play"),
                         "thumbnail media is disabled by default") &&
                passed;
            renderer.setMediaEnabled(true);
            passed = check(renderer.mediaCommand(0, "volume", 0) && renderer.mediaCommand(0, "play"),
                         "embedded media reaches session player through public renderer command") &&
                passed;
            bool red = false, blue = false;
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 6000 && !(red && blue))
            {
                QCoreApplication::processEvents();
                QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::white);
                QPainter painter(&image);
                renderer.paint(&painter);
                painter.end();
                const auto color = image.pixelColor(100, 50);
                red = red || (color.red() > 150 && color.blue() < 60);
                blue = blue || (color.blue() > 150 && color.red() < 60);
                QThread::msleep(10);
            }
            passed =
                check(red && blue, "embedded movie frames reach slide painter in object order") && passed;
            const auto state = renderer.mediaStates().value("0").toMap();
            passed =
                check(state.value("duration").toDouble() > 1.8 && state.value("error").toString().isEmpty(),
                    "media controls expose duration and error state") &&
                passed;
            renderer.setSlideIndex(1);
            passed = check(renderer.mediaStates().isEmpty() && renderer.mediaItems().isEmpty(),
                         "changing slide closes ordinary media playback and its temporary resources") &&
                passed;
            renderer.setSlideIndex(0);
            scene->slides[0].media_cues = {{"video", "play", 0, 0, 1.2, 0, false},
                {"video", "pause", 1, 0, -1, 0, false}, {"video", "stop", 2, 0, -1, 0, false}};
            renderer.setDocument(QVariant::fromValue(prepare_presentation(scene)));
            passed =
                check(renderer.mediaStates().isEmpty(), "static preview does not autoplay timing media") &&
                passed;
            renderer.setAnimationEnabled(true);
            bool automatic_blue = false;
            timer.restart();
            while (timer.elapsed() < 4000 && !automatic_blue)
            {
                QCoreApplication::processEvents();
                QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::white);
                QPainter painter(&image);
                renderer.paint(&painter);
                painter.end();
                const auto color = image.pixelColor(100, 50);
                automatic_blue = color.blue() > 150 && color.red() < 60;
                QThread::msleep(10);
            }
            passed = check(automatic_blue,
                         "autoplay seeks after metadata arrives and paints actual decoded frame") &&
                passed;
            passed = check(renderer.advanceAnimation(), "click advances to media pause cue") && passed;
            QCoreApplication::processEvents();
            passed = check(!renderer.mediaStates().value("0").toMap().value("playing").toBool(),
                         "timing pause stops media clock") &&
                passed;
            passed = check(renderer.advanceAnimation() && renderer.mediaStates().isEmpty(),
                         "timing stop releases the player") &&
                passed;
            scene->slides.emplace_back();
            scene->slides[0].media_cues = {{"video", "play", 0, 0, 0, 0, true, {}, 2}};
            renderer.setDocument(QVariant::fromValue(prepare_presentation(scene)));
            renderer.setAnimationEnabled(true);
            timer.restart();
            while (timer.elapsed() < 3000 &&
                !renderer.mediaStates().value("0").toMap().value("playing").toBool())
            {
                QCoreApplication::processEvents();
                QThread::msleep(10);
            }
            renderer.setSlideIndex(1);
            QCoreApplication::processEvents();
            passed = check(renderer.mediaStates().value("carried:0:0").toMap().value("playing").toBool(),
                         "numSld keeps a media timeline alive on the permitted following slide") &&
                passed;
            renderer.setSlideIndex(2);
            passed = check(renderer.mediaStates().isEmpty(),
                         "cross-slide media is released after its declared slide count") &&
                passed;
            renderer.setMediaEnabled(false);
            passed = check(!renderer.mediaCommand(0, "play"), "inactive page cannot start media") && passed;
        }
        MFShutdown();
        CoUninitialize();
        return passed;
    }

    bool test_preset_paths()
    {
        bool passed = true;
        for (const auto& name : mirrorfly::presentation_geometry_presets())
        {
            const auto geometry = mirrorfly::presentation_geometry(name, 200, 100);
            passed = check(geometry.error.empty(), "preset evaluation for renderer") && passed;
            for (const auto& path : geometry.paths)
            {
                const auto drawn = mirrorfly::presentation_path(path, 200, 100);
                passed = check(!drawn.isEmpty(), "each preset produces a real painter path") && passed;
            }
        }
        QImage image(220, 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.translate(10, 10);
        const auto donut = mirrorfly::presentation_geometry("donut", 200, 100);
        mirrorfly::paint_presentation_geometry(painter, donut, 200, 100, QBrush(Qt::red), QPen(Qt::NoPen));
        painter.end();
        passed = check(image.pixelColor(110, 60) == QColor(Qt::white) &&
                         image.pixelColor(110, 15) == QColor(Qt::red) &&
                         image.pixelColor(11, 11) == QColor(Qt::white),
                     "preset ring retains its hole and curved outer edge in headless rendering") &&
            passed;
        return passed;
    }

    bool wait_until_idle(mirrorfly::PresentationBridge& bridge)
    {
        if (!bridge.busy() && !bridge.syncing())
        {
            return true;
        }
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&bridge, &mirrorfly::PresentationBridge::stateChanged, &loop, [&bridge, &loop]()
        {
            if (!bridge.busy() && !bridge.syncing())
            {
                QTimer::singleShot(0, &loop, &QEventLoop::quit);
            }
        });
        timeout.start(10000);
        loop.exec();
        return check(
            !bridge.busy() && !bridge.syncing(), "presentation operation completes within ten seconds");
    }

    QByteArray png_fixture(const QColor& color = QColor(20, 90, 180))
    {
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(color);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }

#ifdef _WIN32
    void set_le_u16(QByteArray& bytes, qsizetype offset, std::uint16_t value)
    {
        bytes[offset] = static_cast<char>(value & 255);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 255);
    }

    void set_le_u32(QByteArray& bytes, qsizetype offset, std::uint32_t value)
    {
        for (int index = 0; index < 4; ++index)
        {
            bytes[offset + index] = static_cast<char>((value >> (index * 8)) & 255);
        }
    }

    std::uint32_t get_le_u32(const QByteArray& bytes, qsizetype offset)
    {
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
        {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + index]))
                << (index * 8);
        }
        return value;
    }

    bool paint_metafile_fixture(HDC dc)
    {
        if (!dc || SetMapMode(dc, MM_ANISOTROPIC) == 0 || !SetWindowOrgEx(dc, 0, 0, nullptr) ||
            !SetWindowExtEx(dc, 100, 100, nullptr) || !SetViewportOrgEx(dc, 0, 0, nullptr) ||
            !SetViewportExtEx(dc, 100, 100, nullptr))
        {
            return false;
        }
        const RECT left{0, 0, 50, 100};
        const RECT right{50, 0, 100, 100};
        const HBRUSH red = CreateSolidBrush(RGB(180, 60, 30));
        const HBRUSH blue = CreateSolidBrush(RGB(36, 90, 156));
        if (!red || !blue)
        {
            if (red)
                DeleteObject(red);
            if (blue)
                DeleteObject(blue);
            return false;
        }
        const bool painted = FillRect(dc, &left, red) && FillRect(dc, &right, blue);
        DeleteObject(red);
        DeleteObject(blue);
        return painted;
    }

    QByteArray emf_fixture()
    {
        const RECT frame{0, 0, 2540, 2540};
        HDC dc = CreateEnhMetaFileW(nullptr, nullptr, &frame, L"Mirrorfly\0EMF fixture\0");
        if (!paint_metafile_fixture(dc))
        {
            if (dc)
                DeleteEnhMetaFile(CloseEnhMetaFile(dc));
            return {};
        }
        const HENHMETAFILE metafile = CloseEnhMetaFile(dc);
        if (!metafile)
        {
            return {};
        }
        const UINT size = GetEnhMetaFileBits(metafile, 0, nullptr);
        QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
        if (!size || GetEnhMetaFileBits(metafile, size, reinterpret_cast<BYTE*>(bytes.data())) != size)
        {
            bytes.clear();
        }
        DeleteEnhMetaFile(metafile);
        return bytes;
    }

    QByteArray wmf_fixture()
    {
        HDC dc = CreateMetaFileW(nullptr);
        if (!paint_metafile_fixture(dc))
        {
            if (dc)
                DeleteMetaFile(CloseMetaFile(dc));
            return {};
        }
        const HMETAFILE metafile = CloseMetaFile(dc);
        if (!metafile)
        {
            return {};
        }
        const UINT size = GetMetaFileBitsEx(metafile, 0, nullptr);
        QByteArray records(static_cast<qsizetype>(size), Qt::Uninitialized);
        if (!size || GetMetaFileBitsEx(metafile, size, records.data()) != size)
        {
            records.clear();
        }
        DeleteMetaFile(metafile);
        if (records.isEmpty())
        {
            return {};
        }
        QByteArray header(22, '\0');
        set_le_u32(header, 0, 0x9AC6CDD7);
        set_le_u16(header, 6, 0);
        set_le_u16(header, 8, 0);
        set_le_u16(header, 10, 1440);
        set_le_u16(header, 12, 1440);
        set_le_u16(header, 14, 1440);
        std::uint16_t checksum = 0;
        for (qsizetype offset = 0; offset < 20; offset += 2)
        {
            checksum ^= static_cast<std::uint16_t>(static_cast<unsigned char>(header[offset])) |
                static_cast<std::uint16_t>(static_cast<unsigned char>(header[offset + 1]) << 8);
        }
        set_le_u16(header, 20, checksum);
        return header + records;
    }

    bool metafile_colors(const QImage& image)
    {
        if (image.width() < 4 || image.height() < 2)
        {
            return false;
        }
        const QColor left = image.pixelColor(image.width() / 4, image.height() / 2);
        const QColor right = image.pixelColor(image.width() * 3 / 4, image.height() / 2);
        return left.red() > 140 && left.green() < 100 && left.blue() < 80 && right.red() < 80 &&
            right.green() > 60 && right.blue() > 120;
    }

    bool test_metafile_images()
    {
        using namespace mirrorfly;
        const QByteArray emf = emf_fixture();
        const QByteArray wmf = wmf_fixture();
        auto scene = std::make_shared<PresentationScene>();
        scene->images.push_back({"sample.emf", "image/x-emf", emf.toStdString()});
        scene->images.push_back({"sample.wmf", "image/x-wmf", wmf.toStdString()});
        const auto document = prepare_presentation(scene);
        const QImage emf_image = presentation_image(document, "sample.emf");
        const QImage wmf_image = presentation_image(document, "sample.wmf");
        bool passed = check(
            !emf.isEmpty() && !wmf.isEmpty(), "Windows APIs create non-empty EMF and placeable WMF fixtures");
        passed = check(!emf_image.isNull(), "package EMF decodes from memory") && passed;
        passed = check(emf_image.isNull() || metafile_colors(emf_image),
                     "package EMF renders real colored records from memory") &&
            passed;
        passed = check(!wmf_image.isNull() && metafile_colors(wmf_image),
                     "package placeable WMF renders real colored records from memory") &&
            passed;
        passed = check(document->image_sizes.value(QStringLiteral("sample.emf")) == emf_image.size() &&
                         document->image_sizes.value(QStringLiteral("sample.wmf")) == wmf_image.size(),
                     "metafile inspection and rendering report matching source dimensions") &&
            passed;
        passed = check(emf_image.constBits() == presentation_image(document, "sample.emf").constBits(),
                     "metafile pixels share the bounded image cache") &&
            passed;

        QByteArray commented_wmf = wmf;
        QByteArray comment(22, '\0');
        set_le_u32(comment, 0, 11);
        set_le_u16(comment, 4, META_ESCAPE);
        set_le_u16(comment, 6, MFCOMMENT);
        set_le_u16(comment, 8, 12);
        commented_wmf.insert(40, comment);
        set_le_u32(commented_wmf, 28, static_cast<std::uint32_t>((commented_wmf.size() - 22) / 2));
        set_le_u32(commented_wmf, 34, std::max(11U, get_le_u32(wmf, 34)));
        passed = check(!decode_metafile_image(commented_wmf.toStdString(), MetafileFormat::Wmf).rgba.empty(),
                     "bounded WMF metadata comments are stripped without losing drawing records") &&
            passed;
        auto unsafe_wmf = commented_wmf;
        set_le_u16(unsafe_wmf, 46, SETABORTPROC);
        auto malformed_comment = commented_wmf;
        set_le_u16(malformed_comment, 48, 65535);
        passed =
            check(decode_metafile_image(unsafe_wmf.toStdString(), MetafileFormat::Wmf).rgba.empty() &&
                    decode_metafile_image(malformed_comment.toStdString(), MetafileFormat::Wmf).rgba.empty(),
                "unsafe WMF escape functions and overflowing comments remain rejected") &&
            passed;

        const int bitmap_width = 1754, bitmap_height = 1240;
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = bitmap_width;
        bitmap.bmiHeader.biHeight = bitmap_height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        QByteArray pixels(bitmap_width * bitmap_height * 4, static_cast<char>(0xff));
        const HDC bitmap_dc = CreateEnhMetaFileW(nullptr, nullptr, nullptr, nullptr);
        if (bitmap_dc)
            StretchDIBits(bitmap_dc, 0, 0, bitmap_width, bitmap_height, 0, 0, bitmap_width, bitmap_height,
                pixels.constData(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
        const HENHMETAFILE bitmap_metafile = bitmap_dc ? CloseEnhMetaFile(bitmap_dc) : nullptr;
        QByteArray large_emf;
        if (bitmap_metafile)
        {
            large_emf.resize(GetEnhMetaFileBits(bitmap_metafile, 0, nullptr));
            GetEnhMetaFileBits(bitmap_metafile, static_cast<UINT>(large_emf.size()),
                reinterpret_cast<BYTE*>(large_emf.data()));
            DeleteEnhMetaFile(bitmap_metafile);
        }
        passed = check(large_emf.size() > 8 * 1024 * 1024 &&
                         !decode_metafile_image(large_emf.toStdString(), MetafileFormat::Emf).rgba.empty(),
                     "large bounded uncompressed DIB records render within the overall metafile budget") &&
            passed;
        for (qsizetype offset = 0; offset + 80 <= large_emf.size();)
        {
            const auto size = get_le_u32(large_emf, offset + 4);
            if (get_le_u32(large_emf, offset) == EMR_STRETCHDIBITS)
            {
                auto invalid = large_emf;
                set_le_u32(invalid, offset + 56, 0xfffffff0);
                passed = check(decode_metafile_image(invalid.toStdString(), MetafileFormat::Emf).rgba.empty(),
                             "large bitmap offsets cannot escape their containing record") &&
                    passed;
                invalid = large_emf;
                set_le_u32(invalid, offset + get_le_u32(large_emf, offset + 48) + 4, 0x7fffffff);
                passed = check(decode_metafile_image(invalid.toStdString(), MetafileFormat::Emf).rgba.empty(),
                             "large bitmap dimensions cannot exceed the source pixel budget") &&
                    passed;
                break;
            }
            if (size < 8)
                break;
            offset += size;
        }

        QByteArray blocked_emf = emf;
        const std::uint32_t first_record_size = get_le_u32(blocked_emf, 4);
        if (first_record_size + 8 <= static_cast<std::uint32_t>(blocked_emf.size()))
        {
            set_le_u32(blocked_emf, first_record_size, EMR_GLSRECORD);
        }
        QByteArray bad_wmf = wmf;
        bad_wmf[20] = static_cast<char>(bad_wmf[20] ^ 1);
        const std::string oversized(16 * 1024 * 1024 + 1, '\0');
        passed = check(decode_metafile_image(emf.left(64).toStdString(), MetafileFormat::Emf).rgba.empty() &&
                         decode_metafile_image(emf.toStdString(), MetafileFormat::Wmf).rgba.empty() &&
                         decode_metafile_image(blocked_emf.toStdString(), MetafileFormat::Emf).rgba.empty() &&
                         decode_metafile_image(bad_wmf.toStdString(), MetafileFormat::Wmf).rgba.empty() &&
                         inspect_metafile_image(oversized, MetafileFormat::Emf).width == 0,
                     "truncated, mismatched, GL, bad-checksum and oversized metafiles are rejected") &&
            passed;
        return passed;
    }
#else
    bool test_metafile_images()
    {
        return true;
    }
#endif

    void set_u32(QByteArray& bytes, qsizetype offset, std::uint32_t value)
    {
        for (int index = 0; index < 4; ++index)
        {
            bytes[offset + index] = static_cast<char>((value >> ((3 - index) * 8)) & 255);
        }
    }

    QByteArray huge_png_fixture()
    {
        QByteArray bytes = png_fixture();
        set_u32(bytes, 16, 50000);
        set_u32(bytes, 20, 50000);
        std::uint32_t crc = 0xFFFFFFFF;
        for (qsizetype offset = 12; offset < 29; ++offset)
        {
            crc ^= static_cast<unsigned char>(bytes[offset]);
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320U : 0U);
            }
        }
        set_u32(bytes, 29, ~crc);
        return bytes;
    }

    bool test_tiff_images()
    {
        using namespace mirrorfly;
        const auto fixture = [](const QString& name)
        {
            QFile file(QDir(QString::fromUtf8(MIRRORFLY_TEST_FIXTURE_DIRECTORY)).filePath(name));
            if (!file.open(QIODevice::ReadOnly))
                return QByteArray{};
            return file.readAll();
        };
        auto scene = std::make_shared<PresentationScene>();
        const auto bytes = fixture(QStringLiteral("presentation-rgb.tiff"));
        scene->images.push_back({"sample.tiff", "image/tiff", bytes.toStdString()});
        scene->images.push_back(
            {"large.tiff", "image/tiff", fixture(QStringLiteral("presentation-large.tiff")).toStdString()});
        const auto document = prepare_presentation(scene);
        const auto image = presentation_image(document, "sample.tiff");
        bool passed = check(!bytes.isEmpty() && image.size() == QSize(8, 8) &&
                image.pixelColor(1, 3) == QColor(Qt::red) && image.pixelColor(6, 3) == QColor(Qt::blue),
            "embedded LZW TIFF decodes real colored pixels through the platform interface");
        passed = check(image.constBits() == presentation_image(document, "sample.tiff").constBits(),
                     "TIFF pixels share the bounded image cache") &&
            passed;
        const auto scaled = presentation_image(document, "large.tiff");
        passed = check(document->image_sizes.value(QStringLiteral("large.tiff")) == QSize(4096, 8) &&
                         scaled.size() == QSize(2048, 4) && scaled.pixelColor(100, 2) == QColor(Qt::red) &&
                         scaled.pixelColor(1900, 2) == QColor(Qt::blue),
                     "TIFF downsampling keeps original physical dimensions and both image halves") &&
            passed;
        const QColor first[]{Qt::blue, Qt::magenta, Qt::cyan, Qt::red, Qt::cyan, Qt::magenta, Qt::blue};
        const QColor last[]{Qt::cyan, Qt::red, Qt::blue, Qt::magenta, Qt::blue, Qt::red, Qt::cyan};
        for (int orientation = 2; orientation <= 8; ++orientation)
        {
            const auto source = fixture(QStringLiteral("presentation-orientation-%1.tiff").arg(orientation));
            const auto decoded = decode_tiff_image(source.toStdString());
            const auto size = inspect_tiff_image(source.toStdString());
            const int width = orientation >= 5 ? 3 : 2;
            const int height = orientation >= 5 ? 2 : 3;
            const bool valid = decoded.size.width == width && decoded.size.height == height &&
                size.width == width && size.height == height && decoded.rgba.size() == 24;
            passed =
                check(valid, "TIFF orientation applies matching metadata and decoded dimensions") && passed;
            if (valid)
            {
                const QImage oriented(decoded.rgba.data(), width, height, width * 4, QImage::Format_RGBA8888);
                passed = check(oriented.pixelColor(0, 0) == first[orientation - 2] &&
                                 oriented.pixelColor(width - 1, height - 1) == last[orientation - 2],
                             "TIFF rotation and reflection preserve the correct corner pixels") &&
                    passed;
            }
        }
        auto huge = bytes;
        const auto word = [&](int position)
        {
            return static_cast<unsigned char>(huge[position]) |
                (static_cast<unsigned char>(huge[position + 1]) << 8);
        };
        const int directory = word(4) | (word(6) << 16);
        const int entries = word(directory);
        for (int index = 0; index < entries; ++index)
        {
            const int entry = directory + 2 + index * 12;
            if (word(entry) == 256 || word(entry) == 257)
            {
                huge[entry + 8] = static_cast<char>(0xff);
                huge[entry + 9] = static_cast<char>(0xff);
            }
        }
        passed = check(inspect_tiff_image(huge.toStdString()).width == 0 &&
                         decode_tiff_image(huge.toStdString()).rgba.empty() &&
                         decode_tiff_image(bytes.left(16).toStdString()).rgba.empty() &&
                         decode_tiff_image("not TIFF").rgba.empty(),
                     "oversized and malformed TIFF input is rejected before pixel allocation") &&
            passed;
        return passed;
    }

    bool test_fonts_and_image_limits()
    {
        auto scene = std::make_shared<mirrorfly::PresentationScene>();
        mirrorfly::PresentationSlide slide;
        mirrorfly::PresentationShape shape;
        mirrorfly::PresentationParagraph paragraph;
        for (int index = 0; index < 6; ++index)
        {
            mirrorfly::PresentationRun run;
            run.text = "text";
            run.font_family = "Mirrorfly Missing Font " + std::to_string(index);
            run.east_asian_font_family = "Mirrorfly Missing CJK Font";
            paragraph.runs.push_back(run);
        }
        shape.text.paragraphs.push_back(paragraph);
        slide.shapes.push_back(shape);
        scene->slides.push_back(slide);
        const auto png = png_fixture();
        const auto huge_png = huge_png_fixture();
        scene->images.push_back(
            {"image.png", "image/png", std::string(png.constData(), static_cast<std::size_t>(png.size()))});
        scene->images.push_back({"large.png", "image/png",
            std::string(huge_png.constData(), static_cast<std::size_t>(huge_png.size()))});
        scene->images.push_back({"safe.svg", "image/svg+xml",
            "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8' viewBox='0 0 8 8'>"
            "<rect width='8' height='8' fill='#B43C1E'/><rect x='4' width='4' height='8' fill='#245A9C'/>"
            "</svg>"});
        scene->images.push_back({"external.svg", "image/svg+xml",
            "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'><image "
            "href='file:///must-not-read.png'/></svg>"});
        auto document = mirrorfly::prepare_presentation(scene);
        bool passed = check(document->scene.get() == scene.get(),
            "render resources share the immutable scene without copying");
        passed = check(document->font_summary.contains(QStringLiteral("Mirrorfly Missing Font 5")) &&
                         document->font_summary.contains(QStringLiteral("Mirrorfly Missing CJK Font")),
                     "font summary retains every missing Latin and CJK family") &&
            passed;
        passed = check(!document->fonts.value(QStringLiteral("mirrorfly missing font 0")).isEmpty(),
                     "missing fonts resolve to a system fallback") &&
            passed;
        if (QGuiApplication::platformName() == QStringLiteral("windows"))
        {
            const auto installed = QFontDatabase::families();
            passed =
                check(!installed.isEmpty() &&
                        installed.contains(document->fonts.value(QStringLiteral("mirrorfly missing font 0"))),
                    "Windows fallback selects an installed system font without bundled font files") &&
                passed;
        }
        const QImage first = mirrorfly::presentation_image(document, "image.png");
        const QImage second = mirrorfly::presentation_image(document, "image.png");
        passed = check(first.size() == QSize(8, 8) && first.constBits() == second.constBits(),
                     "raster images decode once and use implicitly shared cached storage") &&
            passed;
        auto changed_scene = std::make_shared<mirrorfly::PresentationScene>(*scene);
        const auto changed_png = png_fixture(QColor(180, 60, 30));
        passed = check(changed_png.size() == png.size(),
                     "cache identity fixture images use the same encoded byte length") &&
            passed;
        changed_scene->images.front().bytes = std::make_shared<const std::string>(
            changed_png.constData(), static_cast<std::size_t>(changed_png.size()));
        const auto changed_document = mirrorfly::prepare_presentation(changed_scene, document);
        const QImage changed = mirrorfly::presentation_image(changed_document, "image.png");
        passed = check(changed.pixelColor(0, 0) == QColor(180, 60, 30),
                     "same-name same-size changed image bytes do not reuse stale decoded pixels") &&
            passed;
        passed = check(mirrorfly::presentation_image(document, "large.png").isNull(),
                     "oversized declared PNG dimensions are rejected before decoding") &&
            passed;
        const QImage svg = mirrorfly::presentation_image(document, "safe.svg");
        passed = check(document->image_sizes.value(QStringLiteral("safe.svg")) == QSize(8, 8) &&
                         svg.size() == QSize(8, 8) && svg.pixelColor(1, 4) == QColor(180, 60, 30) &&
                         svg.pixelColor(6, 4) == QColor(36, 90, 156),
                     "package SVG renders static colored pixels through the platform interface") &&
            passed;
        passed = check(mirrorfly::presentation_image(document, "external.svg").isNull(),
                     "SVG external image references are rejected without filesystem access") &&
            passed;
        const auto malformed_svg = mirrorfly::decode_svg_image("<svg><path");
        const auto oversized_svg = mirrorfly::inspect_svg_image(
            "<svg xmlns='http://www.w3.org/2000/svg' width='5000' height='5000'/>");
        const auto active_svg = mirrorfly::decode_svg_image(
            "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'><script>bad()</script></svg>");
        const auto css_svg = mirrorfly::decode_svg_image(
            "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'>"
            "<rect width='8' height='8' style='fill:url(file:///must-not-read.svg)'/></svg>");
        passed =
            check(malformed_svg.rgba.empty() && oversized_svg.width == 0 && active_svg.rgba.empty() &&
                    css_svg.rgba.empty(),
                "malformed, active, external and oversized SVG resources are rejected before rendering") &&
            passed;
        return passed;
    }

    bool test_embedded_font_session_revisions()
    {
        using namespace mirrorfly;
        auto scene = std::make_shared<PresentationScene>();
        PresentationShape shape;
        shape.width = 100;
        shape.height = 40;
        shape.editable = true;
        PresentationRun run;
        run.text = "embedded font revision";
        run.font_family = "Mirrorfly Embedded A";
        PresentationParagraph paragraph;
        paragraph.runs.push_back(run);
        shape.text.paragraphs.push_back(paragraph);
        PresentationSlide slide;
        slide.shapes = {shape, shape};
        scene->slides.push_back(slide);
        scene->embedded_fonts = {{"Mirrorfly Embedded A", "regular", "font-a.fntdata",
                                     std::make_shared<const std::string>("invalid-a")},
            {"Mirrorfly Embedded B", "regular", "font-b.fntdata",
                std::make_shared<const std::string>("invalid-b")}};
        scene->images.push_back({"unused.svg", "image/svg+xml", "<svg/>"});
        PresentationPrepareOptions options;
        options.eager_image_analysis = false;
        const auto original = prepare_presentation(scene, {}, options);
        auto deleted = std::make_shared<PresentationScene>(*scene);
        deleted->slides.front().shapes.erase(deleted->slides.front().shapes.begin());
        deleted->images.clear();
        const auto after_delete = prepare_presentation(deleted, original, options);
        bool passed = check(original->font_loader && after_delete->font_loader == original->font_loader,
            "object and image removal retain unchanged embedded font registrations during full analysis");
        const auto undone = prepare_presentation(scene, after_delete, options);
        const auto redone = prepare_presentation(deleted, undone, options);
        passed = check(undone->font_loader == original->font_loader &&
                         redone->font_loader == original->font_loader,
                     "undo and redo retain the document font session") &&
            passed;
        auto restyled = std::make_shared<PresentationScene>(*deleted);
        restyled->slides.front().shapes.front().text.paragraphs.front().runs.front().font_family =
            "Mirrorfly Embedded B";
        const auto after_style = prepare_presentation(restyled, redone, options);
        passed = check(after_style->font_loader == original->font_loader &&
                         after_style->embedded_fonts.contains("mirrorfly embedded b") &&
                         !after_style->embedded_fonts.contains("mirrorfly embedded a"),
                     "font usage is reanalyzed independently of the retained font session") &&
            passed;
        auto changed = std::make_shared<PresentationScene>(*restyled);
        changed->embedded_fonts.back().bytes = std::make_shared<const std::string>("changed-b");
        options.reuse_analysis = true;
        const auto after_bytes = prepare_presentation(changed, after_style, options);
        passed =
            check(after_bytes->font_loader != original->font_loader,
                "same-path changed embedded font bytes invalidate registration even with analysis reuse") &&
            passed;
        auto renamed = std::make_shared<PresentationScene>(*changed);
        renamed->embedded_fonts.back().family = "Mirrorfly Embedded C";
        const auto after_metadata = prepare_presentation(renamed, after_bytes, options);
        passed = check(after_metadata->font_loader != after_bytes->font_loader &&
                         !after_metadata->embedded_fonts.contains("mirrorfly embedded b"),
                     "changed font metadata invalidates both registration and usage analysis") &&
            passed;
        auto removed = std::make_shared<PresentationScene>(*renamed);
        removed->embedded_fonts.clear();
        const auto after_removal = prepare_presentation(removed, after_metadata, options);
        passed = check(!after_removal->font_loader && after_removal->embedded_fonts.isEmpty(),
                     "removing embedded resources releases the font session from the new revision") &&
            passed;
        return passed;
    }

    bool test_paint_and_texture_bounds()
    {
        auto scene = std::make_shared<mirrorfly::PresentationScene>();
        scene->width = 100;
        scene->height = 100;
        mirrorfly::PresentationSlide slide;
        mirrorfly::PresentationShape shape;
        shape.width = 30;
        shape.height = 20;
        shape.transform = {1, 0, 0, 1, 10, 15};
        shape.fill.color = "#D03020";
        slide.shapes.push_back(shape);
        scene->slides.push_back(slide);
        mirrorfly::SlideRenderer renderer;
        renderer.setDocument(QVariant::fromValue(mirrorfly::prepare_presentation(scene)));
        renderer.setSize(QSizeF(100, 100));
        QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.paint(&painter);
        painter.end();
        bool passed = check(
            image.pixelColor(20, 25) == QColor("#D03020") && image.pixelColor(80, 80) == QColor(Qt::white),
            "native painting applies document transforms, fills and slide background");
        passed = check(renderer.hitTest(20, 25) == 0 && renderer.hitTest(80, 80) == -1,
                     "hit testing finds a shape and rejects empty slide space") &&
            passed;
        renderer.setSize(QSizeF(200, 100));
        passed = check(renderer.hitTest(70, 25) == 0 && renderer.hitTest(20, 25) == -1,
                     "hit testing accounts for letterboxed slide coordinates") &&
            passed;
        renderer.setSelectedShape(0);
        const auto editor = renderer.textEditorState();
        passed = check(editor.value("valid").toBool() && editor.value("tx").toDouble() == 60 &&
                         editor.value("ty").toDouble() == 15 && editor.value("width").toDouble() == 30,
                     "inline editor uses the same letterboxed transform as hit testing") &&
            passed;
        renderer.setSize(QSizeF(200, 200));
        passed = check(renderer.textEditorState().value("a").toDouble() == 2 &&
                         renderer.textEditorState().value("tx").toDouble() == 20,
                     "inline geometry tracks zoom without changing document coordinates") &&
            passed;
        renderer.setSize(QSizeF(200, 100));
        passed = check(renderer.hitTest(std::numeric_limits<qreal>::quiet_NaN(), 25) == -1,
                     "hit testing rejects nonfinite coordinates") &&
            passed;
        renderer.setSlideIndex(4);
        passed =
            check(renderer.textEditorState().empty(), "invalid slide exposes no editable text") && passed;
        passed = check(renderer.hitTest(70, 25) == -1, "hit testing rejects an out-of-range slide") && passed;
        renderer.setSlideIndex(0);
        renderer.setSize(QSizeF(10000000, 10000000));
        const QSize texture = renderer.textureSize();
        passed = check(texture.width() <= 4096 && texture.height() <= 4096 &&
                         static_cast<qint64>(texture.width()) * texture.height() <= 8 * 1024 * 1024,
                     "large document geometry cannot request an unbounded GPU texture") &&
            passed;
        for (const qreal ratio : {1.25, 1.5, 2.0, 3.0})
        {
            const QSize logical = mirrorfly::bounded_slide_texture(QSizeF(10000000, 10000000), ratio);
            const QSize physical = logical * ratio;
            passed = check(physical.width() <= 4096 && physical.height() <= 4096 &&
                             static_cast<qint64>(physical.width()) * physical.height() <= 8 * 1024 * 1024,
                         "high-DPI multiplication remains within the physical texture budget") &&
                passed;
        }
        renderer.setDocument(QVariant{});
        passed = check(!renderer.document().isValid(), "clearing the renderer releases its shared scene") &&
            passed;
        return passed;
    }

    bool test_slide_transitions()
    {
        auto scene = std::make_shared<mirrorfly::PresentationScene>();
        scene->width = 100;
        scene->height = 100;
        mirrorfly::PresentationSlide first;
        first.background.color = "#B43C1E";
        first.transition.type = "fade";
        first.transition.duration = 0.4;
        mirrorfly::PresentationSlide second;
        second.background.color = "#245A9C";
        second.transition.type = "push";
        second.transition.direction = "l";
        second.transition.duration = 0.8;
        second.transition.advance_on_click = false;
        second.transition.advance_after = 2.5;
        scene->slides = {first, second};
        mirrorfly::SlideRenderer renderer;
        renderer.setDocument(QVariant::fromValue(mirrorfly::prepare_presentation(scene)));
        renderer.setSize(QSizeF(100, 100));
        renderer.setTransitionsEnabled(true);
        renderer.setSlideIndex(1);
        bool passed = check(renderer.transitionState().value("active").toBool() &&
                renderer.transitionState().value("type").toString() == QStringLiteral("push") &&
                renderer.transitionState().value("durationMs").toInt() == 800 &&
                !renderer.transitionState().value("advanceOnClick").toBool() &&
                renderer.transitionState().value("advanceAfterMs").toInt() == 2500,
            "slide transition exposes type, timing and advance rules");
        passed =
            check(renderer.seekTransition(0.5), "transition timeline supports bounded deterministic seek") &&
            passed;
        QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.paint(&painter);
        painter.end();
        passed = check(image.pixelColor(25, 50) == QColor("#B43C1E") &&
                         image.pixelColor(75, 50) == QColor("#245A9C"),
                     "halfway push transition paints outgoing and incoming slides in motion") &&
            passed;
        passed = check(renderer.seekTransition(1) && !renderer.transitionState().value("active").toBool(),
                     "finishing a page transition clears its playback state") &&
            passed;
        image.fill(Qt::transparent);
        painter.begin(&image);
        renderer.paint(&painter);
        painter.end();
        passed = check(image.pixelColor(25, 50) == QColor("#245A9C") &&
                         image.pixelColor(75, 50) == QColor("#245A9C"),
                     "completed transition leaves only the destination slide") &&
            passed;
        renderer.setSlideIndex(0);
        passed = check(renderer.transitionState().value("active").toBool(),
                     "reverse navigation starts the destination slide transition") &&
            passed;
        renderer.setTransitionsEnabled(false);
        passed = check(!renderer.transitionState().value("active").toBool() && !renderer.seekTransition(0.5),
                     "leaving presentation mode cancels page transition state") &&
            passed;
        return passed;
    }

    bool test_transition_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.editSchema().contains("setSlideTransition") &&
                editor.slideTransition().value("editable").toBool(),
            "transition bridge and AI schema expose the same capability");
        const auto slide_node = [&editor]()
        {
            for (const auto& item : editor.semanticTree(0, 0).value("nodes").toList())
            {
                const auto node = item.toMap();
                if (node.value("type") == QStringLiteral("slide"))
                    return node;
            }
            return QVariantMap{};
        };
        passed = check(slide_node().value("actions").toList().contains("setSlideTransition"),
                     "slide semantic node advertises transition edit") &&
            passed;
        passed = check(editor.applyEdit(QStringLiteral("insertTable"), {{"rows", 2}, {"columns", 2}}) &&
                         wait_until_idle(editor),
                     "bridge creates a sourced slide for transition synchronization") &&
            passed;
        const QVariantMap options{{"type", "push"}, {"direction", "l"}, {"durationSeconds", 1.0},
            {"advanceOnClick", false}, {"advanceAfterSeconds", 2.5}};
        passed = check(editor.applyEdit(QStringLiteral("setSlideTransition"), options) &&
                         editor.pendingEdits() == 1 && wait_until_idle(editor),
                     "transition edit reports asynchronous progress") &&
            passed;
        passed = check(editor.slideTransition().value("type") == QStringLiteral("push") &&
                         slide_node().value("transition").toMap().value("advanceAfterSeconds") == 2.5,
                     "bridge and semantic tree agree after transition commit") &&
            passed;
        editor.undo();
        passed = check(wait_until_idle(editor) && editor.slideTransition().value("type").toString().isEmpty(),
                     "transition edit supports undo") &&
            passed;
        editor.redo();
        passed =
            check(wait_until_idle(editor) && editor.slideTransition().value("type") == QStringLiteral("push"),
                "transition edit supports redo") &&
            passed;
        return passed;
    }

    bool test_template_preview_reuse()
    {
        using mirrorfly::RenderPresentationPtr;
        mirrorfly::PresentationBridge bridge;
        const auto original = bridge.templatePreviews({});
        const auto repeated = bridge.templatePreviews({});
        bool passed = check(
            original.size() == 4 && repeated.size() == 4, "template previews include every gallery layout");
        for (auto it = original.cbegin(); it != original.cend(); ++it)
        {
            passed = check(it.value().value<RenderPresentationPtr>() ==
                             repeated.value(it.key()).value<RenderPresentationPtr>(),
                         "unchanged template palettes reuse prepared previews") &&
                passed;
        }
        const QVariantMap changed{
            {"researchStudio", QVariantMap{{"palette", QVariantMap{{"accent", "#2A6B9A"}}}}}};
        const auto recolored = bridge.templatePreviews(changed);
        passed = check(recolored.value("researchStudio").value<RenderPresentationPtr>() !=
                         original.value("researchStudio").value<RenderPresentationPtr>(),
                     "a changed palette prepares the affected layout again") &&
            passed;
        for (auto it = original.cbegin(); it != original.cend(); ++it)
        {
            if (it.key() != "researchStudio")
                passed = check(it.value().value<RenderPresentationPtr>() ==
                                 recolored.value(it.key()).value<RenderPresentationPtr>(),
                             "unchanged layouts keep their prepared previews") &&
                    passed;
        }
        return passed;
    }

    bool test_bridge_state()
    {
        mirrorfly::PresentationBridge bridge;
        int completions = 0;
        int successful = 0;
        int recorded = 0;
        bool completion_after_idle = true;
        QObject::connect(&bridge, &mirrorfly::PresentationBridge::openCompleted, &bridge, [&](bool success)
        {
            ++completions;
            successful += success ? 1 : 0;
            completion_after_idle = completion_after_idle && !bridge.busy();
        });
        QObject::connect(&bridge, &mirrorfly::PresentationBridge::fileRecorded, &bridge, [&](const QString&)
        {
            ++recorded;
        });
        bool passed = check(bridge.requestOpen(QUrl(QStringLiteral("https://example.invalid/slides.pptx"))) &&
                completions == 1 && !bridge.busy() && !bridge.active(),
            "an accepted invalid URL completes once without activating a preview");
        const QString path = QDir(QString::fromUtf8(MIRRORFLY_TEST_FIXTURE_DIRECTORY))
                                 .filePath(QStringLiteral("presentation-deflate.pptx"));
        passed = check(bridge.requestOpen(QUrl::fromLocalFile(path)) && bridge.busy(),
                     "PPTX file loading starts asynchronously") &&
            passed;
        passed = check(!bridge.requestOpen(QUrl::fromLocalFile(path)) && completions == 1,
                     "a duplicate busy request is rejected without a false completion") &&
            passed;
        passed = wait_until_idle(bridge) && passed;
        passed = check(bridge.active() && bridge.slideCount() == 1 && bridge.currentSlide() == 0 &&
                         bridge.zoom() == 0 && successful == 1 && recorded == 1,
                     "successful loading activates the ordered document at the first slide") &&
            passed;
        const QVariant previous = bridge.document();
        bridge.setSlide(-1);
        bridge.setSlide(1000);
        bridge.setZoom(5);
        passed = check(bridge.currentSlide() == 0 && bridge.zoom() == 3,
                     "navigation and zoom remain within supported bounds") &&
            passed;
        bridge.requestOpen(QUrl::fromLocalFile(path + QStringLiteral(".missing.pptx")));
        passed = wait_until_idle(bridge) && passed;
        passed = check(bridge.active() &&
                         bridge.document().value<mirrorfly::RenderPresentationPtr>() ==
                             previous.value<mirrorfly::RenderPresentationPtr>() &&
                         recorded == 1 && !bridge.error().isEmpty(),
                     "failed loading preserves the prior scene and does not record a nonexistent file") &&
            passed;
        passed = check(completions == 3 && completion_after_idle,
                     "every accepted operation completes once after busy is cleared") &&
            passed;
        bridge.showHome();
        passed = check(!bridge.active() && bridge.slideCount() == 0 && !bridge.document().isValid(),
                     "returning home releases the preview document and image cache") &&
            passed;
        return passed;
    }

    bool test_system_font_application()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        editor.selectShape(0);
        editor.applyEdit(
            QStringLiteral("updateText"), {{"text", QStringLiteral("学术研究 演示文字 Aa 0123")}});
        const auto installed = QFontDatabase::families(QFontDatabase::SimplifiedChinese);
        QStringList candidates;
        for (const auto& family : QStringList{"Microsoft YaHei", "SimSun", "KaiTi", "FangSong"})
        {
            if (installed.contains(family))
                candidates.append(family);
        }
        if (candidates.size() < 2)
        {
            std::cout << "SKIP: two distinct Chinese system fonts unavailable\n";
            return true;
        }
        QImage previous;
        bool passed = true;
        for (const auto& family : candidates)
        {
            passed =
                check(editor.applyEdit("formatText", {{"fontFamily", family}}), "system family applies") &&
                passed;
            const auto document = editor.document().value<mirrorfly::RenderPresentationPtr>();
            const auto& run =
                document->scene->slides.front().shapes.front().text.paragraphs.front().runs.front();
            passed = check(QString::fromStdString(run.font_family) == family &&
                             QString::fromStdString(run.east_asian_font_family) == family,
                         "Latin and East Asian font names both persist") &&
                passed;
            mirrorfly::SlideRenderer renderer;
            renderer.setDocument(editor.document());
            renderer.setWidth(960);
            renderer.setHeight(540);
            renderer.setSelectedShape(0);
            passed = check(renderer.textEditorState().value("fontFamily") == family,
                         "inline editor receives the selected installed family") &&
                passed;
            renderer.setSelectedShape(-1);
            QImage image(960, 540, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            renderer.paint(&painter);
            painter.end();
            passed = check(previous.isNull() || previous != image,
                         "changing Chinese family changes painted glyphs at identical size") &&
                passed;
            previous = image;
        }
        editor.applyEdit("updateText", {{"text", QStringLiteral("汉字字体研究")}});
        editor.applyEdit("formatText", {{"fontFamily", candidates[0]}});
        auto mixed = std::make_shared<mirrorfly::PresentationScene>(
            *editor.document().value<mirrorfly::RenderPresentationPtr>()->scene);
        auto& mixed_run = mixed->slides.front().shapes.front().text.paragraphs.front().runs.front();
        mixed_run.east_asian_font_family = candidates[1].toStdString();
        mirrorfly::SlideRenderer mixed_renderer;
        mixed_renderer.setDocument(QVariant::fromValue(mirrorfly::prepare_presentation(mixed)));
        mixed_renderer.setWidth(960);
        mixed_renderer.setHeight(540);
        mixed_renderer.setSelectedShape(0);
        passed = check(mixed_renderer.textEditorState().value("fontFamily") == candidates[1],
                     "explicit East Asian family takes precedence for Chinese inline text") &&
            passed;
        return passed;
    }

    bool test_template_text_bounds()
    {
        bool passed = true;
        for (const auto layout : {mirrorfly::PresentationSlideLayout::ResearchStudio,
                 mirrorfly::PresentationSlideLayout::EvidenceBoard,
                 mirrorfly::PresentationSlideLayout::ProjectDashboard,
                 mirrorfly::PresentationSlideLayout::DeliveryRoadmap})
        {
            const auto scene =
                std::make_shared<mirrorfly::PresentationScene>(mirrorfly::make_presentation(layout));
            mirrorfly::SlideRenderer renderer;
            renderer.setWidth(960);
            renderer.setHeight(540);
            renderer.setDocument(QVariant::fromValue(mirrorfly::prepare_presentation(scene)));
            for (std::size_t index = 0; index < scene->slides.front().shapes.size(); ++index)
            {
                const auto& shape = scene->slides.front().shapes[index];
                if (shape.text.paragraphs.empty())
                    continue;
                renderer.setSelectedShape(static_cast<int>(index));
                const auto state = renderer.textEditorState();
                QTextDocument document;
                document.setDocumentMargin(0);
                document.setDefaultFont(state.value("font").value<QFont>());
                document.setTextWidth(shape.width);
                QTextCursor cursor(&document);
                bool first = true;
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    if (!first)
                        cursor.insertBlock();
                    first = false;
                    QTextBlockFormat block;
                    block.setBottomMargin(paragraph.space_after);
                    cursor.setBlockFormat(block);
                    for (const auto& run : paragraph.runs)
                        cursor.insertText(QString::fromStdString(run.text));
                }
                if (document.size().height() > shape.height + 0.5)
                {
                    std::cerr << "Template text overflow: " << shape.name << " needs "
                              << document.size().height() << " has " << shape.height << '\n';
                    passed = false;
                }
            }
            QImage image(960, 540, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            renderer.setSelectedShape(-1);
            renderer.paint(&painter);
            painter.end();
            passed = check(image.pixelColor(2, 2).alpha() == 255,
                         "template paints to an isolated in-memory surface") &&
                passed;
        }
        return passed;
    }

    bool test_click_navigation_surface()
    {
        using namespace mirrorfly;
        auto scene = std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Blank));
        scene->width = 200;
        scene->height = 100;
        scene->slides.push_back(scene->slides.front());
        PresentationShape shape;
        shape.width = 100;
        shape.height = 40;
        shape.transform[4] = 20;
        shape.transform[5] = 20;
        shape.click_action.kind = "slide";
        shape.click_action.target_slide = 1;
        scene->slides[0].shapes.push_back(shape);
        SlideRenderer surface;
        surface.setSize(QSizeF(200, 100));
        surface.setDocument(QVariant::fromValue(prepare_presentation(scene)));
        const auto navigation = surface.clickNavigationAt(30, 30, -1);
        bool passed = check(navigation.value("kind") == "slide" && navigation.value("target") == 1,
            "slide canvas resolves a clicked internal jump");
        passed = check(surface.clickNavigationAt(150, 80, -1).isEmpty(),
                     "clicking outside linked shape leaves normal slide advance") &&
            passed;
        const auto tree = presentation_semantic_tree(*scene, 0, 0);
        const auto nodes = tree.value("nodes").toList();
        bool found = false;
        for (const auto& value : nodes)
        {
            const auto node = value.toMap();
            if (node.value("type") == "shape" && node.value("clickAction").toMap().value("targetSlide") == 1)
                found = true;
        }
        passed = check(found, "semantic tree exposes the in-presentation click target") && passed;
        return passed;
    }

    bool test_placeholder_semantics()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.native_editable = true;
        PresentationShape shape;
        shape.id = 501;
        shape.editable = true;
        shape.source_id = "2";
        shape.source_part = "ppt/slides/slide1.xml";
        shape.effects_source = "theme";
        shape.theme_effect_style_index = 2;
        PresentationPlaceholderInfo placeholder;
        placeholder.type = "body";
        placeholder.index = 1;
        placeholder.has_layout = true;
        placeholder.local_fill_override = true;
        placeholder.fill_source = "slide";
        placeholder.inherited_fill_source = "layout";
        placeholder.local_outline_override = true;
        placeholder.outline_source = "slide";
        placeholder.inherited_outline_source = "layout";
        shape.placeholder = placeholder;
        mirrorfly::PresentationParagraph paragraph;
        mirrorfly::PresentationRun run;
        run.text = "Source";
        run.font_family_source = "master";
        run.font_size_source = "layout";
        run.color_source = "slide";
        run.local_color_override = true;
        paragraph.runs.push_back(run);
        shape.text.paragraphs.push_back(paragraph);
        scene.slides.front().shapes.push_back(shape);
        const auto tree = presentation_semantic_tree(scene, 0, 0);
        const auto nodes = tree.value("nodes").toList();
        bool found = false;
        for (const auto& value : nodes)
        {
            const auto node = value.toMap();
            const auto info = node.value("placeholder").toMap();
            const auto text_sources = node.value("textStyleSources").toMap();
            const auto local_text = node.value("textLocalOverrides").toMap();
            const auto effect_style = node.value("effectStyle").toMap();
            if (node.value("sourceId") == QStringLiteral("2") &&
                info.value("fillSource") == QStringLiteral("slide") &&
                info.value("inheritedFillSource") == QStringLiteral("layout") &&
                info.value("outlineSource") == QStringLiteral("slide") &&
                info.value("inheritedOutlineSource") == QStringLiteral("layout") &&
                text_sources.value("fontFamily") == QStringLiteral("master") &&
                text_sources.value("fontSize") == QStringLiteral("layout") &&
                text_sources.value("color") == QStringLiteral("slide") &&
                text_sources.value("scope") == QStringLiteral("firstRun") &&
                local_text.value("color").toBool() &&
                effect_style.value("source") == QStringLiteral("theme") &&
                effect_style.value("themeIndex").toInt() == 2 &&
                !effect_style.value("directOverride").toBool() &&
                node.value("actions").toList().contains(QStringLiteral("resetTextInheritance")) &&
                node.value("actions").toList().contains(QStringLiteral("resetPlaceholderFill")) &&
                node.value("actions").toList().contains(QStringLiteral("resetPlaceholderOutline")))
                found = true;
        }
        return check(found, "semantic tree exposes placeholder fill and outline provenance");
    }

    bool test_group_ungroup_semantics()
    {
        using namespace mirrorfly;
        auto scene = make_presentation(PresentationSlideLayout::Blank);
        scene.native_editable = true;
        PresentationGroupFrame frame;
        frame.source_id = "rotated-picture-group";
        frame.source_part = "ppt/slides/slide1.xml";
        frame.editable = true;
        frame.ungroupable = true;
        scene.slides[0].source_part = frame.source_part;
        scene.slides[0].groups.push_back(frame);
        PresentationShape picture;
        picture.id = 91;
        picture.source_id = "picture-91";
        picture.source_part = frame.source_part;
        picture.source_groups = {frame.source_id};
        scene.slides[0].shapes.push_back(picture);
        bool exposed = false;
        for (const auto& value : presentation_semantic_tree(scene, 0, 0).value("nodes").toList())
        {
            const auto node = value.toMap();
            if (node.value("type") == QStringLiteral("group") &&
                node.value("groupId") == QStringLiteral("rotated-picture-group") &&
                node.value("actions").toList().contains(QStringLiteral("ungroup")))
                exposed = true;
        }
        return check(exposed, "semantic group node offers ungroup for a safe rotated picture group");
    }

    bool test_group_extension_bridge()
    {
        using namespace mirrorfly;
        PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.applyEdit(QStringLiteral("addShape"),
                                {{QStringLiteral("geometry"), QStringLiteral("rect")},
                                    {QStringLiteral("x"), 510}, {QStringLiteral("y"), 220},
                                    {QStringLiteral("width"), 90}, {QStringLiteral("height"), 65}}) &&
                wait_until_idle(editor),
            "bridge adds a third shape for group extension");
        editor.selectShape(0);
        passed =
            check(editor.applyEdit(QStringLiteral("groupAdjacent"), {{QStringLiteral("targetIndex"), 1}}) &&
                    wait_until_idle(editor),
                "bridge creates a two-member group") &&
            passed;
        editor.selectShape(0);
        const auto selection = editor.selection();
        const auto group_id = selection.value(QStringLiteral("groupId")).toString();
        passed = check(!group_id.isEmpty() && selection.value(QStringLiteral("groupExtensible")).toBool() &&
                         selection.value(QStringLiteral("groupNextIndex")).toInt() == 2 &&
                         selection.value(QStringLiteral("actions"))
                             .toList()
                             .contains(QStringLiteral("addToGroup")),
                     "selection offers the adjacent target for a simple group") &&
            passed;
        bool tree_action = false;
        for (const auto& item : editor.semanticTree(0, 0).value("nodes").toList())
        {
            const auto node = item.toMap();
            tree_action = tree_action ||
                (node.value("type") == QStringLiteral("group") && node.value("groupId") == group_id &&
                    node.value("extensible").toBool() &&
                    node.value("actions").toList().contains(QStringLiteral("addToGroup")) &&
                    node.value("adjacentTargets").toList().contains(2));
        }
        passed = check(tree_action, "semantic group advertises the same adjacent target") && passed;
        passed = check(editor.applyEdit(QStringLiteral("addToGroup"),
                           {{QStringLiteral("groupId"), group_id}, {QStringLiteral("targetIndex"), 2}}) &&
                         editor.pendingEdits() == 1 && wait_until_idle(editor),
                     "group extension reports asynchronous synchronization") &&
            passed;
        editor.selectShape(2);
        passed = check(editor.selection().value(QStringLiteral("groupId")) == group_id,
                     "third shape is now part of the selected group") &&
            passed;
        editor.undo();
        editor.selectShape(2);
        passed = check(wait_until_idle(editor) &&
                         editor.selection().value(QStringLiteral("groupId")).toString().isEmpty(),
                     "group extension participates in undo history") &&
            passed;
        editor.redo();
        editor.selectShape(2);
        passed =
            check(wait_until_idle(editor) && editor.selection().value(QStringLiteral("groupId")) == group_id,
                "group extension participates in redo history") &&
            passed;
        return passed;
    }

    bool test_group_layer_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        bool passed =
            check(editor.applyEdit(QStringLiteral("addShape"),
                      {{"geometry", "rect"}, {"x", 510}, {"y", 220}, {"width", 90}, {"height", 65}}) &&
                    wait_until_idle(editor),
                "group layer bridge adds a third shape");
        editor.selectShape(0);
        passed = check(editor.applyEdit(QStringLiteral("groupAdjacent"), {{"targetIndex", 1}}) &&
                         wait_until_idle(editor),
                     "group layer bridge forms an editable group") &&
            passed;
        editor.selectShape(0);
        const auto id = editor.selection().value("groupId").toString();
        const auto selected_source = [&editor]()
        {
            for (const auto& item : editor.semanticTree(0, 0).value("nodes").toList())
            {
                const auto node = item.toMap();
                if (node.contains("index") && node.value("index").toInt() == editor.selectedShape() &&
                    node.value("type") != QStringLiteral("group"))
                    return node.value("sourceId").toString();
            }
            return QString{};
        };
        const auto original_source = selected_source();
        const auto before = editor.selection().value("groupLayerOptions").toMap();
        passed = check(!id.isEmpty() && before.value("forward").toBool() &&
                         !before.value("backward").toBool() && editor.editSchema().contains("reorderGroup"),
                     "GUI selection and AI schema expose group layer capability") &&
            passed;
        if (id.isEmpty())
            return false;
        passed = check(editor.applyEdit(
                           QStringLiteral("reorderGroup"), {{"groupId", id}, {"position", "front"}}) &&
                         editor.pendingEdits() == 1 && wait_until_idle(editor),
                     "group layer edit reports asynchronous progress") &&
            passed;
        bool semantic_front = false;
        for (const auto& item : editor.semanticTree(0, 0).value("nodes").toList())
        {
            const auto node = item.toMap();
            if (node.value("type") == QStringLiteral("group") && node.value("groupId") == id)
                semantic_front = node.value("layerIndex").toInt() == 1 &&
                    node.value("layerOptions").toMap().value("backward").toBool() &&
                    node.value("actions").toList().contains("reorderGroup");
        }
        passed = check(semantic_front && editor.selection().value("groupId") == id &&
                         !original_source.isEmpty() && selected_source() == original_source,
                     "semantic group layer and restored selection match committed document") &&
            passed;
        editor.undo();
        passed = check(wait_until_idle(editor) && selected_source() == original_source &&
                         editor.selection().value("groupLayerOptions").toMap().value("forward").toBool(),
                     "group layer edit supports undo") &&
            passed;
        editor.redo();
        passed = check(wait_until_idle(editor) && selected_source() == original_source &&
                         editor.selection().value("groupLayerOptions").toMap().value("backward").toBool(),
                     "group layer edit supports redo") &&
            passed;
        return passed;
    }

    bool test_table_style_bridge()
    {
        using namespace mirrorfly;
        PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.applyEdit(QStringLiteral("insertTable"),
                                {{QStringLiteral("rows"), 3}, {QStringLiteral("columns"), 3}}) &&
                wait_until_idle(editor),
            "bridge creates a theme-styled table");
        const auto cell = [&editor](int row, int column)
        {
            for (const auto& value : editor.semanticTree(0, 0).value("nodes").toList())
            {
                const auto node = value.toMap();
                if (node.value("type") == QStringLiteral("tableCell") && node.value("row").toInt() == row &&
                    node.value("column").toInt() == column)
                    return node;
            }
            return QVariantMap{};
        };
        const auto initial = cell(0, 0);
        if (initial.isEmpty())
            return check(false, "table style semantic cell exists") && passed;
        editor.selectShape(initial.value("index").toInt());
        const auto selected = editor.selection();
        passed = check(selected.value("tableStyleAvailable").toBool() &&
                         selected.value("tableStyleOptions").toMap().value("firstRow").toBool() &&
                         initial.value("actions").toList().contains(QStringLiteral("formatTableStyle")),
                     "selection and semantic tree offer linked whole-table style options") &&
            passed;
        passed = check(editor.applyEdit(QStringLiteral("formatTableCell"),
                           {{QStringLiteral("fillColor"), QStringLiteral("#CC5500")}}) &&
                         wait_until_idle(editor),
                     "bridge applies direct cell fill before whole-table restyling") &&
            passed;
        editor.selectShape(cell(0, 0).value("index").toInt());
        QVariantMap style{{"firstRow", false}, {"lastRow", true}, {"firstColumn", true},
            {"lastColumn", false}, {"bandRows", false}, {"bandColumns", true}};
        passed = check(editor.applyEdit(QStringLiteral("formatTableStyle"), {{"style", style}}) &&
                         editor.pendingEdits() == 1 && wait_until_idle(editor),
                     "whole-table style reports one asynchronous edit") &&
            passed;
        const auto updated = cell(0, 0);
        passed = check(updated.value("tableStyleOptions").toMap() == style &&
                         updated.value("tableCellStyle").toMap().value("localFillOverride").toBool() &&
                         cell(2, 2).value("tableStyleOptions").toMap() == style &&
                         editor.selection().value("tableStyleOptions").toMap() == style,
                     "one table style edit updates all cells and preserves direct fill") &&
            passed;
        editor.undo();
        passed = check(wait_until_idle(editor) &&
                         cell(0, 0).value("tableStyleOptions").toMap().value("firstRow").toBool(),
                     "whole-table style participates in undo history") &&
            passed;
        editor.redo();
        passed = check(wait_until_idle(editor) && cell(0, 0).value("tableStyleOptions").toMap() == style,
                     "whole-table style participates in redo history") &&
            passed;
        return passed;
    }

    bool test_table_fill_inheritance_bridge()
    {
        using namespace mirrorfly;
        PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.applyEdit(QStringLiteral("insertTable"),
                                {{QStringLiteral("rows"), 2}, {QStringLiteral("columns"), 2}}) &&
                wait_until_idle(editor),
            "bridge creates table for fill inheritance");
        const auto first_cell = [&editor]()
        {
            for (const auto& item : editor.semanticTree(0, 0).value("nodes").toList())
            {
                const auto node = item.toMap();
                if (node.value("type") == QStringLiteral("tableCell") && node.value("row").toInt() == 0 &&
                    node.value("column").toInt() == 0)
                    return node;
            }
            return QVariantMap{};
        };
        const auto initial = first_cell();
        if (initial.isEmpty())
            return check(false, "table fill inheritance semantic cell exists") && passed;
        editor.selectShape(initial.value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("formatTableCell"),
                           {{QStringLiteral("fillColor"), QStringLiteral("#CC5500")}}) &&
                         wait_until_idle(editor),
                     "bridge sets a direct table cell fill") &&
            passed;
        const auto overridden = first_cell();
        passed = check(overridden.value("tableCellStyle").toMap().value("localFillOverride").toBool() &&
                         overridden.value("actions").toList().contains(QStringLiteral("resetTableCellFill")),
                     "semantic cell exposes direct fill and reset capability") &&
            passed;
        editor.selectShape(overridden.value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("resetTableCellFill")) && wait_until_idle(editor),
                     "bridge restores table cell fill after asynchronous synchronization") &&
            passed;
        const auto restored = first_cell();
        passed = check(!restored.value("tableCellStyle").toMap().value("localFillOverride").toBool() &&
                         !restored.value("actions").toList().contains(QStringLiteral("resetTableCellFill")),
                     "semantic cell clears reset capability after restoration") &&
            passed;
        editor.undo();
        passed =
            check(wait_until_idle(editor) &&
                    first_cell().value("actions").toList().contains(QStringLiteral("resetTableCellFill")),
                "table fill restoration is undoable through the bridge") &&
            passed;
        editor.selectShape(first_cell().value("index").toInt());
        passed =
            check(editor.applyEdit(QStringLiteral("formatTableBorder"),
                      {{QStringLiteral("color"), QStringLiteral("#335577")}, {QStringLiteral("width"), 2}}) &&
                    wait_until_idle(editor),
                "bridge formats the selected table border") &&
            passed;
        const auto bordered = first_cell();
        const auto style = bordered.value("tableCellStyle").toMap();
        passed = check(style.value("localBorderOverride").toBool() &&
                         style.value("uniformBorderColor") == QStringLiteral("#335577") &&
                         style.value("uniformBorderWidth").toDouble() == 2 &&
                         bordered.value("actions").toList().contains(QStringLiteral("resetTableBorder")),
                     "semantic cell exposes direct border and current uniform color") &&
            passed;
        editor.selectShape(bordered.value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("resetTableBorder")) && editor.syncing() &&
                         editor.pendingEdits() == 1 && wait_until_idle(editor),
                     "bridge restores table border through asynchronous progress") &&
            passed;
        const auto inherited_border = first_cell();
        passed =
            check(!inherited_border.value("tableCellStyle").toMap().value("localBorderOverride").toBool() &&
                    !inherited_border.value("actions").toList().contains(
                        QStringLiteral("resetTableBorder")) &&
                    inherited_border.value("tableCellStyle").toMap().value("localFillOverride").toBool(),
                "border reset preserves fill override and clears its own capability") &&
            passed;
        editor.undo();
        passed = check(wait_until_idle(editor) &&
                         first_cell().value("actions").toList().contains(QStringLiteral("resetTableBorder")),
                     "table border restoration is undoable through the bridge") &&
            passed;
        editor.selectShape(first_cell().value("index").toInt());
        passed =
            check(editor.applyEdit(QStringLiteral("formatTableBorder"),
                      {{QStringLiteral("color"), QStringLiteral("#AA5500")}, {QStringLiteral("width"), 3},
                          {QStringLiteral("edge"), QStringLiteral("left")}}) &&
                    wait_until_idle(editor),
                "bridge formats only the left table edge") &&
            passed;
        const auto separate_edges = first_cell().value("tableCellStyle").toMap().value("edges").toMap();
        passed = check(separate_edges.value("left").toMap().value("color") == QStringLiteral("#AA5500") &&
                         separate_edges.value("right").toMap().value("color") == QStringLiteral("#335577"),
                     "semantic tree reports independent table edge colors") &&
            passed;
        editor.selectShape(first_cell().value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("resetTableBorder"),
                           {{QStringLiteral("edge"), QStringLiteral("left")}}) &&
                         wait_until_idle(editor),
                     "bridge restores only the left table edge") &&
            passed;
        const auto remaining_edges = first_cell().value("tableCellStyle").toMap().value("edges").toMap();
        passed = check(!remaining_edges.value("left").toMap().value("localOverride").toBool() &&
                         remaining_edges.value("right").toMap().value("localOverride").toBool(),
                     "resetting left edge preserves other direct edges") &&
            passed;
        return passed;
    }

    bool test_table_text_inheritance_bridge()
    {
        using namespace mirrorfly;
        PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.applyEdit(QStringLiteral("insertTable"),
                                {{QStringLiteral("rows"), 2}, {QStringLiteral("columns"), 2}}) &&
                wait_until_idle(editor),
            "bridge creates table for text inheritance");
        const auto cell = [&editor]()
        {
            for (const auto& item : editor.semanticTree(0, 0).value("nodes").toList())
            {
                const auto node = item.toMap();
                if (node.value("type") == QStringLiteral("tableCell") && node.value("row").toInt() == 0 &&
                    node.value("column").toInt() == 0)
                    return node;
            }
            return QVariantMap{};
        };
        const auto initial = cell();
        if (initial.isEmpty())
            return check(false, "table text inheritance semantic cell exists") && passed;
        editor.selectShape(initial.value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("formatText"),
                           {{QStringLiteral("fontSize"), 31},
                               {QStringLiteral("textColor"), QStringLiteral("#AA5500")}}) &&
                         wait_until_idle(editor),
                     "bridge formats selected table cell text") &&
            passed;
        const auto overridden = cell();
        const auto local = overridden.value("textLocalOverrides").toMap();
        passed =
            check(local.value("fontSize").toBool() && local.value("color").toBool() &&
                    overridden.value("actions").toList().contains(QStringLiteral("resetTextInheritance")) &&
                    overridden.value("textStyleSources").toMap().value("fontSize") ==
                        QStringLiteral("tableCell"),
                "semantic cell exposes direct text sources and reset capability") &&
            passed;
        editor.selectShape(overridden.value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("resetTextInheritance"),
                           {{QStringLiteral("property"), QStringLiteral("color")}}) &&
                         editor.pendingEdits() == 1 && wait_until_idle(editor),
                     "table cell text reset reports asynchronous progress") &&
            passed;
        const auto color_reset = cell().value("textLocalOverrides").toMap();
        passed = check(!color_reset.value("color").toBool() && color_reset.value("fontSize").toBool(),
                     "table cell color reset preserves direct size") &&
            passed;
        editor.undo();
        passed = check(wait_until_idle(editor) &&
                         cell().value("textLocalOverrides").toMap().value("color").toBool(),
                     "table cell text reset participates in undo history") &&
            passed;
        editor.redo();
        passed = check(wait_until_idle(editor) &&
                         !cell().value("textLocalOverrides").toMap().value("color").toBool(),
                     "table cell text reset participates in redo history") &&
            passed;
        return passed;
    }

    bool test_table_structure_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.applyEdit(QStringLiteral("insertTable"),
                                {{QStringLiteral("rows"), 2}, {QStringLiteral("columns"), 2}}) &&
                wait_until_idle(editor),
            "bridge inserts a table before structural edits");
        const auto first_cell = [&editor]()
        {
            for (const auto& item : editor.semanticTree(0, 0).value("nodes").toList())
            {
                const auto node = item.toMap();
                if (node.value("type") == QStringLiteral("tableCell") && node.value("row").toInt() == 0 &&
                    node.value("column").toInt() == 0)
                    return node;
            }
            return QVariantMap{};
        };
        auto node = first_cell();
        const auto schema = mirrorfly::presentation_edit_schema();
        passed = check(schema.value("insertTableRow").toMap().value("availability") ==
                             QStringLiteral("selected tableStructureOptions.insertRow") &&
                         schema.value("mergeTableCell").toMap().value("availability") ==
                             QStringLiteral("selected tableStructureOptions.mergeRight or mergeDown"),
                     "AI edit schema describes the same merge-aware capabilities as the GUI") &&
            passed;
        passed = check(node.value("tableRowCount") == 2 && node.value("tableColumnCount") == 2 &&
                         !node.value("tableHasMerges").toBool() &&
                         node.value("actions").toList().contains(QStringLiteral("deleteTableRow")) &&
                         node.value("actions").toList().contains(QStringLiteral("deleteTableColumn")),
                     "semantic tree exposes table dimensions and both delete capabilities") &&
            passed;
        if (node.isEmpty())
            return false;
        editor.selectShape(node.value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("deleteTableRow")) && wait_until_idle(editor) &&
                         first_cell().value("tableRowCount") == 1,
                     "GUI action deletes a row after asynchronous package synchronization") &&
            passed;
        editor.undo();
        passed = check(wait_until_idle(editor) && first_cell().value("tableRowCount") == 2,
                     "table row deletion is undoable through the bridge") &&
            passed;
        editor.selectShape(first_cell().value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("deleteTableColumn")) && wait_until_idle(editor) &&
                         first_cell().value("tableColumnCount") == 1,
                     "GUI action deletes a column after asynchronous package synchronization") &&
            passed;
        editor.undo();
        passed = check(wait_until_idle(editor) && first_cell().value("tableColumnCount") == 2,
                     "table column deletion is undoable through the bridge") &&
            passed;
        editor.selectShape(first_cell().value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("mergeTableCell"), {{"direction", "right"}}) &&
                         wait_until_idle(editor),
                     "GUI bridge merges two adjacent table cells") &&
            passed;
        node = first_cell();
        passed = check(node.value("tableHasMerges").toBool() && node.value("tableColumnSpan").toInt() == 2 &&
                         node.value("tableUnmergeable").toBool() &&
                         node.value("actions").toList().contains(QStringLiteral("unmergeTableCell")),
                     "AI semantic tree advertises safe table unmerge") &&
            passed;
        const auto merged_structure = node.value("tableStructureOptions").toMap();
        passed = check(merged_structure.value("insertRow").toBool() &&
                         !merged_structure.value("insertColumn").toBool() &&
                         !merged_structure.value("deleteRow").toBool() &&
                         !merged_structure.value("mergeRight").toBool() &&
                         editor.selection().value("tableStructureOptions").toMap() == merged_structure,
                     "GUI selection and AI tree agree on structure actions around a merge") &&
            passed;
        QVariantMap clear_row;
        for (const auto& item : editor.semanticTree(0, 0).value("nodes").toList())
        {
            const auto candidate = item.toMap();
            if (candidate.value("type") == QStringLiteral("tableCell") &&
                candidate.value("row").toInt() == 1 && candidate.value("column").toInt() == 0)
                clear_row = candidate;
        }
        passed = check(clear_row.value("tableStructureOptions").toMap().value("mergeRight").toBool() &&
                         clear_row.value("actions").toList().contains(QStringLiteral("mergeTableCell")),
                     "unaffected row still advertises a safe second merge") &&
            passed;
        editor.selectShape(node.value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("unmergeTableCell")) && editor.syncing() &&
                         editor.pendingEdits() == 1 && wait_until_idle(editor) &&
                         !first_cell().value("tableHasMerges").toBool(),
                     "unmerge follows frontend synchronization and refreshes the semantic tree") &&
            passed;
        editor.undo();
        passed = check(first_cell().value("tableUnmergeable").toBool(),
                     "table unmerge is undoable through the bridge") &&
            passed;
        editor.redo();
        node = first_cell();
        passed = check(!node.value("tableHasMerges").toBool() &&
                         node.value("actions").toList().contains(QStringLiteral("deleteTableRow")),
                     "redo restores normal table structure capabilities") &&
            passed;
        editor.selectShape(node.value("index").toInt());
        passed = check(editor.applyEdit(QStringLiteral("deleteTableRow")) && wait_until_idle(editor) &&
                         first_cell().value("tableRowCount") == 1 && editor.error().isEmpty(),
                     "editing after redo commits against the restored table state") &&
            passed;
        return passed;
    }

    bool test_section_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.sectionsEditable() && editor.slideSections().isEmpty(),
            "new presentation exposes writable section state");
        passed = check(editor.applyEdit(QStringLiteral("addSlide"), {{"layout", "blank"}}) &&
                         editor.slideCount() == 2,
                     "prepare the second slide for a section") &&
            passed;
        editor.setSlide(1);
        passed = check(editor.applyEdit(QStringLiteral("createSection"), {{"name", "课程内容"}}) &&
                         editor.slideSections().size() == 2,
                     "bridge creates a named section through the public edit command") &&
            passed;
        const auto tree = editor.semanticTree(1, 0);
        bool section_node = false;
        for (const auto& value : tree.value("nodes").toList())
            if (value.toMap().value("type") == QStringLiteral("section") &&
                value.toMap().value("name") == QStringLiteral("课程内容"))
                section_node = true;
        passed = check(section_node && editor.snapshot().value("slideSections").toList().size() == 2,
                     "AI tree and snapshot expose the same section state") &&
            passed;
        passed = check(editor.applyEdit(QStringLiteral("renameSection"), {{"name", "重点讲解"}}) &&
                         editor.syncing() && editor.pendingEdits() == 1,
                     "section rename reports pending frontend synchronization") &&
            passed;
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.slideSections().last().toMap().value("name") == QStringLiteral("重点讲解") &&
                         !editor.snapshot().value("syncing").toBool(),
                     "committed section name reaches the frontend and AI snapshot") &&
            passed;
        editor.undo();
        passed = check(editor.slideSections().last().toMap().value("name") == QStringLiteral("课程内容"),
                     "section rename participates in undo history") &&
            passed;
        return passed;
    }

    bool test_click_action_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.editSchema().contains("setClickAction"),
            "AI edit schema declares whole-object slide-show links");
        passed = check(editor.applyEdit(QStringLiteral("addSlide"), {{"layout", "blank"}}),
                     "prepare hyperlink destination") &&
            passed;
        editor.setSlide(0);
        editor.selectShape(0);
        passed = check(editor.applyEdit(
                           QStringLiteral("setClickAction"), {{"kind", "slide"}, {"targetSlide", 1}}) &&
                         editor.selection().value("clickAction").toMap().value("targetSlide") == 1,
                     "GUI and automation share the slide link command") &&
            passed;
        const auto tree = editor.semanticTree(0, 0);
        bool linked_node = false;
        for (const auto& value : tree.value("nodes").toList())
            if (value.toMap().value("clickAction").toMap().value("targetSlide") == 1)
                linked_node = true;
        passed = check(linked_node, "semantic tree exposes the authored link") && passed;
        passed = check(editor.applyEdit(QStringLiteral("setClickAction"), {{"kind", "nextslide"}}) &&
                         editor.syncing() && editor.pendingEdits() == 1,
                     "subsequent link edits expose pending GUI synchronization") &&
            passed;
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.selection().value("clickAction").toMap().value("kind") ==
                         QStringLiteral("nextslide"),
                     "committed action reaches GUI selection") &&
            passed;
        editor.undo();
        passed =
            check(editor.selection().value("clickAction").toMap().value("kind") == QStringLiteral("slide"),
                "link edits participate in undo history") &&
            passed;
        return passed;
    }

    bool test_format_brush_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        editor.selectShape(0);
        const auto source_id = editor.selection().value("id").toString();
        bool passed = check(editor.editSchema().contains("applyFormat") && !source_id.isEmpty(),
            "AI schema exposes format brush with an opaque source ID");
        passed = check(editor.applyEdit(QStringLiteral("formatShape"),
                           {{"fillColor", "#A12542"}, {"outlineColor", "#2468A0"}}),
                     "style the format brush source") &&
            passed;
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.applyEdit(QStringLiteral("addShape"), {{"geometry", "ellipse"}}),
                     "create a format brush target") &&
            passed;
        passed = wait_until_idle(editor) && passed;
        const int target_index = editor.semanticTree(0, 0).value("totalObjects").toInt() - 1;
        editor.selectShape(target_index);
        const auto target_id = editor.selection().value("id").toString();
        const auto original_fill = editor.selection().value("fillColor").toString();
        const auto generation_before = editor.snapshot().value("generation").toString();
        const bool applied = editor.applyEdit(QStringLiteral("applyFormat"), {{"sourceId", source_id}});
        if (!applied)
            std::cerr << "Format brush bridge: " << editor.error().toStdString() << '\n';
        passed =
            check(target_id != source_id && applied, "format brush applies through the bridge") && passed;
        passed = check(editor.selection().value("fillColor").toString() == QStringLiteral("#A12542"),
                     "format brush updates target fill") &&
            passed;
        passed = check(editor.snapshot().value("generation").toString() != generation_before &&
                         (editor.syncing() ? editor.pendingEdits() > 0 : editor.pendingEdits() == 0),
                     "format brush exposes a coherent frontend generation and sync state") &&
            passed;
        passed = wait_until_idle(editor) && passed;
        bool action_exposed = false;
        for (const auto& value : editor.semanticTree(0, 0).value("nodes").toList())
            if (value.toMap().value("id").toString() == target_id &&
                value.toMap().value("actions").toStringList().contains("applyFormat"))
                action_exposed = true;
        passed = check(action_exposed && !editor.snapshot().value("syncing").toBool(),
                     "semantic tree exposes the format brush capability after synchronization") &&
            passed;
        editor.undo();
        passed = check(editor.selection().value("fillColor").toString() == original_fill,
                     "one undo restores the target's previous formatting") &&
            passed;
        passed = check(!editor.applyEdit(QStringLiteral("applyFormat"), {{"sourceId", "missing"}}),
                     "stale or unknown format brush source is rejected") &&
            passed;
        return passed;
    }

    bool test_find_replace_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.editSchema().contains("replaceTextMatches"),
            "AI edit schema describes text search and replacement");
        passed = check(editor.applyEdit(QStringLiteral("addText"),
                           {{"text", "Study study"}, {"width", 200}, {"height", 50}}),
                     "create bridge search fixture") &&
            passed;
        passed = wait_until_idle(editor) && passed;
        const auto found = editor.findText(QStringLiteral("study"), false, 0);
        const auto nodes = found.value("nodes").toList();
        passed = check(found.value("ok").toBool() && found.value("total").toInt() == 2 && nodes.size() == 2 &&
                         nodes[0].toMap().value("replaceable").toBool() &&
                         found.value("offsetUnit").toString() == QStringLiteral("utf8Bytes"),
                     "bridge search returns pageable semantic matches") &&
            passed;
        if (nodes.size() != 2)
            return false;
        const auto match = nodes[0].toMap();
        QVariantMap options{{"query", "study"}, {"replacement", "Lesson"}, {"scope", "match"},
            {"caseSensitive", false}, {"expectedGeneration", found.value("generation")},
            {"shapeId", match.value("shapeId")}, {"slideIndex", match.value("slideIndex")},
            {"shapeIndex", match.value("shapeIndex")}, {"paragraphIndex", match.value("paragraphIndex")},
            {"startByte", match.value("startByte")}};
        passed = check(editor.applyEdit(QStringLiteral("replaceTextMatches"), options),
                     "replace a semantic match through the shared bridge") &&
            passed;
        passed = check(editor.findText(QStringLiteral("study"), false, 0).value("total").toInt() == 1 &&
                         editor.snapshot().value("generation") != found.value("generation"),
                     "frontend search and generation follow optimistic edit") &&
            passed;
        passed = check(!editor.applyEdit(QStringLiteral("replaceTextMatches"), options),
                     "stale search generation is rejected") &&
            passed;
        passed = wait_until_idle(editor) && passed;
        const auto remaining = editor.findText(QStringLiteral("study"), false, 0);
        passed = check(editor.applyEdit(QStringLiteral("replaceTextMatches"),
                           {{"query", "study"}, {"replacement", "Done"}, {"scope", "all"},
                               {"expectedGeneration", remaining.value("generation")}}),
                     "replace all uses one document transaction") &&
            passed;
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.findText(QStringLiteral("study"), false, 0).value("total").toInt() == 0 &&
                         !editor.snapshot().value("syncing").toBool(),
                     "committed replacement updates AI and GUI search state") &&
            passed;
        editor.undo();
        passed = check(editor.findText(QStringLiteral("study"), false, 0).value("total").toInt() == 1,
                     "undo restores the last replacement") &&
            passed;
        return passed;
    }

    bool test_guide_settings_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        const auto original = editor.guideSettings();
        bool passed = check(!original.value("showGrid").toBool() &&
                original.value("scope").toString() == QStringLiteral("documentSession"),
            "canvas aids start disabled and expose their session scope");
        const auto generation = editor.snapshot().value("generation");
        const QVariantList vertical{120.0, 240.0};
        passed =
            check(editor.setGuideSettings({{"showRulers", true}, {"showGrid", true}, {"showGuides", true},
                      {"snapToGuides", true}, {"gridSpacingPt", 18.0}, {"verticalGuidesPt", vertical}}),
                "public UI bridge accepts bounded guide settings") &&
            passed;
        passed = check(editor.snapshot().value("guideSettings").toMap().value("verticalGuidesPt").toList() ==
                             vertical &&
                         editor.guideSettings().value("gridSpacingPt").toDouble() == 18.0 &&
                         editor.snapshot().value("generation") == generation && !editor.modified() &&
                         !editor.syncing(),
                     "guide changes reach the frontend without editing PPTX or scheduling a render job") &&
            passed;
        const auto stable = editor.guideSettings();
        passed = check(!editor.setGuideSettings({{"gridSpacingPt", 0.1}}) && editor.guideSettings() == stable,
                     "invalid grid spacing is rejected atomically") &&
            passed;
        passed = check(!editor.setGuideSettings({{"verticalGuidesPt", QVariantList{120.0, 120.1}}}) &&
                         editor.guideSettings() == stable,
                     "duplicate guide positions are rejected atomically") &&
            passed;
        editor.clear();
        editor.requestNew();
        passed = check(editor.guideSettings() == original,
                     "a new presentation gets fresh session guide settings") &&
            passed;
        return passed;
    }

    bool test_edit_history_and_source_protection()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        bool passed = check(editor.active() && editor.editable() && !editor.modified() &&
                editor.slideWidth() == 960 && editor.slideHeight() == 540 && editor.selectedShape() == 0,
            "new presentations begin as populated editable 16:9 documents");
        passed = check(editor.applyEdit(QStringLiteral("addShape"),
                           {{QStringLiteral("geometry"), QStringLiteral("ellipse")}}) &&
                         editor.modified() && editor.canUndo(),
                     "an edit creates an undoable modified snapshot") &&
            passed;
        passed =
            check(editor.applyEdit(QStringLiteral("transformShape"), {{QStringLiteral("rotation"), 90}}) &&
                    std::abs(editor.selection().value(QStringLiteral("rotation")).toDouble() - 90) < 1e-8,
                "bridge exposes object rotation through the shared edit interface") &&
            passed;
        passed =
            check(editor.applyEdit(
                      QStringLiteral("transformShape"), {{QStringLiteral("flipHorizontal"), true}}) &&
                    std::abs(editor.selection().value(QStringLiteral("rotation")).toDouble() + 90) < 1e-8,
                "bridge exposes object flip through the shared edit interface") &&
            passed;
        passed =
            check(editor.applyEdit(QStringLiteral("formatShape"),
                      {{QStringLiteral("gradientStartColor"), QStringLiteral("#FF0000")},
                          {QStringLiteral("gradientEndColor"), QStringLiteral("#0000FF")},
                          {QStringLiteral("gradientAngle"), 45}, {QStringLiteral("fillOpacity"), 0.7},
                          {QStringLiteral("outlineColor"), QStringLiteral("#112233")},
                          {QStringLiteral("outlineOpacity"), 0.6},
                          {QStringLiteral("lineDash"), QStringLiteral("dashDot")},
                          {QStringLiteral("lineHead"), QStringLiteral("oval")},
                          {QStringLiteral("lineTail"), QStringLiteral("triangle")},
                          {QStringLiteral("shadowEnabled"), true},
                          {QStringLiteral("shadowColor"), QStringLiteral("#223344")},
                          {QStringLiteral("shadowBlur"), 6}, {QStringLiteral("shadowX"), 4},
                          {QStringLiteral("shadowY"), 5}, {QStringLiteral("glowEnabled"), true},
                          {QStringLiteral("glowColor"), QStringLiteral("#4488FF")},
                          {QStringLiteral("glowRadius"), 7}}) &&
                    editor.selection().value(QStringLiteral("hasGradient")).toBool() &&
                    editor.selection().value(QStringLiteral("lineDash")).toString() ==
                        QStringLiteral("dashDot") &&
                    editor.selection().value(QStringLiteral("lineTail")).toString() ==
                        QStringLiteral("triangle") &&
                    editor.selection().value(QStringLiteral("shadowEnabled")).toBool() &&
                    editor.selection().value(QStringLiteral("glowEnabled")).toBool(),
                "bridge exposes gradient, line, arrow and effect formatting through one public action") &&
            passed;
        passed =
            check(editor.applyEdit(QStringLiteral("transformShape"),
                      {{QStringLiteral("width"), 480}, {QStringLiteral("preserveAspect"), true}}) &&
                    std::abs(editor.selection().value(QStringLiteral("width")).toDouble() - 480) < 1e-8 &&
                    std::abs(editor.selection().value(QStringLiteral("height")).toDouble() - 270) < 1e-8,
                "bridge exposes aspect-locked resizing through the shared transform action") &&
            passed;
        const int shape_count = editor.selection().value(QStringLiteral("shapeCount")).toInt();
        passed = check(editor.applyEdit(QStringLiteral("moveShape"), {{QStringLiteral("targetIndex"), 0}}) &&
                         editor.selection().value(QStringLiteral("index")).toInt() == 0 &&
                         editor.selection().value(QStringLiteral("shapeCount")).toInt() == shape_count,
                     "bridge moves a selected object directly to a requested layer") &&
            passed;
        QTemporaryDir directory;
        const QString saved_path = QDir(directory.path()).filePath(QStringLiteral("branch.pptx"));
        editor.save();
        editor.selectSaveFile(QUrl::fromLocalFile(saved_path));
        passed = wait_until_idle(editor) && passed;
        passed = check(!editor.modified() && QFileInfo::exists(saved_path),
                     "saving marks the exact current snapshot as persisted") &&
            passed;
        editor.undo();
        passed = check(editor.modified(), "undoing away from a saved snapshot is modified") && passed;
        passed = check(editor.applyEdit(QStringLiteral("addText"), {}) && editor.modified(),
                     "a new branch after undo does not reuse the saved snapshot identity") &&
            passed;
        passed =
            check(editor.applyEdit(QStringLiteral("formatText"),
                      {{QStringLiteral("strike"), true}, {QStringLiteral("characterSpacing"), 2.5},
                          {QStringLiteral("baseline"), 0.3}}) &&
                    editor.applyEdit(QStringLiteral("formatParagraph"),
                        {{QStringLiteral("alignment"), QStringLiteral("right")},
                            {QStringLiteral("bullet"), false}, {QStringLiteral("numbered"), true},
                            {QStringLiteral("numberStart"), 3}, {QStringLiteral("listLevel"), 2},
                            {QStringLiteral("paragraphIndex"), 0}, {QStringLiteral("marginLeft"), 24},
                            {QStringLiteral("firstLineIndent"), -12}, {QStringLiteral("lineSpacing"), 1.5},
                            {QStringLiteral("spaceBefore"), 6}, {QStringLiteral("spaceAfter"), 9}}) &&
                    editor.applyEdit(QStringLiteral("formatTextBox"),
                        {{QStringLiteral("insetLeft"), 12}, {QStringLiteral("insetRight"), 14},
                            {QStringLiteral("insetTop"), 8}, {QStringLiteral("insetBottom"), 10},
                            {QStringLiteral("verticalAlignment"), QStringLiteral("bottom")},
                            {QStringLiteral("wrap"), false}, {QStringLiteral("autoFit"), true}}) &&
                    editor.selection().value(QStringLiteral("strike")).toBool() &&
                    editor.selection().value(QStringLiteral("numbered")).toBool() &&
                    editor.selection().value(QStringLiteral("numberStart")).toInt() == 3 &&
                    editor.selection().value(QStringLiteral("listLevel")).toInt() == 2 &&
                    editor.paragraphInfo(0).value(QStringLiteral("listLevel")).toInt() == 2 &&
                    !editor.paragraphInfo(1).value(QStringLiteral("valid")).toBool() &&
                    editor.selection().value(QStringLiteral("verticalAlignment")).toString() ==
                        QStringLiteral("bottom") &&
                    !editor.selection().value(QStringLiteral("wrap")).toBool() &&
                    editor.selection().value(QStringLiteral("autoFit")).toBool(),
                "bridge exposes character paragraph and text-box edits for GUI and automation") &&
            passed;
        editor.undo();
        editor.undo();
        passed = check(editor.paragraphInfo(0).value(QStringLiteral("listLevel")).toInt() == 0,
                     "undo restores the previous paragraph level") &&
            passed;
        editor.redo();
        editor.redo();
        passed = check(editor.paragraphInfo(0).value(QStringLiteral("listLevel")).toInt() == 2 &&
                         editor.selection().value(QStringLiteral("verticalAlignment")).toString() ==
                             QStringLiteral("bottom"),
                     "redo restores the paragraph level and following text-box edit") &&
            passed;

        const QString first_image = QDir(directory.path()).filePath(QStringLiteral("first.png"));
        const QString second_image = QDir(directory.path()).filePath(QStringLiteral("second.png"));
        QImage first_pixels(8, 6, QImage::Format_ARGB32_Premultiplied);
        first_pixels.fill(QColor(Qt::red));
        QImage second_pixels(5, 9, QImage::Format_ARGB32_Premultiplied);
        second_pixels.fill(QColor(Qt::blue));
        passed = check(first_pixels.save(first_image) && second_pixels.save(second_image),
                     "image replacement fixture files are available") &&
            passed;
        editor.addImage(QUrl::fromLocalFile(first_image));
        passed = wait_until_idle(editor) && passed;
        passed = check(editor.selection().value(QStringLiteral("isImage")).toBool() &&
                         editor.applyEdit(QStringLiteral("formatImage"),
                             {{QStringLiteral("cropLeft"), 0.1}, {QStringLiteral("opacity"), 0.65}}),
                     "bridge inserts and formats an image before replacement") &&
            passed;
        const double image_width = editor.selection().value(QStringLiteral("width")).toDouble();
        const double image_height = editor.selection().value(QStringLiteral("height")).toDouble();
        editor.replaceImage(QUrl::fromLocalFile(second_image));
        passed = wait_until_idle(editor) && passed;
        passed =
            check(editor.selection().value(QStringLiteral("isImage")).toBool() &&
                    std::abs(editor.selection().value(QStringLiteral("width")).toDouble() - image_width) <
                        1e-8 &&
                    std::abs(editor.selection().value(QStringLiteral("height")).toDouble() - image_height) <
                        1e-8 &&
                    std::abs(editor.selection().value(QStringLiteral("imageCropLeft")).toDouble() - 0.1) <
                        1e-8 &&
                    std::abs(editor.selection().value(QStringLiteral("imageOpacity")).toDouble() - 0.65) <
                        1e-8,
                "asynchronous image replacement preserves the selected picture formatting") &&
            passed;

        mirrorfly::PresentationBridge imported;
        const QString source = QDir(QString::fromUtf8(MIRRORFLY_TEST_FIXTURE_DIRECTORY))
                                   .filePath(QStringLiteral("presentation-deflate.pptx"));
        {
            mirrorfly::PresentationBridge abandoned;
            abandoned.requestOpen(QUrl::fromLocalFile(source));
        }
        imported.requestOpen(QUrl::fromLocalFile(source));
        passed = wait_until_idle(imported) && passed;
        passed = check(imported.active() && !imported.editable(),
                     "every reopened presentation remains read-only") &&
            passed;
        passed = check(!imported.createEditableCopyTo(QUrl::fromLocalFile(source)) && !imported.editable(),
                     "direct PPT copy refuses to overwrite its source") &&
            passed;
        const QFileInfo suggested_copy(imported.saveUrl().toLocalFile());
        passed = check(suggested_copy.absolutePath() == QFileInfo(source).absolutePath() &&
                         suggested_copy.fileName() == QStringLiteral("presentation-deflate-编辑副本.pptx"),
                     "PPTX copy dialog proposes a new sibling filename instead of the protected source") &&
            passed;
        imported.createEditableCopy();
        imported.selectSaveFile(QUrl::fromLocalFile(source));
        passed = check(!imported.busy() && !imported.error().isEmpty(),
                     "conversion cannot overwrite the imported source") &&
            passed;
        imported.clearError();
        bool failed_copy = false;
        const auto failure_connection = QObject::connect(&imported,
            &mirrorfly::PresentationBridge::copyCompleted, &imported, [&](bool success)
        {
            failed_copy = !success;
        });
        imported.createEditableCopy();
        imported.selectSaveFile(QUrl::fromLocalFile(QDir(directory.path()).filePath("missing/copy.pptx")));
        passed = wait_until_idle(imported) && passed;
        passed = check(failed_copy && !imported.editable() && imported.loadingProgress() < 1,
                     "failed PPTX copy finishes loading and preserves the protected source") &&
            passed;
        QObject::disconnect(failure_connection);
        imported.requestOpen(QUrl::fromLocalFile(source));
        passed = wait_until_idle(imported) && passed;
        imported.finishLoadingFrame();
        passed = check(imported.loadingProgress() == 1, "first prepared slide completes PPTX percentage") &&
            passed;
        bool copy_started = false, copy_finished = false;
        QObject::connect(&imported, &mirrorfly::PresentationBridge::loadStarted, &imported, [&]()
        {
            copy_started = imported.busy() && !imported.editable() && imported.loadingProgress() == 0;
        });
        QObject::connect(&imported, &mirrorfly::PresentationBridge::copyCompleted, &imported,
            [&](bool success)
        {
            copy_finished =
                success && !imported.busy() && imported.editable() && imported.loadingProgress() == 1;
        });
        const QString copy = QDir(directory.path()).filePath(QStringLiteral("editable-copy.pptx"));
        passed = check(imported.createEditableCopyTo(QUrl::fromLocalFile(copy)),
                     "direct PPT copy starts without opening a save dialog") &&
            passed;
        passed =
            check(copy_started && !copy_finished, "PPTX copy starts real loading before returning") && passed;
        passed = wait_until_idle(imported) && passed;
        passed = check(copy_finished, "PPTX copy reaches 100 percent only after successful commit") && passed;
        passed = check(imported.editable() && imported.documentPath() == copy,
                     "successful conversion activates the separately saved editable copy") &&
            passed;
        const int objects_before_delete = imported.semanticTree(0, 0).value("totalObjects").toInt();
        QElapsedTimer delete_latency;
        delete_latency.start();
        passed =
            check(imported.applyEdit(QStringLiteral("deleteShape")) && imported.syncing() &&
                    imported.semanticTree(0, 0).value("totalObjects").toInt() == objects_before_delete - 1 &&
                    delete_latency.elapsed() < 100,
                "imported object deletion disappears before package commit") &&
            passed;
        passed = wait_until_idle(imported) && passed;
        imported.undo();
        passed = check(imported.semanticTree(0, 0).value("totalObjects").toInt() == objects_before_delete,
                     "committed imported deletion remains undoable") &&
            passed;
        imported.saveAs();
        imported.selectSaveFile(QUrl::fromLocalFile(source));
        passed = check(!imported.busy() && !imported.error().isEmpty(),
                     "later save-as operations also preserve the imported source") &&
            passed;
        return passed;
    }

    bool test_authored_theme_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        const auto before = editor.selection().value(QStringLiteral("textColor")).toString();
        bool passed = check(editor.themeEditable(), "new presentations expose the linked theme controls");
        passed = check(editor.themeState().value("available").toBool() &&
                         editor.themeState().value("authored").toBool() &&
                         editor.themeState().value("editable").toBool() == editor.themeEditable() &&
                         editor.themeState().value("linkedSlideCount").toInt() == 1,
                     "theme panel previews the authored palette and linked slide scope") &&
            passed;
        const bool applied = editor.applyEdit(QStringLiteral("applyTheme"),
            {{QStringLiteral("colors"), QVariantMap{{QStringLiteral("dk2"), QStringLiteral("#223344")}}},
                {QStringLiteral("fonts"),
                    QVariantMap{{QStringLiteral("majorLatin"), QStringLiteral("Georgia")},
                        {QStringLiteral("majorEastAsian"), QStringLiteral("Georgia")}}}});
        const bool idle = wait_until_idle(editor);
        if (!applied || !idle)
            std::cerr << "Authored theme bridge: " << editor.error().toStdString() << '\n';
        editor.selectShape(0);
        passed = check(applied && idle &&
                         editor.selection().value(QStringLiteral("textColor")).toString() ==
                             QStringLiteral("#223344") &&
                         editor.selection().value(QStringLiteral("fontFamily")).toString() ==
                             QStringLiteral("Georgia"),
                     "theme edit updates a new presentation through the shared bridge") &&
            passed;
        const auto theme_state = editor.themeState();
        QVariantMap semantic_theme;
        for (const auto& value : editor.semanticTree(0, 0).value("nodes").toList())
        {
            const auto node = value.toMap();
            if (node.value("type").toString() == QStringLiteral("slide"))
                semantic_theme = node.value("theme").toMap();
        }
        passed =
            check(theme_state.value("colors").toMap().value("dk2").toString() == QStringLiteral("#223344") &&
                    theme_state.value("fonts").toMap().value("majorLatin").toString() ==
                        QStringLiteral("Georgia") &&
                    semantic_theme == theme_state,
                "GUI and AI semantic tree share the effective theme state after async commit") &&
            passed;
        editor.undo();
        editor.selectShape(0);
        passed = check(editor.selection().value(QStringLiteral("textColor")).toString() == before,
                     "theme edit participates in undo history") &&
            passed;
        passed = check(editor.themeState().value("colors").toMap().value("dk2").toString() !=
                         QStringLiteral("#223344"),
                     "theme preview follows undo") &&
            passed;
        editor.redo();
        editor.selectShape(0);
        passed = check(editor.selection().value(QStringLiteral("textColor")).toString() ==
                         QStringLiteral("#223344"),
                     "theme edit participates in redo history") &&
            passed;
        passed = check(editor.themeState().value("colors").toMap().value("dk2").toString() ==
                         QStringLiteral("#223344"),
                     "theme preview follows redo") &&
            passed;
        return passed;
    }

    bool test_text_inheritance_bridge()
    {
        mirrorfly::PresentationBridge editor;
        editor.requestNew();
        bool passed =
            check(editor.applyEdit(QStringLiteral("applyTheme"),
                      {{QStringLiteral("fonts"),
                          QVariantMap{{QStringLiteral("majorLatin"), QStringLiteral("Georgia")}}}}) &&
                    wait_until_idle(editor),
                "text inheritance fixture creates a native package through theme edit");
        editor.selectShape(0);
        passed = check(editor.applyEdit(QStringLiteral("formatText"), {{QStringLiteral("fontSize"), 34}}) &&
                         wait_until_idle(editor) &&
                         editor.selection().value(QStringLiteral("fontSizeLocalOverride")).toBool() &&
                         editor.selection()
                             .value(QStringLiteral("actions"))
                             .toList()
                             .contains(QStringLiteral("resetTextInheritance")),
                     "bridge exposes direct size override and reset capability") &&
            passed;
        passed = check(editor.applyEdit(QStringLiteral("resetTextInheritance"),
                           {{QStringLiteral("property"), QStringLiteral("fontSize")}}) &&
                         editor.pendingEdits() == 1 && wait_until_idle(editor) &&
                         !editor.selection().value(QStringLiteral("fontSizeLocalOverride")).toBool() &&
                         editor.selection().value(QStringLiteral("fontSize")).toDouble() != 34,
                     "bridge reports asynchronous reset progress and inherited size") &&
            passed;
        editor.undo();
        editor.selectShape(0);
        passed = check(editor.selection().value(QStringLiteral("fontSize")).toDouble() == 34,
                     "text inheritance reset participates in undo history") &&
            passed;
        editor.redo();
        editor.selectShape(0);
        passed = check(!editor.selection().value(QStringLiteral("fontSizeLocalOverride")).toBool(),
                     "text inheritance reset participates in redo history") &&
            passed;
        return passed;
    }
}

int run_presentation_ui_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    if (argc == 2 && std::string(argv[1]) == "--export-presentation-semantics")
    {
        auto scene = mirrorfly::make_presentation(mirrorfly::PresentationSlideLayout::Blank);
        const QJsonObject contract{{"schemaVersion", 1},
            {"format", "Mirrorfly Office PPTX semantic interface"},
            {"editSchema", QJsonObject::fromVariantMap(mirrorfly::presentation_edit_schema())},
            {"treeExample", QJsonObject::fromVariantMap(mirrorfly::presentation_semantic_tree(scene, 0, 0))},
            {"slideTransitionFields",
                QJsonObject{{"type", "current slide effect; empty means no explicit transition"},
                    {"direction", "l|r|u|d for push"}, {"durationSeconds", "transition duration in seconds"},
                    {"advanceOnClick", "allow click to advance"},
                    {"advanceAfterSeconds", "-1 disables automatic advance; otherwise seconds"},
                    {"editable", "false for protected imported transition content"},
                    {"progress", "wait for snapshot.syncing=false, then refresh semanticTree"}}},
            {"groupLayerFields",
                QJsonObject{{"groupId", "top-level group source ID"},
                    {"layerIndex", "zero-based position among known top-level slide objects"},
                    {"layerCount", "number of known top-level slide objects"},
                    {"layerOptions", "back, backward, forward and front availability booleans"},
                    {"progress", "wait for snapshot.syncing=false, then refresh semanticTree"}}},
            {"themeStateFields",
                QJsonObject{{"name", "current slide's linked theme name; may come from the document"},
                    {"colors", "effective theme slot to #RRGGBB map"},
                    {"fonts", "effective Latin and East Asian major/minor font map"},
                    {"linkedSlideCount", "number of slides using the same theme"},
                    {"available", "false when linked theme color or font scheme is unavailable"},
                    {"editable", "true when an editable copy has at least one writable theme slot"},
                    {"editableColorSlots", "writable color slots; missing imported XML entries are omitted"},
                    {"editableFontSlots", "writable font slots; missing imported XML entries are omitted"},
                    {"guiPath", "page > theme > custom color/font; same applyTheme command as AI"},
                    {"progress", "wait for snapshot.syncing=false, then refresh semanticTree"}}},
            {"placeholderObjectFields",
                QJsonObject{{"type", "OOXML placeholder type"}, {"index", "OOXML placeholder idx"},
                    {"hasLayout", "matching layout placeholder found"},
                    {"hasMaster", "matching master placeholder found"},
                    {"fillSource", "slide, layout, master, or none"},
                    {"inheritedFillSource", "layout, master, or none"},
                    {"localFillOverride", "slide has its own fill definition"},
                    {"outlineSource", "slide, layout, master, or none; last line override layer"},
                    {"inheritedOutlineSource", "layout, master, or none; source after reset"},
                    {"localOutlineOverride", "slide has its own line definition"}}},
            {"tableCellFields",
                QJsonObject{{"row", "zero-based selected row"}, {"column", "zero-based selected column"},
                    {"tableRowCount", "visible table row count"},
                    {"tableColumnCount", "visible table column count"},
                    {"tableHasMerges", "true while any cell remains merged"},
                    {"tableRowSpan", "selected cell's visible row span"},
                    {"tableColumnSpan", "selected cell's visible column span"},
                    {"tableUnmergeable", "selected rectangular origin can be safely split"},
                    {"tableStructureOptions",
                        "insertRow/insertColumn/deleteRow/deleteColumn/mergeRight/mergeDown reflect the "
                        "selected row or column relative to all validated merge regions"},
                    {"tableLocalFillOverride", "selected cell has direct fill over its table style"},
                    {"tableCellStyle.localFillOverride", "direct cell fill exists"},
                    {"tableCellStyle.inheritedFillColor", "table style or default color after reset"},
                    {"tableLocalBorderOverride", "selected cell has one or more direct edge lines"},
                    {"tableBorderColor", "current uniform opaque border color, or empty if mixed"},
                    {"tableBorderWidth", "current uniform border width in points, or zero if mixed"},
                    {"tableCellStyle.localBorderOverride", "direct cell edge lines exist"},
                    {"tableCellStyle.uniformBorderColor", "uniform opaque color, or empty if mixed"},
                    {"tableCellStyle.uniformBorderWidth", "uniform width in points, or zero if mixed"},
                    {"tableBorderEdges",
                        "left/top/right/bottom objects with color, opacity, width, localOverride"},
                    {"tableCellStyle.edges", "same four edge objects for AI inspection"}}},
            {"tableStyleFields",
                QJsonObject{{"tableStyleAvailable", "whether the selected table has a linked style"},
                    {"tableStyleOptions", "complete six-boolean map for the selected table"},
                    {"formatTableStyle.style", "submit all six booleans in one edit"},
                    {"progress", "wait for syncing=false, then reread selection and semanticTree"}}},
            {"effectStyleFields",
                QJsonObject{{"effectStyle.source", "theme, direct, or none"},
                    {"effectStyle.themeIndex", "1-based effectStyleLst entry, or zero"},
                    {"effectStyle.directOverride", "true when a local effect list overrides the theme"},
                    {"selection.effectsSource", "same displayed provenance in the Appearance panel"}}},
            {"textStyleSourceFields",
                QJsonObject{{"fontFamily", "first run Latin font source"},
                    {"eastAsianFont", "first run East Asian font source"},
                    {"fontSize", "first run size source"}, {"color", "first run fill source"},
                    {"scope", "firstRun; use selection for displayed font family"},
                    {"mixed", "true when another run has different provenance"},
                    {"values",
                        "application|theme|presentation|master|layout|slide|tableStyle|tableCell|inherited|"
                        "approximation"}}},
            {"textLocalOverrideFields",
                QJsonObject{{"fontFamily", "any run has direct shape or cell font override"},
                    {"fontSize", "any run has direct shape or cell size override"},
                    {"color", "any run has direct shape or cell text fill override"},
                    {"scope",
                        "anyRun; resetTextInheritance removes this property from the selected object"}}},
            {"progress",
                "after applyEdit, poll snapshot.pendingEdits and snapshot.syncing; refresh the "
                "semantic tree when syncing becomes false"}};
        const auto output = QJsonDocument(contract).toJson(QJsonDocument::Indented);
        std::cout.write(output.constData(), output.size());
        return std::cout.good() ? 0 : 1;
    }
    const bool resources = test_fonts_and_image_limits();
    const bool font_revisions = test_embedded_font_session_revisions();
    const bool metafiles = test_metafile_images();
    const bool tiff = test_tiff_images();
    const bool fonts = test_system_font_application();
    const bool templates = test_template_text_bounds();
    const bool paint = test_paint_and_texture_bounds();
    const bool transitions = test_slide_transitions();
    const bool transition_bridge = test_transition_bridge();
    const bool geometry = test_preset_paths();
    const bool media = test_embedded_media_surface();
    const bool animation = test_animation_surface();
    const bool wordart = test_wordart_surface();
    const bool table = test_table_surface();
    const bool chart = test_chart_surface();
    const bool picture_fill = test_picture_fill_surface();
    const bool lazy_images = test_lazy_image_preparation();
    const bool group_units = test_group_coordinate_units();
    const bool rotated_group_pixels = test_rotated_picture_ungroup_pixels();
    const bool list_spacing = test_list_spacing_surface();
    const bool numbering = test_numbering_format_surface();
    const bool picture_bullet = test_picture_bullet_surface();
    const bool math = test_math_approximation_surface();
    const bool vertical_text = test_vertical_text_surface();
    const bool autofit = test_text_autofit();
    const bool line = test_line_surface();
    const bool preview_reuse = test_template_preview_reuse();
    const bool bridge = test_bridge_state();
    const bool sections = test_section_bridge();
    const bool click_action = test_click_action_bridge();
    const bool format_brush = test_format_brush_bridge();
    const bool find_replace = test_find_replace_bridge();
    const bool guides = test_guide_settings_bridge();
    const bool click_navigation = test_click_navigation_surface();
    const bool placeholder_semantics = test_placeholder_semantics();
    const bool group_semantics = test_group_ungroup_semantics();
    const bool group_extension_bridge = test_group_extension_bridge();
    const bool group_layer_bridge = test_group_layer_bridge();
    const bool table_fill_inheritance = test_table_fill_inheritance_bridge();
    const bool table_style_bridge = test_table_style_bridge();
    const bool table_text_inheritance = test_table_text_inheritance_bridge();
    const bool table_structure_bridge = test_table_structure_bridge();
    const bool editing = test_edit_history_and_source_protection();
    const bool authored_theme = test_authored_theme_bridge();
    const bool text_inheritance = test_text_inheritance_bridge();
    QThreadPool::globalInstance()->waitForDone();
    return resources && font_revisions && metafiles && tiff && templates && fonts && paint && transitions &&
            transition_bridge && geometry && media && animation && wordart && table && chart &&
            picture_fill && lazy_images && group_units && rotated_group_pixels && list_spacing && numbering &&
            picture_bullet && math && vertical_text && autofit && line && preview_reuse && bridge &&
            sections && click_action && format_brush && find_replace && guides && click_navigation &&
            placeholder_semantics && group_semantics && group_extension_bridge && group_layer_bridge &&
            table_fill_inheritance && table_style_bridge && table_text_inheritance &&
            table_structure_bridge && editing && authored_theme && text_inheritance
        ? 0
        : 1;
}

int main(int argc, char* argv[])
{
    return run_presentation_ui_tests(argc, argv);
}
