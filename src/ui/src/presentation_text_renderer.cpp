#include "presentation_text_renderer.hpp"

#include <QCache>
#include <QCryptographicHash>
#include <QDataStream>
#include <QGlyphRun>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    constexpr double pi = 3.14159265358979323846;

    QByteArray layer_key(const QTextDocument& document, const QSize& size, double scale, double padding,
        const std::vector<mirrorfly::PresentationTextEffects>& effects)
    {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream << document.toHtml() << document.defaultFont() << size << scale << padding;
        for (auto block = document.begin(); block.isValid(); block = block.next())
            for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
                stream << iterator.fragment().charFormat();
        for (const auto& effect : effects)
        {
            stream << QString::fromStdString(effect.shadow_color) << effect.shadow_opacity;
            stream << effect.shadow_blur << effect.shadow_x << effect.shadow_y;
            stream << QString::fromStdString(effect.glow_color) << effect.glow_opacity << effect.glow_radius;
        }
        return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    }

    QPainterPath fragment_path(const QTextBlock& block, const QTextFragment& fragment)
    {
        QPainterPath result;
        const auto* layout = block.layout();
        if (!layout)
            return result;
        const auto runs = layout->glyphRuns(fragment.position() - block.position(), fragment.length());
        for (const auto& run : runs)
        {
            const auto font = run.rawFont();
            const auto glyphs = run.glyphIndexes();
            const auto positions = run.positions();
            for (qsizetype index = 0; index < std::min(glyphs.size(), positions.size()); ++index)
            {
                const auto position = positions[index] + layout->position();
                result.addPath(QTransform::fromTranslate(position.x(), position.y())
                        .map(font.pathForGlyph(glyphs[index])));
            }
        }
        return result;
    }

    QImage colored_blur(const QPainterPath& path, const QSize& size, double scale, double padding,
        const std::string& color, double opacity, double blur)
    {
        QImage mask(size, QImage::Format_ARGB32_Premultiplied);
        if (mask.isNull())
            return {};
        mask.fill(Qt::transparent);
        {
            QPainter painter(&mask);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.scale(scale, scale);
            painter.translate(padding, padding);
            painter.fillPath(path, Qt::white);
        }
        const int width = mask.width();
        const int height = mask.height();
        std::vector<unsigned char> alpha(static_cast<std::size_t>(width) * height);
        std::vector<unsigned char> scratch(alpha.size());
        for (int row = 0; row < height; ++row)
        {
            const auto pixels = reinterpret_cast<const QRgb*>(mask.constScanLine(row));
            for (int column = 0; column < width; ++column)
                alpha[static_cast<std::size_t>(row) * width + column] =
                    static_cast<unsigned char>(qAlpha(pixels[column]));
        }
        const int radius = std::clamp(static_cast<int>(std::ceil(blur * scale / 2)), 0, 96);
        if (radius)
        {
            const int divisor = radius * 2 + 1;
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int row = 0; row < height; ++row)
                {
                    int sum = 0;
                    const auto base = static_cast<std::size_t>(row) * width;
                    for (int column = 0; column <= std::min(radius, width - 1); ++column)
                        sum += alpha[base + column];
                    for (int column = 0; column < width; ++column)
                    {
                        scratch[base + column] = static_cast<unsigned char>(sum / divisor);
                        if (column - radius >= 0)
                            sum -= alpha[base + column - radius];
                        if (column + radius + 1 < width)
                            sum += alpha[base + column + radius + 1];
                    }
                }
                for (int column = 0; column < width; ++column)
                {
                    int sum = 0;
                    for (int row = 0; row <= std::min(radius, height - 1); ++row)
                        sum += scratch[static_cast<std::size_t>(row) * width + column];
                    for (int row = 0; row < height; ++row)
                    {
                        alpha[static_cast<std::size_t>(row) * width + column] =
                            static_cast<unsigned char>(sum / divisor);
                        if (row - radius >= 0)
                            sum -= scratch[static_cast<std::size_t>(row - radius) * width + column];
                        if (row + radius + 1 < height)
                            sum += scratch[static_cast<std::size_t>(row + radius + 1) * width + column];
                    }
                }
            }
        }
        const QColor tint(QString::fromStdString(color));
        for (int row = 0; row < height; ++row)
        {
            auto pixels = reinterpret_cast<QRgb*>(mask.scanLine(row));
            for (int column = 0; column < width; ++column)
            {
                const int value = qRound(
                    alpha[static_cast<std::size_t>(row) * width + column] * std::clamp(opacity, 0.0, 1.0));
                pixels[column] = qRgba(
                    tint.red() * value / 255, tint.green() * value / 255, tint.blue() * value / 255, value);
            }
        }
        return mask;
    }

    void paint_reflections(QPainter& painter, QTextDocument& document, const QRectF& area, double offset,
        const std::vector<mirrorfly::PresentationTextEffects>& effects)
    {
        using Parameters = std::array<double, 5>;
        std::map<Parameters, QPainterPath> groups;
        for (auto block = document.begin(); block.isValid(); block = block.next())
            for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
            {
                const auto fragment = iterator.fragment();
                const int index =
                    fragment.charFormat().intProperty(mirrorfly::presentation_text_effect_property) - 1;
                if (index < 0 || static_cast<std::size_t>(index) >= effects.size())
                    continue;
                const auto& effect = effects[index];
                if (effect.reflection_opacity <= 0 && effect.reflection_end_opacity <= 0)
                    continue;
                auto path = fragment_path(block, fragment);
                if (effect.outline_width > 0)
                {
                    QPainterPathStroker stroker;
                    stroker.setWidth(effect.outline_width);
                    path = path.united(stroker.createStroke(path));
                }
                const Parameters parameters{effect.reflection_opacity, effect.reflection_offset,
                    effect.reflection_end_opacity, effect.reflection_start_position,
                    effect.reflection_end_position};
                groups[parameters].addPath(path);
            }
        thread_local QCache<QByteArray, QImage> cache(16 * 1024);
        for (const auto& group : groups)
        {
            const auto& values = group.first;
            const auto& path = group.second;
            const auto bounds = path.boundingRect().intersected(QRectF(0, 0, area.width(), area.height()));
            if (bounds.isEmpty())
                continue;
            const double scale = std::min({2.0, 4096 / bounds.width(), 4096 / bounds.height(),
                std::sqrt(4 * 1024 * 1024.0 / (bounds.width() * bounds.height()))});
            const QSize size(
                std::max(1, qCeil(bounds.width() * scale)), std::max(1, qCeil(bounds.height() * scale)));
            QByteArray key = layer_key(document, size, scale, 0, {});
            QDataStream stream(&key, QIODevice::Append);
            stream << path << bounds;
            for (double value : values)
                stream << value;
            key = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
            QImage reflected;
            if (const auto cached = cache.object(key))
                reflected = *cached;
            else
            {
                QImage ink(size, QImage::Format_ARGB32_Premultiplied);
                if (ink.isNull())
                    continue;
                ink.fill(Qt::transparent);
                QPainter source(&ink);
                source.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
                source.scale(scale, scale);
                source.translate(-bounds.topLeft());
                source.setClipPath(path);
                document.drawContents(&source, bounds);
                source.end();
                reflected = ink.mirrored(false, true);
                QPainter fade(&reflected);
                fade.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                QLinearGradient gradient(0, 0, 0, reflected.height());
                const double start = std::clamp(values[3], 0.0, 0.99999);
                const double end = std::clamp(values[4], start + 0.00001, 1.0);
                gradient.setColorAt(start, QColor(0, 0, 0, qRound(values[0] * 255)));
                gradient.setColorAt(end, QColor(0, 0, 0, qRound(values[2] * 255)));
                fade.fillRect(reflected.rect(), gradient);
                fade.end();
                cache.insert(
                    key, new QImage(reflected), static_cast<int>((reflected.sizeInBytes() + 1023) / 1024));
            }
            painter.drawImage(
                QRectF(area.left() + bounds.left(), area.top() + offset + bounds.bottom() + values[1],
                    bounds.width(), bounds.height()),
                reflected);
        }
    }

    QPointF warped_point(
        double u, double v, double width, double height, const std::string& warp, double adjustment)
    {
        const double bend = std::clamp(adjustment, 0.05, 0.45);
        const double arch = 4 * (u - 0.5) * (u - 0.5);
        if (warp.find("Circle") != std::string::npos || warp.find("Ring") != std::string::npos ||
            warp.find("Button") != std::string::npos)
        {
            const double angle = 2 * pi * u - pi / 2;
            const double radius = 0.25 + 0.25 * v;
            return {width * (0.5 + radius * std::cos(angle)), height * (0.5 + radius * std::sin(angle))};
        }
        double top = 0;
        double bottom = 1;
        if (warp.find("Wave") != std::string::npos)
        {
            const double cycles =
                warp.find("Double") != std::string::npos || warp.find("Wave4") != std::string::npos ? 2 : 1;
            top = bend * (1 + std::sin(u * 2 * pi * cycles));
            bottom = 1 - 2 * bend + top;
        }
        else if (warp.find("ArchUp") != std::string::npos || warp == "textCurveUp" || warp == "textCanUp")
        {
            top = arch * bend;
            bottom = 1 - bend + top;
        }
        else if (warp.find("ArchDown") != std::string::npos || warp == "textCurveDown" ||
            warp == "textCanDown")
        {
            top = (1 - arch) * bend;
            bottom = 1 - bend + top;
        }
        else if (warp.find("Inflate") != std::string::npos || warp.find("Deflate") != std::string::npos)
        {
            const double curve = warp.find("Deflate") != std::string::npos ? 1 - arch : arch;
            if (warp.find("Bottom") == std::string::npos)
                top = bend * curve;
            if (warp.find("Top") == std::string::npos)
                bottom = 1 - bend * curve;
        }
        else if (warp.find("Triangle") != std::string::npos || warp.find("Chevron") != std::string::npos)
        {
            const bool inverted = warp.find("Inverted") != std::string::npos;
            const double slope = std::abs(2 * u - 1);
            top = bend * (inverted ? 1 - slope : slope);
            bottom = warp.find("Chevron") != std::string::npos ? 1 - bend + top : 1;
        }
        else if (warp.find("Slant") != std::string::npos || warp.find("Cascade") != std::string::npos)
        {
            top = bend * (warp.find("Up") != std::string::npos ? 1 - u : u);
            bottom = 1 - bend + top;
        }
        else if (warp == "textFadeRight" || warp == "textFadeLeft")
        {
            const double slope = warp == "textFadeRight" ? u : 1 - u;
            top = bend * slope;
            bottom = 1 - top;
        }
        else if (warp == "textFadeUp" || warp == "textFadeDown")
        {
            const double slope = warp == "textFadeUp" ? 1 - v : v;
            return {width * (bend * slope + u * (1 - 2 * bend * slope)), v * height};
        }
        return {u * width, height * (top + (bottom - top) * v)};
    }

    void draw_warped(QPainter& painter, const QImage& image, const QRectF& bounds, const std::string& warp,
        double adjustment)
    {
        painter.save();
        painter.translate(bounds.topLeft());
        const int strips = 64;
        for (int index = 0; index < strips; ++index)
        {
            const double u = index / static_cast<double>(strips);
            const double next = (index + 1) / static_cast<double>(strips);
            const QPolygonF source{{u * image.width(), 0}, {next * image.width(), 0},
                {next * image.width(), static_cast<double>(image.height())},
                {u * image.width(), static_cast<double>(image.height())}};
            const QPolygonF target{warped_point(u, 0, bounds.width(), bounds.height(), warp, adjustment),
                warped_point(next, 0, bounds.width(), bounds.height(), warp, adjustment),
                warped_point(next, 1, bounds.width(), bounds.height(), warp, adjustment),
                warped_point(u, 1, bounds.width(), bounds.height(), warp, adjustment)};
            QTransform transform;
            if (!QTransform::quadToQuad(source, target, transform))
                continue;
            painter.save();
            QPainterPath clip;
            clip.addPolygon(target);
            painter.setClipPath(clip, Qt::IntersectClip);
            painter.setTransform(transform, true);
            painter.drawImage(QPointF(0, 0), image);
            painter.restore();
        }
        painter.restore();
    }
}

