#include "slide_renderer_paint.hpp"
#include "presentation_animation_painter.hpp"
#include "presentation_fill_renderer.hpp"
#include "presentation_geometry_renderer.hpp"
#include "presentation_line_renderer.hpp"
#include "presentation_text_renderer.hpp"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextOption>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>

namespace mirrorfly::slide_paint
{
    constexpr double pi = 3.14159265358979323846;

    bool shape_has_deferred_asset(const mirrorfly::PresentationShape& shape)
    {
        if (!shape.image_path.empty() || !shape.fill.image_path.empty() ||
            !shape.outline_fill.image_path.empty() || !shape.effects.outline_fill.image_path.empty())
        {
            return true;
        }
        for (const auto& paragraph : shape.text.paragraphs)
        {
            if (!paragraph.bullet_image_path.empty())
            {
                return true;
            }
            for (const auto& run : paragraph.runs)
            {
                if (!run.fill.image_path.empty() || !run.effects.outline_fill.image_path.empty())
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool slide_has_deferred_assets(const mirrorfly::PresentationSlide& slide)
    {
        return !slide.background.image_path.empty() ||
            std::any_of(slide.shapes.begin(), slide.shapes.end(), shape_has_deferred_asset);
    }

    QString utf8(const std::string& value)
    {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }

    bool animation_space(char32_t character)
    {
        return character <= 0x20 || character == 0x85 || character == 0xA0 || character == 0x1680 ||
            (character >= 0x2000 && character <= 0x200A) || character == 0x2028 || character == 0x2029 ||
            character == 0x202F || character == 0x205F || character == 0x3000;
    }

    QColor document_color(const std::string& value, double opacity)
    {
        QColor color(utf8(value));
        if (color.isValid())
        {
            color.setAlphaF(color.alphaF() * std::clamp(opacity, 0.0, 1.0));
        }
        return color;
    }

    mirrorfly::PresentationShape preview_shape(
        const mirrorfly::PresentationShape& shape, const QVariantMap& preview)
    {
        auto result = shape;
        if (preview.value(QStringLiteral("id")).toString() != QString::number(shape.id))
        {
            return result;
        }
        const double x = preview.value(QStringLiteral("x")).toDouble();
        const double y = preview.value(QStringLiteral("y")).toDouble();
        const double width = preview.value(QStringLiteral("width")).toDouble();
        const double height = preview.value(QStringLiteral("height")).toDouble();
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
            std::abs(x) <= 20000 && std::abs(y) <= 20000 && width >= 1 && width <= 20000 && height >= 1 &&
            height <= 20000)
        {
            result.transform[4] = x;
            result.transform[5] = y;
            result.width = width;
            result.height = height;
        }
        return result;
    }

    QBrush fill_brush(const mirrorfly::PresentationFill& fill, const QRectF& bounds,
        const mirrorfly::RenderPresentationPtr& document)
    {
        if (!fill.image_path.empty())
            return mirrorfly::presentation_image_fill(document, fill, bounds);
        if (!fill.pattern.empty())
            return mirrorfly::presentation_pattern_fill(fill);
        if (!fill.stops.empty())
        {
            const double radians = fill.angle_degrees * pi / 180;
            const QPointF direction(std::cos(radians), std::sin(radians));
            const double extent =
                (std::abs(bounds.width() * direction.x()) + std::abs(bounds.height() * direction.y())) / 2;
            QLinearGradient gradient(
                bounds.center() - direction * extent, bounds.center() + direction * extent);
            for (const auto& stop : fill.stops)
            {
                gradient.setColorAt(std::clamp(stop.position, 0.0, 1.0),
                    document_color(stop.color, stop.opacity * fill.opacity));
            }
            return gradient;
        }
        const QColor color = document_color(fill.color, fill.opacity);
        return color.isValid() ? QBrush(color) : QBrush(Qt::NoBrush);
    }

    QPainterPath shape_path(const std::string& geometry, const QRectF& bounds)
    {
        QPainterPath path;
        const double width = bounds.width();
        const double height = bounds.height();
        if (geometry == "ellipse")
        {
            path.addEllipse(bounds);
        }
        else if (geometry == "roundRect")
        {
            const double radius = std::min(width, height) * 0.16;
            path.addRoundedRect(bounds, radius, radius);
        }
        else if (geometry == "line")
        {
            path.moveTo(0, 0);
            path.lineTo(width, height);
        }
        else if (geometry == "triangle" || geometry == "rtTriangle")
        {
            path.moveTo(geometry == "triangle" ? width / 2 : 0, 0);
            path.lineTo(width, height);
            path.lineTo(0, height);
            path.closeSubpath();
        }
        else if (geometry == "diamond")
        {
            path.moveTo(width / 2, 0);
            path.lineTo(width, height / 2);
            path.lineTo(width / 2, height);
            path.lineTo(0, height / 2);
            path.closeSubpath();
        }
        else if (geometry == "parallelogram" || geometry == "trapezoid")
        {
            path.moveTo(width * 0.2, 0);
            path.lineTo(geometry == "trapezoid" ? width * 0.8 : width, 0);
            path.lineTo(geometry == "trapezoid" ? width : width * 0.8, height);
            path.lineTo(0, height);
            path.closeSubpath();
        }
        else if (geometry == "rightArrow" || geometry == "leftArrow" || geometry == "chevron")
        {
            path.moveTo(0, height * 0.25);
            path.lineTo(width * 0.65, height * 0.25);
            path.lineTo(width * 0.65, 0);
            path.lineTo(width, height * 0.5);
            path.lineTo(width * 0.65, height);
            path.lineTo(width * 0.65, height * 0.75);
            path.lineTo(0, height * 0.75);
            if (geometry == "chevron")
            {
                path.lineTo(width * 0.3, height * 0.5);
            }
            path.closeSubpath();
            if (geometry == "leftArrow")
            {
                path = QTransform(-1, 0, 0, 1, width, 0).map(path);
            }
        }
        else if (geometry == "hexagon" || geometry == "pentagon" || geometry == "octagon" ||
            geometry == "star5")
        {
            const int sides = geometry == "hexagon" ? 6 : (geometry == "octagon" ? 8 : 5);
            const int points = geometry == "star5" ? 10 : sides;
            for (int index = 0; index < points; ++index)
            {
                const double angle = -pi / 2 + 2 * pi * index / points;
                const double radius = geometry == "star5" && index % 2 == 1 ? 0.23 : 0.5;
                const QPointF point(
                    width * (0.5 + radius * std::cos(angle)), height * (0.5 + radius * std::sin(angle)));
                if (index == 0)
                {
                    path.moveTo(point);
                }
                else
                {
                    path.lineTo(point);
                }
            }
            path.closeSubpath();
        }
        else
        {
            path.addRect(bounds);
        }
        return path;
    }

    QFont run_font(const mirrorfly::RenderPresentationPtr& document, const mirrorfly::PresentationRun& run,
        double scale, bool east_asian_first)
    {
        const auto family = utf8(run.font_family).trimmed().toCaseFolded();
        const auto east_asian = utf8(run.east_asian_font_family).trimmed().toCaseFolded();
        QFont font;
        const auto resolve = [&](const QString& key, const QString& fallback)
        {
            if (document->embedded_fonts.contains(key) &&
                mirrorfly::presentation_embedded_font_loaded(document, key))
            {
                return document->fonts.value(key, key);
            }
            const auto analyzed = document->fonts.constFind(key);
            if (analyzed != document->fonts.cend() && !document->embedded_fonts.contains(key))
            {
                return analyzed.value();
            }
            if (document->environment)
            {
                const auto installed = document->environment->installed_fonts.constFind(key);
                if (installed != document->environment->installed_fonts.cend())
                {
                    return installed.value();
                }
            }
            return fallback;
        };
        const auto latin = resolve(family, document->latin_fallback);
        QString cjk;
        if (!east_asian.isEmpty())
        {
            cjk = resolve(east_asian, document->cjk_fallback);
        }
        else if (east_asian_first && !family.isEmpty() &&
            !mirrorfly::presentation_embedded_font_loaded(document, family) &&
            (!document->environment || !document->environment->installed_fonts.contains(family)))
        {
            cjk = document->cjk_fallback;
        }
        else
        {
            cjk = latin;
        }
        QStringList families{latin, cjk, document->cjk_fallback};
        if (east_asian_first)
        {
            families = {cjk, latin, document->cjk_fallback};
        }
        font.setFamilies(families);
        font.setPixelSize(std::clamp(qRound(run.font_size * scale), 1, 2048));
        font.setBold(run.bold);
        font.setItalic(run.italic);
        font.setUnderline(run.underline);
        font.setStrikeOut(run.strike);
        font.setLetterSpacing(QFont::AbsoluteSpacing, run.spacing * scale);
        const auto primary_key = east_asian_first && !east_asian.isEmpty() ? east_asian : family;
        if (document->substituted_fonts.contains(primary_key))
        {
            font.setStretch(92);
        }
        return font;
    }

    void draw_text(QPainter* painter, const mirrorfly::RenderPresentationPtr& document,
        const mirrorfly::PresentationShape& shape,
        const std::vector<mirrorfly::PresentationAnimationState>& paragraph_states,
        const std::vector<std::vector<mirrorfly::PresentationAnimationState>>& character_states)
    {
        if (shape.text.paragraphs.empty())
        {
            return;
        }
        const auto& text = shape.text;
        QRectF text_bounds(0, 0, shape.width, shape.height);
        if (shape.path_geometry && shape.path_geometry->width > 0 && shape.path_geometry->height > 0)
        {
            const auto& box = shape.path_geometry->text_rect;
            const double sx = shape.width / shape.path_geometry->width;
            const double sy = shape.height / shape.path_geometry->height;
            text_bounds = QRectF(box[0] * sx, box[1] * sy, std::max(0.0, box[2] - box[0]) * sx,
                std::max(0.0, box[3] - box[1]) * sy);
        }
        QRectF area =
            text_bounds.adjusted(text.inset_left, text.inset_top, -text.inset_right, -text.inset_bottom);
        if (area.isEmpty())
        {
            return;
        }
        if (!text.vertical.empty() && text.vertical != "horz")
        {
            const QPointF center = area.center();
            area = QRectF(
                center.x() - area.height() / 2, center.y() - area.width() / 2, area.height(), area.width());
        }

        QTextDocument text_document;
        text_document.setUndoRedoEnabled(false);
        text_document.setDocumentMargin(0);
        text_document.setDefaultFont(run_font(document, mirrorfly::PresentationRun{}, text.font_scale));
        QTextOption option;
        option.setWrapMode(text.wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        option.setTabStopDistance(18);
        text_document.setDefaultTextOption(option);
        text_document.setTextWidth(area.width());
        QTextCursor cursor(&text_document);
        std::vector<mirrorfly::PresentationTextEffects> effects;
        std::array<int, 9> numbered_indices;
        numbered_indices.fill(1);
        std::array<bool, 9> numbered_active{};
        bool first = true;
        for (std::size_t paragraph_index = 0; paragraph_index < text.paragraphs.size(); ++paragraph_index)
        {
            const auto& paragraph = text.paragraphs[paragraph_index];
            const auto* paragraph_state =
                paragraph_index < paragraph_states.size() ? &paragraph_states[paragraph_index] : nullptr;
            const auto animated_color =
                [&](QColor color, const mirrorfly::PresentationAnimationState* character_state = nullptr)
            {
                const auto apply_color = [&](const mirrorfly::PresentationAnimationState* state)
                {
                    if (!state || state->color.empty())
                        return;
                    QColor replacement(QString::fromStdString(state->color));
                    if (replacement.isValid())
                    {
                        replacement.setAlphaF(color.alphaF());
                        color = replacement;
                    }
                };
                apply_color(paragraph_state);
                apply_color(character_state);
                double opacity = 1;
                if (paragraph_state)
                    opacity *= std::clamp(paragraph_state->opacity, 0.0, 1.0) *
                        std::clamp(paragraph_state->reveal, 0.0, 1.0);
                if (character_state)
                    opacity *= std::clamp(character_state->opacity, 0.0, 1.0) *
                        std::clamp(character_state->reveal, 0.0, 1.0);
                color.setAlphaF(color.alphaF() * opacity);
                return color;
            };
            if (!first)
            {
                cursor.insertBlock();
            }
            first = false;
            QTextBlockFormat block;
            Qt::Alignment alignment = Qt::AlignLeft;
            if (paragraph.alignment == "center")
            {
                alignment = Qt::AlignHCenter;
            }
            else if (paragraph.alignment == "right")
            {
                alignment = Qt::AlignRight;
            }
            else if (paragraph.alignment == "justify")
            {
                alignment = Qt::AlignJustify;
            }
            block.setAlignment(alignment);
            block.setLeftMargin(paragraph.margin_left);
            block.setTextIndent(paragraph.first_line_indent);
            if ((paragraph.numbered || !paragraph.bullet.empty() || !paragraph.bullet_image_path.empty()) &&
                paragraph.first_line_indent < 0)
            {
                QTextOption::Tab stop;
                stop.position = -paragraph.first_line_indent;
                block.setTabPositions({stop});
            }
            const auto& base_run =
                paragraph.runs.empty() ? mirrorfly::PresentationRun{} : paragraph.runs.front();
            const double line_height = QFontMetricsF(run_font(document, base_run, text.font_scale)).height();
            block.setTopMargin(paragraph.space_before_percent >= 0
                    ? line_height * paragraph.space_before_percent
                    : paragraph.space_before);
            block.setBottomMargin(paragraph.space_after_percent >= 0
                    ? line_height * paragraph.space_after_percent
                    : paragraph.space_after);
            block.setProperty(
                mirrorfly::presentation_relative_before_property, paragraph.space_before_percent >= 0);
            block.setProperty(
                mirrorfly::presentation_relative_after_property, paragraph.space_after_percent >= 0);
            if (paragraph.fixed_line_spacing > 0)
            {
                block.setLineHeight(paragraph.fixed_line_spacing, QTextBlockFormat::FixedHeight);
            }
            else
            {
                block.setLineHeight(std::max(0.1, paragraph.line_spacing - text.line_spacing_reduction) * 100,
                    QTextBlockFormat::ProportionalHeight);
            }
            cursor.setBlockFormat(block);
            if (!paragraph.runs.empty())
            {
                const auto& first_run = paragraph.runs.front();
                QTextCharFormat first_format;
                first_format.setFont(run_font(document, first_run, text.font_scale));
                first_format.setForeground(
                    animated_color(document_color(first_run.color, first_run.opacity)));
                cursor.setBlockCharFormat(first_format);
                auto bullet_run = first_run;
                if (!paragraph.bullet_font.empty())
                    bullet_run.font_family = paragraph.bullet_font;
                bullet_run.font_size = paragraph.bullet_size_points > 0
                    ? paragraph.bullet_size_points
                    : first_run.font_size * paragraph.bullet_size_percent;
                QTextCharFormat bullet_format;
                bullet_format.setFont(run_font(document, bullet_run, text.font_scale));
                QBrush bullet_brush = first_format.foreground();
                if (!paragraph.bullet_color.empty())
                {
                    bullet_brush = QBrush(
                        animated_color(document_color(paragraph.bullet_color, paragraph.bullet_opacity)));
                }
                bullet_format.setForeground(bullet_brush);
                const bool has_text =
                    std::any_of(paragraph.runs.begin(), paragraph.runs.end(), [](const auto& run)
                {
                    return !run.text.empty();
                });
                if (paragraph.numbered && has_text)
                {
                    const int level = std::clamp(paragraph.list_level, 0, 8);
                    if (!numbered_active[level])
                        numbered_indices[level] = paragraph.number_start;
                    else
                        numbered_indices[level] = std::max(numbered_indices[level], paragraph.number_start);
                    numbered_active[level] = true;
                    for (int deeper = level + 1; deeper < 9; ++deeper)
                    {
                        numbered_indices[deeper] = 1;
                        numbered_active[deeper] = false;
                    }
                    QString suffix = QStringLiteral(".\t");
                    if (paragraph.number_format == "arabicParenR")
                        suffix = QStringLiteral(")\t");
                    cursor.insertText(QString::number(numbered_indices[level]++) + suffix, bullet_format);
                }
                else
                {
                    numbered_indices.fill(1);
                    numbered_active.fill(false);
                    if (!paragraph.bullet_image_path.empty() && has_text)
                    {
                        const auto bullet_image =
                            mirrorfly::presentation_image(document, paragraph.bullet_image_path);
                        if (!bullet_image.isNull())
                        {
                            QUrl resource;
                            resource.setScheme(QStringLiteral("mirrorfly-bullet"));
                            resource.setPath(QStringLiteral("/") + utf8(paragraph.bullet_image_path));
                            text_document.addResource(QTextDocument::ImageResource, resource, bullet_image);
                            const double height = QFontMetricsF(bullet_format.font()).height() * 0.6;
                            QTextImageFormat image_format;
                            image_format.setName(resource.toString());
                            image_format.setHeight(height);
                            image_format.setWidth(height * bullet_image.width() / bullet_image.height());
                            image_format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
                            cursor.insertImage(image_format);
                            cursor.insertText(QStringLiteral("\t"), bullet_format);
                        }
                        else
                        {
                            cursor.insertText(QStringLiteral("•\t"), bullet_format);
                        }
                    }
                    else if (!paragraph.bullet.empty() && has_text)
                    {
                        cursor.insertText(utf8(paragraph.bullet) + QStringLiteral("\t"), bullet_format);
                    }
                }
            }
            std::size_t paragraph_character = 0;
            for (const auto& run : paragraph.runs)
            {
                effects.push_back(run.effects);
                const auto characters = utf8(run.text).toStdU32String();
                std::size_t start = 0;
                while (start < characters.size())
                {
                    const bool cjk = mirrorfly::presentation_east_asian_character(characters[start]);
                    std::size_t end = start + 1;
                    const bool animated_characters = paragraph_index < character_states.size() &&
                        !character_states[paragraph_index].empty();
                    while (!animated_characters && end < characters.size() &&
                        mirrorfly::presentation_east_asian_character(characters[end]) == cjk)
                    {
                        ++end;
                    }
                    const auto character_index = paragraph_character + start;
                    const auto* character_state =
                        animated_characters && character_index < character_states[paragraph_index].size()
                        ? &character_states[paragraph_index][character_index]
                        : nullptr;
                    QTextCharFormat format;
                    format.setFont(run_font(document, run, text.font_scale, cjk));
                    format.setForeground(
                        animated_color(document_color(run.color, run.opacity), character_state));
                    if (!run.fill.stops.empty() || !run.fill.pattern.empty())
                    {
                        double animation_opacity = paragraph_state
                            ? std::clamp(paragraph_state->opacity, 0.0, 1.0) *
                                std::clamp(paragraph_state->reveal, 0.0, 1.0)
                            : 1;
                        if (character_state)
                            animation_opacity *= std::clamp(character_state->opacity, 0.0, 1.0) *
                                std::clamp(character_state->reveal, 0.0, 1.0);
                        if (animation_opacity >= 1 && (!paragraph_state || paragraph_state->color.empty()) &&
                            (!character_state || character_state->color.empty()))
                            format.setForeground(
                                fill_brush(run.fill, QRectF(0, 0, area.width(), area.height()), document));
                    }
                    if (run.effects.outline_width > 0)
                    {
                        QBrush outline =
                            document_color(run.effects.outline_color, run.effects.outline_opacity);
                        if (!run.effects.outline_fill.stops.empty() ||
                            !run.effects.outline_fill.pattern.empty())
                            outline = fill_brush(run.effects.outline_fill,
                                QRectF(0, 0, area.width(), area.height()), document);
                        if (!run.effects.outline_color.empty() || !run.effects.outline_fill.stops.empty() ||
                            !run.effects.outline_fill.pattern.empty())
                            format.setTextOutline(QPen(outline, run.effects.outline_width));
                    }
                    format.setBaselineOffset(run.baseline * 100);
                    format.setProperty(
                        mirrorfly::presentation_text_effect_property, static_cast<int>(effects.size()));
                    cursor.insertText(
                        QString::fromUcs4(characters.data() + start, static_cast<qsizetype>(end - start)),
                        format);
                    start = end;
                }
                paragraph_character += characters.size();
            }
        }

        if (text.auto_fit || shape.table_cell)
            mirrorfly::fit_presentation_text(text_document, area.size(), shape.table_cell.has_value());
        const double free_height = std::max(0.0, area.height() - text_document.size().height());
        const double offset = text.vertical_alignment == "center"
            ? free_height / 2
            : (text.vertical_alignment == "bottom" ? free_height : 0);
        mirrorfly::paint_presentation_text(*painter, text_document, text, area, offset, effects);
    }

    QPainterPath shape_silhouette(const mirrorfly::PresentationShape& shape, const QRectF& bounds)
    {
        QPainterPath result;
        if (shape.path_geometry)
        {
            for (const auto& path : shape.path_geometry->paths)
            {
                if (path.fill != "none")
                {
                    result.addPath(mirrorfly::presentation_path(path, shape.width, shape.height));
                }
            }
        }
        if (result.isEmpty())
        {
            result = shape_path(shape.geometry, bounds);
        }
        return result;
    }

    void paint_shape_effects(QPainter& painter, const mirrorfly::PresentationShape& shape,
        const QPainterPath& silhouette, bool foreground)
    {
        const auto& effects = shape.effects;
        if (!foreground)
        {
            if (shape.approximate_3d)
            {
                painter.save();
                painter.translate(
                    std::clamp(shape.width * 0.015, 1.5, 6.0), std::clamp(shape.height * 0.02, 1.5, 6.0));
                painter.fillPath(silhouette, QColor(25, 31, 42, 72));
                painter.restore();
            }
            if (!shape.inner_shadow && effects.shadow_opacity > 0)
            {
                QColor color = document_color(effects.shadow_color.empty() ? "#000000" : effects.shadow_color,
                    effects.shadow_opacity * 0.72);
                painter.save();
                painter.translate(effects.shadow_x, effects.shadow_y);
                if (effects.shadow_blur > 0)
                {
                    QPen pen(color, std::max(1.0, effects.shadow_blur));
                    pen.setJoinStyle(Qt::RoundJoin);
                    painter.setPen(pen);
                    painter.setBrush(color);
                    painter.drawPath(silhouette);
                }
                else
                {
                    painter.fillPath(silhouette, color);
                }
                painter.restore();
            }
            if (effects.glow_opacity > 0 && effects.glow_radius > 0)
            {
                QColor color = document_color(
                    effects.glow_color.empty() ? "#FFFFFF" : effects.glow_color, effects.glow_opacity * 0.6);
                QPen pen(color, std::max(1.0, effects.glow_radius * 2));
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(silhouette);
            }
            return;
        }

        if (shape.inner_shadow && effects.shadow_opacity > 0)
        {
            QColor color = document_color(effects.shadow_color.empty() ? "#000000" : effects.shadow_color,
                effects.shadow_opacity * 0.65);
            QPen pen(color, std::max(1.0, effects.shadow_blur + 1));
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(silhouette.translated(effects.shadow_x, effects.shadow_y));
        }
        if (shape.soft_edge_radius > 0)
        {
            QPen pen(QColor(255, 255, 255, 80), std::max(1.0, shape.soft_edge_radius));
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(silhouette);
        }
        if (shape.approximate_3d)
        {
            QPen highlight(QColor(255, 255, 255, 90), 1.5);
            highlight.setJoinStyle(Qt::RoundJoin);
            painter.setPen(highlight);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(silhouette.translated(-0.75, -0.75));
        }
        if (effects.reflection_opacity > 0)
        {
            painter.save();
            painter.setOpacity(std::clamp(effects.reflection_opacity * 0.35, 0.0, 0.35));
            painter.translate(0, shape.height * 2 + effects.reflection_offset);
            painter.scale(1, -1);
            painter.fillPath(silhouette, QColor(80, 88, 100));
            painter.restore();
        }
    }

    void draw_shape(QPainter* painter, const mirrorfly::RenderPresentationPtr& document,
        const mirrorfly::PresentationShape& shape, const QVariantMap& theme, bool editing_text,
        const QImage& video_frame, const std::vector<mirrorfly::PresentationAnimationState>& paragraph_states,
        const std::vector<std::vector<mirrorfly::PresentationAnimationState>>& character_states)
    {
        painter->save();
        const auto& matrix = shape.transform;
        painter->setWorldTransform(
            QTransform(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]), true);
        const QRectF bounds(0, 0, shape.width, shape.height);
        const QPainterPath silhouette = shape_silhouette(shape, bounds);
        paint_shape_effects(*painter, shape, silhouette, false);
        const QColor outline = document_color(shape.outline_color, shape.outline_opacity);
        QPen pen(Qt::NoPen);
        if ((outline.isValid() || !shape.outline_fill.stops.empty() || !shape.outline_fill.pattern.empty()) &&
            shape.outline_width > 0)
        {
            const auto brush = shape.outline_fill.stops.empty() && shape.outline_fill.pattern.empty()
                ? QBrush(outline)
                : fill_brush(shape.outline_fill, bounds, document);
            pen = QPen(brush, shape.outline_width);
            pen = mirrorfly::presentation_line_pen(pen, shape.line_style);
        }
        if (shape.path_geometry)
        {
            mirrorfly::paint_presentation_geometry(*painter, *shape.path_geometry, shape.width, shape.height,
                fill_brush(shape.fill, bounds, document), pen);
        }
        else
        {
            painter->setPen(pen);
            painter->setBrush(fill_brush(shape.fill, bounds, document));
            painter->drawPath(shape_path(shape.geometry, bounds));
        }
        mirrorfly::paint_presentation_line_ends(*painter, shape, pen);
        paint_shape_effects(*painter, shape, silhouette, true);

        if (!video_frame.isNull() || !shape.image_path.empty())
        {
            painter->save();
            QPainterPath clip;
            clip.setFillRule(Qt::WindingFill);
            if (shape.path_geometry)
                for (const auto& path : shape.path_geometry->paths)
                    if (path.fill != "none")
                        clip.addPath(mirrorfly::presentation_path(path, shape.width, shape.height));
            if (clip.isEmpty())
                clip = shape_path(shape.geometry, bounds);
            painter->setClipPath(clip, Qt::IntersectClip);
        }
        if (!video_frame.isNull())
            painter->drawImage(bounds, video_frame);
        else if (!shape.image_path.empty())
        {
            const QImage image = mirrorfly::presentation_image(document, shape.image_path);
            if (!image.isNull())
            {
                const auto& crop = shape.image_crop;
                const QRectF source(image.width() * crop[0], image.height() * crop[1],
                    image.width() * std::max(0.0, 1 - crop[0] - crop[2]),
                    image.height() * std::max(0.0, 1 - crop[1] - crop[3]));
                painter->setOpacity(std::clamp(shape.image_opacity, 0.0, 1.0));
                painter->drawImage(bounds, image, source);
            }
            else
            {
                painter->setPen(QPen(QColor(theme.value(QStringLiteral("textSecondary")).toString()), 1));
                painter->setBrush(QColor(theme.value(QStringLiteral("accentSoft")).toString()));
                painter->drawRect(bounds);
                QFont font;
                font.setFamilies({document->cjk_fallback, document->latin_fallback});
                font.setPixelSize(std::max(1, theme.value(QStringLiteral("fontSize")).toInt() - 2));
                painter->setFont(font);
                painter->drawText(bounds.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap,
                    QStringLiteral("此图片暂不支持预览"));
            }
        }
        if (!video_frame.isNull() || !shape.image_path.empty())
            painter->restore();
        if (!editing_text)
        {
            draw_text(painter, document, shape, paragraph_states, character_states);
        }
        painter->restore();
    }

    double shape_effect_padding(const mirrorfly::PresentationShape& shape)
    {
        return std::max({4.0, shape.outline_width + 2, shape.effects.glow_radius,
            shape.effects.shadow_blur +
                std::max(std::abs(shape.effects.shadow_x), std::abs(shape.effects.shadow_y)),
            shape.soft_edge_radius});
    }

    QRectF shape_scene_bounds(const mirrorfly::PresentationShape& shape)
    {
        QRectF local(0, 0, shape.width, shape.height);
        const double padding = shape_effect_padding(shape);
        local.adjust(-padding, -padding, padding, padding);
        const auto& matrix = shape.transform;
        return QTransform(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5])
            .mapRect(local.normalized());
    }

    bool paint_shape_patch(QPainter& painter, const mirrorfly::RenderPresentationPtr& document,
        int slide_index, const QVariantMap& theme, const QSizeF& viewport,
        const mirrorfly::PresentationShape& previous, int replacement_index,
        const mirrorfly::PresentationShape* replacement)
    {
        if (!document || !document->scene || slide_index < 0 ||
            slide_index >= static_cast<int>(document->scene->slides.size()) || viewport.isEmpty())
        {
            return false;
        }
        const auto& scene = *document->scene;
        if (scene.width <= 0 || scene.height <= 0)
        {
            return false;
        }
        QRectF dirty = shape_scene_bounds(previous);
        if (replacement)
        {
            dirty = dirty.united(shape_scene_bounds(*replacement));
        }
        if (dirty.isEmpty())
        {
            return false;
        }
        const auto& slide = scene.slides[static_cast<std::size_t>(slide_index)];
        const double scale = std::min(viewport.width() / scene.width, viewport.height() / scene.height);
        painter.save();
        painter.translate(
            (viewport.width() - scene.width * scale) / 2, (viewport.height() - scene.height * scale) / 2);
        painter.scale(scale, scale);
        painter.setClipRect(QRectF(0, 0, scene.width, scene.height), Qt::IntersectClip);
        painter.setClipRect(dirty, Qt::IntersectClip);
        const QRectF bounds(0, 0, scene.width, scene.height);
        painter.fillRect(bounds, Qt::white);
        painter.fillRect(bounds, fill_brush(slide.background, bounds, document));
        for (int index = 0; index < static_cast<int>(slide.shapes.size()); ++index)
        {
            const auto* shape = index == replacement_index && replacement
                ? replacement
                : &slide.shapes[static_cast<std::size_t>(index)];
            if (shape_scene_bounds(*shape).intersects(dirty))
            {
                draw_shape(&painter, document, *shape, theme, false);
            }
        }
        painter.restore();
        return true;
    }
}

namespace mirrorfly
{
    void paint_presentation_slide(QPainter& painter, const RenderPresentationPtr& document, std::size_t index,
        const QVariantMap& theme, const QRectF& area)
    {
        if (!document || !document->scene || index >= document->scene->slides.size())
            return;
        ensure_presentation_fonts(document, index);
        const auto& scene = *document->scene;
        const double scale = std::min(area.width() / scene.width, area.height() / scene.height);
        painter.save();
        painter.translate(
            area.center().x() - scene.width * scale / 2, area.center().y() - scene.height * scale / 2);
        painter.scale(scale, scale);
        const QRectF bounds(0, 0, scene.width, scene.height);
        painter.setClipRect(bounds, Qt::IntersectClip);
        const auto& slide = scene.slides[index];
        painter.fillRect(bounds, Qt::white);
        painter.fillRect(bounds, slide_paint::fill_brush(slide.background, bounds, document));
        for (const auto& shape : slide.shapes)
        {
            if (document->renderCancelled())
            {
                break;
            }
            slide_paint::draw_shape(&painter, document, shape, theme, false);
        }
        painter.restore();
    }
}