namespace mirrorfly
{
    void fit_presentation_text(QTextDocument& document, const QSizeF& bounds, bool shrink_to_cell)
    {
        const auto fits = [&]()
        {
            return document.size().height() <= bounds.height() + 0.01 &&
                document.idealWidth() <= bounds.width() + 0.01;
        };
        if (bounds.isEmpty() || fits())
            return;
        QByteArray description;
        QDataStream stream(&description, QIODevice::WriteOnly);
        stream << document.toHtml() << document.defaultFont() << bounds << shrink_to_cell;
        const auto option = document.defaultTextOption();
        stream << static_cast<int>(option.wrapMode()) << static_cast<int>(option.textDirection());
        stream << static_cast<int>(option.flags()) << option.tabStopDistance();
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            const auto format = block.blockFormat();
            stream << format.boolProperty(presentation_relative_before_property);
            stream << format.boolProperty(presentation_relative_after_property);
            const auto tabs = format.tabPositions();
            stream << tabs.size();
            for (const auto& tab : tabs)
                stream << tab.position << static_cast<int>(tab.type) << tab.delimiter;
        }
        const auto key = QCryptographicHash::hash(description, QCryptographicHash::Sha256);
        struct Span
        {
            int position;
            int length;
            QTextCharFormat format;
        };
        struct Block
        {
            int position;
            QTextBlockFormat format;
            QTextCharFormat character;
        };
        std::vector<Span> spans;
        std::vector<Block> blocks;
        for (auto block = document.begin(); block.isValid(); block = block.next())
        {
            blocks.push_back({block.position(), block.blockFormat(), block.charFormat()});
            for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
            {
                const auto fragment = iterator.fragment();
                spans.push_back({fragment.position(), fragment.length(), fragment.charFormat()});
            }
        }
        const auto stretched_format = [shrink_to_cell](QTextCharFormat format, double scale)
        {
            QFont font = format.font();
            const int stretch = font.stretch() > 0 ? font.stretch() : 100;
            if (shrink_to_cell)
            {
                if (font.pixelSize() > 0)
                    font.setPixelSize(std::max(1, static_cast<int>(std::floor(font.pixelSize() * scale))));
                else if (font.pointSizeF() > 0)
                    font.setPointSizeF(std::max(1.0, font.pointSizeF() * scale));
                if (font.letterSpacingType() == QFont::AbsoluteSpacing)
                    font.setLetterSpacing(QFont::AbsoluteSpacing, font.letterSpacing() * scale);
            }
            else
                font.setStretch(std::clamp(qRound(stretch * scale), 1, 4000));
            format.setFont(font);
            return format;
        };
        const auto apply_scale = [&](double scale)
        {
            QTextCursor cursor(&document);
            cursor.beginEditBlock();
            for (const auto& block : blocks)
            {
                cursor.setPosition(block.position);
                auto format = block.format;
                if (shrink_to_cell)
                {
                    format.setTopMargin(format.topMargin() * scale);
                    format.setBottomMargin(format.bottomMargin() * scale);
                    if (format.lineHeightType() == QTextBlockFormat::FixedHeight)
                        format.setLineHeight(format.lineHeight() * scale, QTextBlockFormat::FixedHeight);
                }
                cursor.setBlockFormat(format);
                cursor.setBlockCharFormat(stretched_format(block.character, scale));
            }
            for (const auto& span : spans)
            {
                cursor.setPosition(span.position);
                cursor.setPosition(span.position + span.length, QTextCursor::KeepAnchor);
                cursor.setCharFormat(stretched_format(span.format, scale));
            }
            cursor.endEditBlock();
        };
        thread_local QCache<QByteArray, double> scales(2048);
        if (const auto cached = scales.object(key))
        {
            apply_scale(*cached);
            return;
        }
        double lower = shrink_to_cell ? 0.05 : 0.92, upper = 1;
        for (int iteration = 0; iteration < 9; ++iteration)
        {
            const double value = (lower + upper) / 2;
            apply_scale(value);
            if (fits())
                lower = value;
            else
                upper = value;
        }
        apply_scale(lower);
        scales.insert(key, new double(lower));
    }

    void paint_presentation_text(QPainter& painter, QTextDocument& document, const PresentationText& text,
        const QRectF& area, double offset, const std::vector<PresentationTextEffects>& effects)
    {
        const bool warped = !text.warp.empty() && text.warp != "textNoShape" && text.warp != "textPlain";
        const double visible_width =
            text.clip_horizontal || warped ? area.width() : std::max(area.width(), document.idealWidth());
        const double visible_height =
            text.clip_vertical || warped ? area.height() : std::max(area.height(), document.size().height());
        const QRectF visible(area.topLeft(), QSizeF(visible_width, visible_height));
        double padding = 0;
        for (const auto& effect : effects)
            padding = std::max(
                {padding, effect.shadow_blur * 2 + std::abs(effect.shadow_x) + std::abs(effect.shadow_y),
                    effect.glow_radius * 2, effect.outline_width * 2});
        const bool composited = warped ||
            std::any_of(effects.begin(), effects.end(), [](const auto& effect)
        {
            return effect.shadow_opacity > 0 || effect.glow_opacity > 0 || effect.reflection_opacity > 0 ||
                effect.reflection_end_opacity > 0;
        });
        painter.save();
        const double rotation = text.rotation +
            (text.vertical == "vert270" ? 270 : (!text.vertical.empty() && text.vertical != "horz" ? 90 : 0));
        if (rotation)
        {
            painter.translate(area.center());
            painter.rotate(rotation);
            painter.translate(-area.center());
        }
        if (!composited)
        {
            painter.setClipRect(visible.adjusted(-padding, -padding, padding, padding), Qt::IntersectClip);
            painter.translate(area.left(), area.top() + offset);
            document.drawContents(&painter,
                QRectF(-padding, -padding, visible_width + 2 * padding, visible_height + 2 * padding));
            painter.restore();
            return;
        }
        const double width = visible_width + 2 * padding;
        const double height = std::min(visible_height, document.size().height()) + 2 * padding;
        const double scale =
            std::min({2.0, 4096 / width, 4096 / height, std::sqrt(4 * 1024 * 1024.0 / (width * height))});
        const QSize size(std::max(1, static_cast<int>(std::ceil(width * scale))),
            std::max(1, static_cast<int>(std::ceil(height * scale))));
        thread_local QCache<QByteArray, QImage> layers(32 * 1024);
        const auto key = layer_key(document, size, scale, padding, effects);
        QImage image;
        if (const auto cached = layers.object(key))
            image = *cached;
        else
        {
            image = QImage(size, QImage::Format_ARGB32_Premultiplied);
            if (image.isNull())
            {
                painter.restore();
                return;
            }
            image.fill(Qt::transparent);
            QPainter output(&image);
            output.scale(scale, scale);
            output.translate(padding, padding);
            for (auto block = document.begin(); block.isValid(); block = block.next())
                for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator)
                {
                    const auto fragment = iterator.fragment();
                    const int index =
                        fragment.charFormat().intProperty(presentation_text_effect_property) - 1;
                    if (index < 0 || static_cast<std::size_t>(index) >= effects.size())
                        continue;
                    const auto& effect = effects[index];
                    if (effect.shadow_opacity <= 0 && effect.glow_opacity <= 0)
                        continue;
                    const auto path = fragment_path(block, fragment);
                    if (effect.shadow_opacity > 0)
                    {
                        const auto layer = colored_blur(path, size, scale, padding, effect.shadow_color,
                            effect.shadow_opacity, effect.shadow_blur);
                        output.drawImage(
                            QRectF(-padding + effect.shadow_x, -padding + effect.shadow_y, width, height),
                            layer);
                    }
                    if (effect.glow_opacity > 0)
                    {
                        const auto layer = colored_blur(path, size, scale, padding, effect.glow_color,
                            effect.glow_opacity, effect.glow_radius);
                        output.drawImage(QRectF(-padding, -padding, width, height), layer);
                    }
                }
            document.drawContents(&output,
                QRectF(-padding, -padding, visible_width + 2 * padding, visible_height + 2 * padding));
            output.end();
            const auto cost = static_cast<int>((image.sizeInBytes() + 1023) / 1024);
            layers.insert(key, new QImage(image), cost);
        }
        const QRectF destination(area.left() - padding, area.top() + offset - padding, width, height);
        if (warped)
            draw_warped(painter, image, destination, text.warp, text.warp_adjustment);
        else
            painter.drawImage(destination, image);
        paint_reflections(painter, document, visible, offset, effects);
        painter.restore();
    }
}
