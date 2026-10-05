#include "office_ai_page_layout.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QRectF>
#include <QStringList>

#include <cmath>

namespace
{
    QJsonObject failure(const QString& code, const QString& hint)
    {
        return {{"ok", false}, {"error", code}, {"hint", hint}, {"executed", 0}};
    }

    double luminance(const QColor& color)
    {
        const auto channel = [](int byte)
        {
            const double value = byte / 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) +
            0.0722 * channel(color.blue());
    }

    double contrast(const QColor& first, const QColor& second)
    {
        const double bright = qMax(luminance(first), luminance(second));
        const double dark = qMin(luminance(first), luminance(second));
        return (bright + 0.05) / (dark + 0.05);
    }

    QString foreground(const QColor& background, const QColor& preferred)
    {
        if (contrast(background, preferred) >= 4.5)
            return preferred.name();
        const QColor light("#FFFFFF");
        const QColor dark("#14213A");
        return contrast(background, light) > contrast(background, dark) ? light.name() : dark.name();
    }

    double cover_title_size(const QString& title, double width, double scale)
    {
        if (title.size() > 18)
            return 54;
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setBold(true);
        for (double size = 54; size >= 35; size -= 1)
        {
            font.setPixelSize(qMax(1, qRound(size * scale)));
            if (QFontMetricsF(font).horizontalAdvance(title) <= (width - 12) * scale)
                return size;
        }
        return 54;
    }

    double cover_word_size(const QString& word, double width, double scale)
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setBold(true);
        for (double size = 46; size >= 24; size -= 1)
        {
            font.setPixelSize(qMax(1, qRound(size * scale)));
            if (QFontMetricsF(font).horizontalAdvance(word) <= (width - 12) * scale)
                return size;
        }
        return 0;
    }

    double cover_summary_size(const QString& summary, double width, double scale)
    {
        if (summary.contains('\n'))
            return 27;
        QFont font(QStringLiteral("Microsoft YaHei"));
        for (double size = 27; size >= 18; size -= 1)
        {
            font.setPixelSize(qMax(1, qRound(size * scale)));
            if (QFontMetricsF(font).horizontalAdvance(summary) <= (width - 12) * scale)
                return size;
        }
        return 22;
    }

    QColor mix(const QColor& first, const QColor& second, double amount)
    {
        const double other = 1.0 - amount;
        return QColor::fromRgb(qRound(first.red() * other + second.red() * amount),
            qRound(first.green() * other + second.green() * amount),
            qRound(first.blue() * other + second.blue() * amount));
    }

    QJsonObject block_overflow(const QString& layout, int index, const QJsonObject& block)
    {
        const QString hint =
            QStringLiteral("Block %1 overflows %2. Split this block across pages or shorten it.")
                .arg(index + 1)
                .arg(layout);
        auto result = failure("page_text_overflow", hint);
        result.insert("layout", layout);
        result.insert("blockIndex", index);
        result.insert("textLength", block.value("text").toString().size());
        if (block.contains("example"))
            result.insert("exampleLength", block.value("example").toString().size());
        return result;
    }

    QString block_body(const QJsonObject& block)
    {
        const QString example = block.value("example").toString();
        if (example.isEmpty())
            return block.value("text").toString();
        return block.value("text").toString() + QStringLiteral("\n例：") + example;
    }

    QJsonObject field_overflow(const QString& field, const QString& value, const QString& hint)
    {
        auto result = failure("page_text_overflow", hint);
        result.insert("field", field);
        result.insert("textLength", value.size());
        return result;
    }

    QJsonObject page_field_error(
        const QString& code, const QString& field, const QString& hint, const QString& layout = {})
    {
        auto result = failure(code, hint);
        result.insert("field", field);
        if (!layout.isEmpty())
            result.insert("layout", layout);
        return result;
    }

    bool title_mentions(const QString& title, const QStringList& cues)
    {
        for (const auto& cue : cues)
            if (title.contains(cue, Qt::CaseInsensitive))
                return true;
        return false;
    }

    bool short_blocks(const QJsonArray& blocks, int maximum, int heading_maximum = 24)
    {
        for (const auto& value : blocks)
            if (value.toObject().value("text").toString().size() > maximum ||
                value.toObject().value("heading").toString().size() > heading_maximum)
                return false;
        return true;
    }

    QString automatic_layout(
        const QJsonObject& page, int number, const QJsonArray& blocks, const QString& style_id)
    {
        if (page.contains("code"))
            return "code";
        const int count = static_cast<int>(blocks.size());
        if (number == 1 && count <= 1)
            return "cover";
        if (page.contains("imagePath"))
            return "visual";
        const QString title = page.value("title").toString();
        if (count >= 2 && count <= 3 &&
            title_mentions(title,
                {QStringLiteral("误区"), QStringLiteral("误解"), QStringLiteral("谣言"),
                    QStringLiteral("迷思"), "myth", "fact check"}) &&
            short_blocks(blocks, 45, 18))
            return "factcheck";
        if (count == 2 &&
            title_mentions(title,
                {QStringLiteral("对比"), QStringLiteral("比较"), QStringLiteral("差异"),
                    QStringLiteral("区别"), QStringLiteral("利弊"), "versus"}) &&
            short_blocks(blocks, 110))
            return "comparison";
        if (count >= 2 &&
            title_mentions(title,
                {QStringLiteral("步骤"), QStringLiteral("流程"), QStringLiteral("阶段"),
                    QStringLiteral("路线"), QStringLiteral("路径"), "timeline", "roadmap"}) &&
            short_blocks(blocks, count == 2 ? 75 : 45))
            return count == 3 && short_blocks(blocks, 38, 12) ? "flow" : "steps";
        const QString subtitle = page.value("subtitle").toString();
        const int hub_limit = count == 4 ? 25 : count == 3 ? 45 : 75;
        if (count >= 2 && subtitle.size() <= 24 && !subtitle.trimmed().isEmpty() &&
            title_mentions(title,
                {QStringLiteral("关系"), QStringLiteral("构成"), QStringLiteral("体系"),
                    QStringLiteral("要素"), "relationships"}) &&
            short_blocks(blocks, hub_limit))
            return "hub";
        if (count == 1 &&
            title_mentions(title,
                {QStringLiteral("结论"), QStringLiteral("核心观点"), QStringLiteral("关键主张"),
                    "takeaway"}) &&
            short_blocks(blocks, 70))
            return "statement";
        if (count == 1)
            return "grid";
        if (count >= 3 && short_blocks(blocks, 110))
        {
            if (number % 2 != 0 || style_id == "editorial")
                return "grid";
            if (style_id == "research" || style_id == "natural")
                return "steps";
            if (style_id == "modern" && short_blocks(blocks, 38, 12))
                return "flow";
        }
        if (count >= 2)
            return number % 2 == 0 ? "columns" : "grid";
        return "grid";
    }

    class PageBuilder
    {
    public:
        PageBuilder(double width, double height, const QColor& ink, const QColor& paper, const QColor& accent,
            int number, const QString& style_id)
            : ink_(ink), paper_(paper), accent_(accent), number_(number), style_id_(style_id)
        {
            scale_ = qMin(width / 960.0, height / 540.0);
        }

        void shape(double x, double y, double width, double height, const QColor& color,
            const QString& geometry = "rect", double opacity = 1.0)
        {
            elements_.append(QJsonObject{{"type", "shape"}, {"geometry", geometry}, {"x", x * scale_},
                {"y", y * scale_}, {"width", width * scale_}, {"height", height * scale_},
                {"style",
                    QJsonObject{
                        {"fillColor", color.name()}, {"fillOpacity", opacity}, {"outlineWidth", 0}}}});
        }

        void image(const QString& path, double x, double y, double width, double height)
        {
            elements_.append(QJsonObject{{"type", "image"}, {"path", path}, {"x", x * scale_},
                {"y", y * scale_}, {"width", width * scale_}, {"height", height * scale_}});
        }

        bool text(const QString& value, double x, double y, double width, double height, double size,
            double minimum, bool bold, const QColor& background, const QColor& preferred,
            const QString& font_family = QStringLiteral("Microsoft YaHei"), bool preserve_lines = false)
        {
            if (value.isEmpty())
                return true;
            if (width < 16 || height < 16)
                return false;
            const double inset = height < 50 ? 4 : 12;
            QFont font(font_family);
            font.setBold(bold);
            double chosen = size;
            for (; chosen >= minimum; chosen -= 1.0)
            {
                font.setPixelSize(qMax(1, qRound(chosen * scale_)));
                const QRectF box(0, 0, (width - inset) * scale_, (height - inset) * scale_);
                const auto flags = preserve_lines ? Qt::TextExpandTabs : Qt::TextWordWrap;
                const auto bounds = QFontMetricsF(font).boundingRect(box, flags, value);
                if (bounds.width() <= box.width() && bounds.height() <= box.height())
                    break;
            }
            if (chosen < minimum)
                return false;
            elements_.append(QJsonObject{{"type", "text"}, {"text", value}, {"x", x * scale_},
                {"y", y * scale_}, {"width", width * scale_}, {"height", height * scale_},
                {"style",
                    QJsonObject{{"fontFamily", font_family}, {"fontSize", chosen * scale_}, {"bold", bold},
                        {"textColor", foreground(background, preferred)}}}});
            return true;
        }

        bool header(
            const QString& title, const QString& subtitle, const QString& eyebrow, QString& overflowing_field)
        {
            if (style_id_ == "editorial")
                shape(26, 30, 7, 125, accent_);
            else if (style_id_ == "natural")
                shape(876, 3, 78, 78, accent_, "ellipse", 0.13);
            else if (style_id_ == "research")
                shape(0, 0, 960, 8, ink_);
            if (!text(title, 58, 29, 685, 85, 36, 26, true, paper_, ink_))
            {
                overflowing_field = "title";
                return false;
            }
            shape(58, 118, 72, 5, accent_);
            shape(833, 52, 72, 5, accent_);
            const QString marker =
                eyebrow.isEmpty() ? QStringLiteral("专题 / %1").arg(number_, 2, 10, QChar('0')) : eyebrow;
            if (!text(marker, 764, 74, 140, 35, 15, 13, true, paper_, accent_))
            {
                overflowing_field = "eyebrow";
                return false;
            }
            if (!subtitle.isEmpty() && !text(subtitle, 58, 126, 804, 45, 21, 17, false, paper_, ink_))
            {
                overflowing_field = "subtitle";
                return false;
            }
            return true;
        }

        bool card(const QJsonObject& block, int index, double x, double y, double width, double height,
            const QColor& surface, bool numbered, bool prominent = false)
        {
            shape(x, y, width, height, surface);
            shape(x, y, prominent ? 8 : 5, height, accent_);
            const QString heading = block.value("heading").toString();
            const QString example = block.value("example").toString();
            const QColor ink = prominent ? QColor("#FFFFFF") : ink_;
            const bool compact = height < 110;
            const double heading_height = heading.isEmpty() ? 0 : (compact ? 29 : 48);
            if (numbered)
            {
                shape(x + 24, y + (compact ? 13 : 20), 38, 38, accent_, "ellipse");
                const QString index_text = QString::number(index + 1);
                if (!text(index_text, x + 28, y + (compact ? 15 : 22), 34, 34, 16, 14, true, accent_,
                        QColor("#FFFFFF")))
                    return false;
            }
            const double text_x = x + (numbered ? 76 : 24);
            const double text_width = width - (numbered ? 92 : 44);
            if (!heading.isEmpty() &&
                !text(heading, text_x, y + (compact ? 7 : 16), text_width, heading_height, compact ? 20 : 25,
                    16, true, surface, ink))
                return false;
            const double body_y = compact ? (heading.isEmpty() ? 12 : 37) : (heading.isEmpty() ? 24 : 64);
            const bool narrow_card = width < 300 && !compact && !numbered;
            const bool separate_example = narrow_card && !example.isEmpty();
            const QString body = separate_example ? block.value("text").toString() : block_body(block);
            double body_bottom = heading.isEmpty() ? 19 : 18;
            if (compact)
                body_bottom = 9;
            if (narrow_card)
                body_bottom = separate_example ? 123 : 72;
            double body_size = 18;
            if (narrow_card)
            {
                body_size = body.size() <= 24 ? 25 : body.size() <= 38 ? 23 : 21;
            }
            else if (!compact)
            {
                body_size = body.size() <= 24 ? 35 : body.size() <= 48 ? 29 : 22;
            }
            if (!text(body, text_x, y + body_y, text_width, height - body_y - body_bottom, body_size,
                    narrow_card ? 18 : 14, false, surface, ink))
                return false;
            if (narrow_card)
            {
                if (separate_example)
                {
                    const QColor inset = mix(surface, accent_, 0.18);
                    shape(text_x, y + height - 111, text_width, 94, inset);
                    shape(text_x, y + height - 111, 4, 94, accent_);
                    if (!text(QStringLiteral("例 / %1").arg(index + 1, 2, 10, QChar('0')), text_x + 13,
                            y + height - 102, text_width - 26, 24, 16, 14, true, inset, accent_) ||
                        !text(example, text_x + 13, y + height - 75, text_width - 26, 64, 18, 13, false,
                            inset, ink))
                        return false;
                }
                else
                {
                    shape(text_x, y + height - 61, text_width, 2, mix(surface, accent_, 0.55));
                    if (!text(QStringLiteral("%1").arg(index + 1, 2, 10, QChar('0')), text_x, y + height - 53,
                            58, 42, 25, 25, true, surface, accent_))
                        return false;
                }
            }
            return true;
        }

        bool editorial(const QJsonObject& block, int index, double x, double y, double width, double height,
            bool lead = false)
        {
            const bool compact = height < 185;
            const QString heading = block.value("heading").toString();
            const QString body = block_body(block);
            shape(x, y, lead ? 84 : 54, 5, accent_);
            const double number_size = lead ? 50 : compact ? 25 : 37;
            const double number_height = lead ? 70 : compact ? 36 : 53;
            const double heading_x = x + (lead ? 110 : compact ? 75 : 90);
            const double heading_height = lead ? 58 : compact ? 38 : 49;
            const double body_top = lead ? 108 : compact ? 52 : 81;
            double body_size = body.size() <= 45 ? 28 : 23;
            if (lead)
                body_size = body.size() <= 45 ? 32 : 27;
            else if (compact)
                body_size = body.size() <= 45 ? 21 : 18;
            const double heading_size = lead ? 31 : compact ? 22 : 27;
            const double minimum_body_size = compact ? 14 : lead ? 18 : 16;
            if (!text(QStringLiteral("%1").arg(index + 1, 2, 10, QChar('0')), x, y + 11, heading_x - x - 8,
                    number_height, number_size, compact ? 22 : 31, true, paper_, accent_))
                return false;
            if (!text(heading, heading_x, y + (compact ? 9 : 17), width - (heading_x - x), heading_height,
                    heading_size, compact ? 16 : 19, true, paper_, ink_))
                return false;
            return text(body, x, y + body_top, width, height - body_top - 8, body_size, minimum_body_size,
                false, paper_, ink_);
        }

        QJsonObject result(const QColor& background) const
        {
            return {{"ok", true}, {"elements", elements_}, {"background", background.name()}};
        }

    private:
        double scale_;
        QColor ink_;
        QColor paper_;
        QColor accent_;
        int number_;
        QString style_id_;
        QJsonArray elements_;
    };
}

namespace mirrorfly
{
    QJsonObject office_ai_page_recipe(const QJsonObject& page, const QJsonObject& theme, double width,
        double height, int number, const QSize& image_size)
    {
        if (!std::isfinite(width) || !std::isfinite(height) || width < 100 || height < 100 || width > 20000 ||
            height > 20000)
            return failure("invalid_slide_size", "Read the current slide dimensions first.");
        for (const auto& key : page.keys())
            if (!QStringList{"title", "subtitle", "eyebrow", "layout", "blocks", "imagePath", "imageCredit",
                    "code", "coverWord"}
                    .contains(key))
                return failure("invalid_page_field", key);
        const auto title = page.value("title").toString();
        const auto subtitle = page.value("subtitle").toString();
        const auto eyebrow = page.value("eyebrow").toString();
        const auto blocks = page.value("blocks").toArray();
        const auto image_path = page.value("imagePath").toString();
        const auto image_credit = page.value("imageCredit").toString();
        const auto code = page.value("code").toString();
        const auto cover_word = page.value("coverWord").toString().trimmed();
        const QString style_id = theme.value("styleId").toString("modern");
        const auto layout = page.contains("layout") ? page.value("layout").toString()
                                                    : automatic_layout(page, number, blocks, style_id);
        if (!page.value("title").isString() || title.trimmed().isEmpty() || title.size() > 100)
            return page_field_error(
                "invalid_page", "title", "Use a nonempty title of at most 100 characters.");
        if ((page.contains("subtitle") && !page.value("subtitle").isString()) || subtitle.size() > 180)
            return page_field_error("invalid_page", "subtitle", "Use a subtitle of at most 180 characters.");
        if ((page.contains("eyebrow") && !page.value("eyebrow").isString()) || eyebrow.size() > 40)
            return page_field_error("invalid_page", "eyebrow", "Use an eyebrow of at most 40 characters.");
        if ((page.contains("layout") && !page.value("layout").isString()) ||
            !QStringList{"cover", "visual", "grid", "columns", "steps", "flow", "comparison", "statement",
                "hub", "code", "factcheck"}
                .contains(layout))
            return page_field_error("invalid_page", "layout",
                "Use cover, visual, grid, columns, steps, flow, comparison, statement, hub, code or "
                "factcheck.");
        if (layout == "code" &&
            (!page.value("code").isString() || code.trimmed().isEmpty() || code.size() > 600))
            return page_field_error("invalid_page", "code",
                "Code layout needs complete source code of at most 600 characters; split longer programs.",
                layout);
        if (layout != "code" && page.contains("code"))
            return page_field_error("invalid_page", "code", "Use code only with the code layout.", layout);
        if (page.contains("imagePath") &&
            (!page.value("imagePath").isString() || image_path.isEmpty() ||
                (layout != "cover" && layout != "visual") || !image_size.isValid()))
            return page_field_error("invalid_page_image", "imagePath",
                "Only cover and visual layouts may use an existing validated local image path.", layout);
        if (layout == "visual" && image_path.isEmpty())
            return page_field_error("invalid_page_image", "imagePath",
                "Visual layout needs an existing validated local image path.", layout);
        if (page.contains("imageCredit") &&
            (!page.value("imageCredit").isString() || image_credit.size() > 140 || image_path.isEmpty()))
            return page_field_error("invalid_page_image", "imageCredit", "Invalid image source credit.");
        if (page.contains("coverWord") &&
            (!page.value("coverWord").isString() || cover_word.isEmpty() || cover_word.size() > 8 ||
                layout != "cover"))
            return page_field_error("invalid_page", "coverWord",
                "Use a short coverWord of at most 8 characters on a cover; imagePath takes precedence.",
                layout);
        const bool missing_blocks = !page.contains("blocks");
        if ((!missing_blocks && !page.value("blocks").isArray()) ||
            (missing_blocks && layout != "cover" && layout != "statement" && layout != "code"))
            return page_field_error(
                "invalid_page", "blocks", "Provide a blocks array for content pages.", layout);
        const bool invalid_count = blocks.size() > 4 ||
            ((layout == "grid" || layout == "columns" || layout == "steps") && blocks.isEmpty()) ||
            (layout == "flow" && (blocks.size() < 2 || blocks.size() > 4)) ||
            (layout == "code" && blocks.size() > 2) ||
            (layout == "factcheck" && (blocks.size() < 2 || blocks.size() > 3)) ||
            (layout == "visual" && (blocks.isEmpty() || blocks.size() > 2)) ||
            ((layout == "cover" || layout == "statement") && blocks.size() > 1) ||
            (layout == "statement" && blocks.isEmpty() && subtitle.trimmed().isEmpty()) ||
            (layout == "comparison" && blocks.size() != 2) || (layout == "hub" && blocks.size() < 2);
        if (invalid_count)
        {
            auto result = page_field_error("invalid_page", "blocks",
                "Grid, columns and steps need 1..4 blocks; cover needs 0..1; statement needs 0..1 "
                "and a subtitle when empty; flow and hub need 2..4; comparison needs exactly 2; "
                "visual needs 1..2 short blocks; code accepts 0..2 short explanation blocks; "
                "factcheck needs 2..3 claims.",
                layout);
            result.insert("blockCount", blocks.size());
            return result;
        }
        if (layout == "hub" && (subtitle.trimmed().isEmpty() || subtitle.size() > 24))
            return page_field_error("invalid_page", "subtitle",
                "Hub needs a short central idea in subtitle, at most 24 characters.", layout);
        for (int index = 0; index < blocks.size(); ++index)
        {
            const auto value = blocks.at(index);
            const auto block = value.toObject();
            if (!value.isObject() || !block.value("text").isString() ||
                block.value("text").toString().trimmed().isEmpty() ||
                (block.contains("heading") && !block.value("heading").isString()) ||
                block.value("text").toString().size() > 600 ||
                block.value("heading").toString().size() > 60 ||
                (block.contains("example") &&
                    (!block.value("example").isString() ||
                        block.value("example").toString().trimmed().isEmpty() ||
                        block.value("example").toString().size() > 64)))
            {
                auto result = page_field_error("invalid_block", "blocks",
                    "Each content block needs nonempty text up to 600 characters and an optional "
                    "heading up to 60 and a short optional example up to 64; long examples may overflow.",
                    layout);
                result.insert("blockIndex", index);
                return result;
            }
            if (layout == "factcheck" && block.value("heading").toString().trimmed().isEmpty())
            {
                auto result = page_field_error("invalid_block", "blocks",
                    "Each factcheck block needs the claim in heading and the correction in text.", layout);
                result.insert("blockIndex", index);
                return result;
            }
            for (const auto& key : block.keys())
                if (key != "text" && key != "heading" && key != "example")
                {
                    auto result = page_field_error("invalid_block_field", key,
                        "Use only text, heading and example in each block.", layout);
                    result.insert("blockIndex", index);
                    return result;
                }
        }
        const QColor ink(theme.value("ink").toString());
        const QColor paper(theme.value("paper").toString());
        const QColor accent(theme.value("accent").toString());
        if (!ink.isValid() || !paper.isValid() || !accent.isValid())
            return failure("invalid_theme", "Use three valid #RRGGBB theme colors.");
        const QColor canvas = number % 3 == 0 ? mix(paper, accent, 0.06) : paper;
        PageBuilder builder(width, height, ink, canvas, accent, number, style_id);
        if (layout == "cover")
        {
            const bool has_image = !image_path.isEmpty();
            if (has_image)
            {
                builder.shape(621, 130, 281, 298, accent);
                const double image_scale = qMin(273.0 / image_size.width(), 290.0 / image_size.height());
                const double image_width = image_size.width() * image_scale;
                const double image_height = image_size.height() * image_scale;
                builder.image(image_path, 625 + (273 - image_width) / 2, 134 + (290 - image_height) / 2,
                    image_width, image_height);
            }
            else
            {
                QString visual_word = cover_word;
                if (visual_word.isEmpty() &&
                    (title.contains("C++", Qt::CaseInsensitive) ||
                        eyebrow.contains("C++", Qt::CaseInsensitive)))
                    visual_word = QStringLiteral("C++");
                if (visual_word.isEmpty())
                {
                    for (const QChar character : title)
                    {
                        if (character.isLetterOrNumber())
                            visual_word.append(character);
                        if (visual_word.size() == 2)
                            break;
                    }
                }
                if (visual_word.isEmpty())
                    visual_word = QStringLiteral("01");
                const QColor panel = mix(ink, accent, style_id == "natural" ? 0.24 : 0.42);
                builder.shape(704, 0, 256, 540, panel);
                builder.shape(704, 0, 8, 540, accent);
                if (style_id == "editorial")
                {
                    builder.shape(741, 105, 164, 5, accent);
                    builder.shape(741, 350, 164, 2, paper, "rect", 0.65);
                    builder.shape(741, 385, 82, 5, accent);
                }
                else if (style_id == "research")
                {
                    builder.shape(737, 129, 186, 186, ink);
                    builder.shape(748, 140, 164, 164, accent);
                    builder.shape(737, 358, 186, 3, paper);
                }
                else
                {
                    builder.shape(729, 112, 206, 206, accent, "ellipse");
                    builder.shape(747, 130, 170, 170, ink, "ellipse");
                    builder.shape(736, 122, 24, 24, paper, "ellipse", 0.85);
                    builder.shape(906, 284, 16, 16, paper, "ellipse", 0.80);
                    builder.shape(744, 350, 176, 2, paper, "rect", 0.75);
                    builder.shape(744, 378, 122, 3, accent, "rect", 0.95);
                }
                const double word_size =
                    cover_word_size(visual_word, 148, qMin(width / 960.0, height / 540.0));
                if (word_size == 0 ||
                    !builder.text(visual_word, 758, 172, 148, 87, word_size, word_size, true, ink, paper,
                        QStringLiteral("Microsoft YaHei"), true))
                    return field_overflow(
                        "coverWord", visual_word, "Use a shorter coverWord; the title can stay as written.");
                builder.text(QStringLiteral("01"), 746, 402, 70, 31, 20, 17, true, panel, paper);
            }
            const double title_width = has_image ? 535 : 620;
            const double title_size =
                cover_title_size(title, title_width, qMin(width / 960.0, height / 540.0));
            if (!builder.text(title, 58, 155, title_width, 158, title_size, 35, true, ink, QColor("#FFFFFF")))
                return field_overflow("title", title, "Shorten the cover title; keep the summary separate.");
            builder.shape(56, 95, 106, 6, accent);
            builder.shape(58, 472, 842, 2, accent);
            const QString block_heading =
                blocks.isEmpty() ? QString() : blocks.first().toObject().value("heading").toString();
            const QString label = eyebrow.isEmpty()
                ? (block_heading.isEmpty() ? QStringLiteral("专题演示 / %1").arg(number) : block_heading)
                : eyebrow;
            if (!builder.text(label, 58, 64, 600, 32, 18, 15, true, ink, accent))
                return field_overflow(eyebrow.isEmpty() ? "blocks[0].heading" : "eyebrow",
                    eyebrow.isEmpty() ? block_heading : eyebrow, "Shorten the cover label.");
            const QString block_text = blocks.isEmpty() ? QString() : block_body(blocks.first().toObject());
            QStringList summary_parts;
            if (!subtitle.isEmpty())
                summary_parts.append(subtitle);
            if (!eyebrow.isEmpty() && !block_heading.isEmpty())
                summary_parts.append(block_heading);
            if (!block_text.isEmpty())
                summary_parts.append(block_text);
            const QString summary = summary_parts.join(QStringLiteral("\n"));
            const double summary_width = has_image ? 535 : 615;
            const double summary_size =
                cover_summary_size(summary, summary_width, qMin(width / 960.0, height / 540.0));
            if (!builder.text(
                    summary, 58, 335, summary_width, 118, summary_size, 18, false, ink, QColor("#FFFFFF")))
                return field_overflow(
                    "coverSummary", summary, "Shorten the cover summary; move details to another page.");
            if (!image_credit.isEmpty())
                builder.text(image_credit, 58, 507, 844, 25, 11, 8, false, ink, paper);
            return builder.result(ink);
        }
        const int count = static_cast<int>(blocks.size());
        const QString header_subtitle =
            (layout == "statement" && count == 0) || layout == "hub" ? QString() : subtitle;
        QString overflowing_field;
        if (!builder.header(title, header_subtitle, eyebrow, overflowing_field))
        {
            QString value = header_subtitle;
            if (overflowing_field == "title")
                value = title;
            else if (overflowing_field == "eyebrow")
                value = eyebrow;
            return field_overflow(overflowing_field, value,
                QStringLiteral("Shorten the %1; other page content is unchanged.").arg(overflowing_field));
        }
        const double top = header_subtitle.isEmpty() ? 150 : 183;
        const double bottom = 44;
        const double available = 540 - top - bottom;
        const QColor surface = canvas.lightnessF() < 0.5 ? canvas.lighter(145) : QColor("#FFFFFF");
        const QColor soft = mix(canvas, accent, 0.11);
        if (layout == "visual")
        {
            builder.shape(58, top, 500, available, soft);
            builder.shape(58, top, 5, available, accent);
            const double image_scale =
                qMin(492.0 / image_size.width(), (available - 8) / image_size.height());
            const double image_width = image_size.width() * image_scale;
            const double image_height = image_size.height() * image_scale;
            builder.image(image_path, 62 + (492 - image_width) / 2, top + (available - image_height) / 2,
                image_width, image_height);
            const double gap = 14;
            const double card_height = (available - (count - 1) * gap) / count;
            for (int index = 0; index < count; ++index)
            {
                const auto block = blocks.at(index).toObject();
                if (!builder.card(block, index, 584, top + index * (card_height + gap), 318, card_height,
                        index == 0 ? surface : soft, false))
                    return block_overflow(layout, index, block);
            }
            if (!image_credit.isEmpty())
                builder.text(image_credit, 58, 507, 844, 25, 11, 8, false, canvas, ink);
            return builder.result(canvas);
        }
        if (layout == "hub")
        {
            if (count == 4)
            {
                const double gap = 14;
                const double card_height = (available - gap) / 2;
                const double center_y = top + (available - 196) / 2;
                for (int index = 0; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    const bool left = index % 2 == 0;
                    const double x = left ? 58 : 616;
                    const double y = top + (index / 2) * (card_height + gap);
                    const double middle = y + card_height / 2;
                    const QColor panel = index % 3 == 0 ? soft : surface;
                    builder.shape(x, y, 286, card_height, panel);
                    builder.shape(x, y, 6, card_height, accent);
                    builder.shape(left ? 344 : 578, middle - 1, 38, 2, mix(canvas, accent, 0.65));
                    builder.shape(left ? 371 : 578, middle - 5, 10, 10, accent, "ellipse");
                    builder.shape(x + 18, y + 17, 32, 32, accent, "ellipse");
                    if (!builder.text(QString::number(index + 1), x + 21, y + 19, 26, 28, 16, 14, true,
                            accent, QColor("#FFFFFF")) ||
                        !builder.text(block.value("heading").toString(), x + 59, y + 16, 208, 40, 23, 16,
                            true, panel, ink) ||
                        !builder.text(block_body(block), x + 20, y + 65, 246, card_height - 77, 20, 14, false,
                            panel, ink))
                        return block_overflow(layout, index, block);
                }
                builder.shape(382, center_y, 196, 196, accent, "ellipse");
                builder.shape(398, center_y + 16, 164, 164, ink, "ellipse");
                if (!builder.text(subtitle, 412, center_y + 39, 136, 118, subtitle.size() > 8 ? 22 : 26, 17,
                        true, ink, QColor("#FFFFFF")))
                    return field_overflow("subtitle", subtitle,
                        "Shorten the central idea; keep supporting details in the blocks.");
                return builder.result(canvas);
            }
            const double circle_y = top + (available - 252) / 2;
            builder.shape(73, circle_y, 252, 252, accent, "ellipse");
            builder.shape(94, circle_y + 21, 210, 210, ink, "ellipse");
            builder.shape(129, circle_y + 56, 140, 140, mix(ink, accent, 0.19), "ellipse");
            if (!builder.text(subtitle, 116, circle_y + 72, 166, 113, 29, 18, true, ink, QColor("#FFFFFF")))
                return field_overflow(
                    "subtitle", subtitle, "Shorten the central idea; keep supporting details in the blocks.");
            const double gap = count == 4 ? 6 : 10;
            const double row_height = (available - (count - 1) * gap) / count;
            for (int index = 0; index < count; ++index)
            {
                const auto block = blocks.at(index).toObject();
                const double y = top + index * (row_height + gap);
                const double middle = y + row_height / 2;
                builder.shape(325, middle - 1, 59, 2, mix(canvas, accent, 0.56));
                builder.shape(380, middle - 5, 10, 10, accent, "ellipse");
                builder.shape(406, y, 496, row_height, index % 2 == 0 ? surface : soft);
                builder.shape(406, y, 6, row_height, accent);
                builder.shape(428, y + 15, 40, 40, accent, "ellipse");
                builder.shape(438, y + 25, 20, 20, ink, "ellipse");
                const QString heading = block.value("heading").toString();
                const QString body = block_body(block);
                const double heading_height = heading.isEmpty() ? 0 : 33;
                if (!builder.text(heading, 482, y + 9, 395, heading_height, 22, 16, true,
                        index % 2 == 0 ? surface : soft, ink) ||
                    !builder.text(body, 482, y + (heading.isEmpty() ? 13 : 43), 395,
                        row_height - (heading.isEmpty() ? 22 : 49), count == 4 ? 17 : 20, 14, false,
                        index % 2 == 0 ? surface : soft, ink))
                    return block_overflow(layout, index, block);
            }
            return builder.result(canvas);
        }
        if (layout == "statement")
        {
            const QString statement = count == 0 ? subtitle : block_body(blocks.first().toObject());
            const QString heading =
                count == 0 ? QString() : blocks.first().toObject().value("heading").toString();
            builder.shape(58, top, 844, available, ink);
            builder.shape(58, top, 12, available, accent);
            builder.shape(814, top + available - 80, 55, 55, accent, "ellipse", 0.65);
            if (!builder.text(heading, 105, top + 27, 675, 42, 23, 17, true, ink, accent))
                return field_overflow("blocks[0].heading", heading, "Shorten the statement heading.");
            const bool short_statement = statement.size() <= 24;
            const double statement_y =
                top + (heading.isEmpty() ? (short_statement ? available * 0.33 : 46) : 80);
            const double statement_height = heading.isEmpty() && short_statement
                ? available * 0.48
                : available - (heading.isEmpty() ? 90 : 120);
            const double statement_size = statement.size() > 16 ? 32 : 42;
            if (!builder.text(statement, 105, statement_y, 750, statement_height, statement_size, 28, true,
                    ink, QColor("#FFFFFF")))
                return count == 0
                    ? field_overflow("subtitle", statement, "Shorten the statement or split its details.")
                    : block_overflow(layout, 0, blocks.first().toObject());
            return builder.result(canvas);
        }
        if (layout == "code")
        {
            const double code_height = count == 0 ? available : available - 105;
            builder.shape(58, top, 844, code_height, ink);
            builder.shape(58, top, 844, 5, accent);
            if (!builder.text(QStringLiteral("SOURCE"), 77, top + 15, 806, 29, 15, 13, true, ink, accent) ||
                !builder.text(code, 77, top + 55, 806, code_height - 70, 22, 14, false, ink,
                    QColor("#FFFFFF"), QStringLiteral("Consolas"), true))
                return field_overflow(
                    "code", code, "Use fewer lines or split the complete program across code pages.");
            if (count > 0)
            {
                const double gap = 12;
                const double note_width = (844 - (count - 1) * gap) / count;
                for (int index = 0; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    if (!builder.card(block, index, 58 + index * (note_width + gap), top + code_height + 13,
                            note_width, 92, index == 0 ? surface : soft, false))
                        return block_overflow(layout, index, block);
                }
            }
            return builder.result(canvas);
        }
        if (count == 0)
            return builder.result(canvas);
        if (layout == "factcheck")
        {
            const double gap = 11;
            const double row_height = (available - (count - 1) * gap) / count;
            for (int index = 0; index < count; ++index)
            {
                const auto block = blocks.at(index).toObject();
                const double y = top + index * (row_height + gap);
                const QColor claim = mix(ink, accent, index == 0 ? 0.08 : 0.18);
                const QColor response = index % 2 == 0 ? surface : soft;
                const QString example = block.value("example").toString();
                builder.shape(58, y, 275, row_height, claim);
                builder.shape(58, y, 6, row_height, accent);
                builder.shape(366, y, 536, row_height, response);
                builder.shape(366, y, 5, row_height, accent);
                builder.shape(339, y + row_height / 2 - 13, 21, 26, accent, "rightArrow");
                if (!builder.text(QStringLiteral("说法 %1").arg(index + 1), 78, y + 8, 235, 23, 15, 13, true,
                        claim, accent) ||
                    !builder.text(block.value("heading").toString(), 78, y + 30, 235, row_height - 39, 22, 16,
                        true, claim, QColor("#FFFFFF")) ||
                    !builder.text(
                        QStringLiteral("实际情况"), 387, y + 6, 486, 25, 17, 14, true, response, accent) ||
                    !builder.text(block.value("text").toString(), 387, y + 32, 486,
                        row_height - (example.isEmpty() ? 38 : 65), count == 2 ? 22 : 19, 15, false, response,
                        ink))
                    return block_overflow(layout, index, block);
                if (!example.isEmpty() &&
                    !builder.text(QStringLiteral("例 / ") + example, 387, y + row_height - 30, 486, 25, 16,
                        13, false, response, ink))
                    return block_overflow(layout, index, block);
            }
            return builder.result(canvas);
        }
        if (layout == "flow")
        {
            const double gap = count == 4 ? 22 : 30;
            const double card_width = (844 - (count - 1) * gap) / count;
            const double card_y = top + 47;
            const double card_height = available - 47;
            for (int index = 0; index < count; ++index)
            {
                const auto block = blocks.at(index).toObject();
                const double x = 58 + index * (card_width + gap);
                const QColor panel = index == 0 ? ink : index % 2 == 0 ? surface : soft;
                const QColor body_color = index == 0 ? QColor("#FFFFFF") : ink;
                builder.shape(x, card_y, card_width, card_height, panel);
                builder.shape(x, card_y, card_width, 5, accent);
                builder.shape(x + card_width / 2 - 37, top + 4, 74, 74, index == 0 ? ink : accent, "hexagon");
                if (index + 1 < count)
                    builder.shape(x + card_width + 2, top + 31, gap - 4, 16, accent, "rightArrow", 0.85);
                if (!builder.text(QString::number(index + 1), x + card_width / 2 - 20, top + 21, 40, 40, 24,
                        20, true, index == 0 ? ink : accent, QColor("#FFFFFF")))
                    return block_overflow(layout, index, block);
                const QString heading = block.value("heading").toString();
                const QString body = block.value("text").toString();
                const QString example = block.value("example").toString();
                const double text_x = x + 19;
                const double text_width = card_width - 38;
                const double heading_y = top + 91;
                if (!builder.text(
                        heading, text_x, heading_y, text_width, 51, 25, 17, true, panel, body_color))
                    return block_overflow(layout, index, block);
                const double body_y = heading.isEmpty() ? top + 98 : top + 150;
                const double example_space = example.isEmpty() ? 0 : 83;
                if (!builder.text(body, text_x, body_y, text_width,
                        top + available - body_y - 18 - example_space, body.size() <= 30 ? 22 : 19, 15, false,
                        panel, body_color))
                    return block_overflow(layout, index, block);
                if (!example.isEmpty())
                {
                    const QColor inset = mix(panel, accent, index == 0 ? 0.16 : 0.12);
                    builder.shape(text_x, top + available - 91, text_width, 73, inset);
                    builder.shape(text_x, top + available - 91, 4, 73, accent);
                    if (!builder.text(QStringLiteral("例"), text_x + 11, top + available - 87,
                            text_width - 22, 21, 15, 13, true, inset, body_color) ||
                        !builder.text(example, text_x + 11, top + available - 64, text_width - 22, 48, 17, 13,
                            false, inset, body_color))
                        return block_overflow(layout, index, block);
                }
            }
            return builder.result(canvas);
        }
        if (layout == "steps")
        {
            if (count >= 3 && theme.contains("styleId") && style_id == "natural")
            {
                const double gap = 16;
                const double width_each = (844 - (count - 1) * gap) / count;
                for (int index = 0; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    const double x = 58 + index * (width_each + gap);
                    const double offset = index % 2 == 0 ? 0 : 44;
                    const double y = top + offset;
                    if (index + 1 < count)
                    {
                        builder.shape(x + width_each / 2 + 23, top + 23, width_each + gap - 46, 2, accent,
                            "rect", 0.32);
                    }
                    if (offset > 0)
                        builder.shape(x + width_each / 2 - 1, top + 23, 2, offset, accent, "rect", 0.32);
                    builder.shape(x + width_each / 2 - 23, y, 46, 46, mix(canvas, accent, 0.28), "ellipse");
                    if (!builder.text(QString::number(index + 1), x + width_each / 2 - 15, y + 7, 30, 32, 20,
                            16, true, mix(canvas, accent, 0.28), ink) ||
                        !builder.text(block.value("heading").toString(), x, y + 62, width_each, 50, 25, 18,
                            true, canvas, ink) ||
                        !builder.text(block_body(block), x, y + 121, width_each, available - offset - 139, 22,
                            15, false, canvas, ink))
                        return block_overflow(layout, index, block);
                }
                return builder.result(canvas);
            }
            if (count == 4)
            {
                const double row_gap = 24;
                const double row_height = (available - row_gap) / 2;
                const double positions[][2]{{58, top}, {498, top}, {498, top + row_height + row_gap},
                    {58, top + row_height + row_gap}};
                builder.shape(464, top + 18, 29, 16, accent, "rightArrow", 0.72);
                builder.shape(464, top + row_height + row_gap + 18, 29, 16, accent, "leftArrow", 0.72);
                builder.shape(883, top + row_height - 3, 3, row_gap + 6, accent, "rect", 0.55);
                for (int index = 0; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    const double x = positions[index][0];
                    const double y = positions[index][1];
                    const QString heading = block.value("heading").toString();
                    const QString body = block_body(block);
                    builder.shape(x, y + 3, 46, 46, index == 0 ? ink : accent, "ellipse");
                    if (!builder.text(QString::number(index + 1), x + 6, y + 8, 34, 34, 19, 17, true,
                            index == 0 ? ink : accent, QColor("#FFFFFF")) ||
                        !builder.text(heading, x + 68, y, 322, 46, 25, 17, true, canvas, ink) ||
                        !builder.text(body, x + 68, y + (heading.isEmpty() ? 4 : 47), 322,
                            row_height - (heading.isEmpty() ? 12 : 51), body.size() <= 48 ? 21 : 18, 14,
                            false, canvas, ink))
                        return block_overflow(layout, index, block);
                    builder.shape(x + 68, y + row_height - 4, 322, 3,
                        index == count - 1 ? accent : mix(canvas, accent, 0.42));
                }
                return builder.result(canvas);
            }
            const double gap = 11;
            const double height_each = (available - (count - 1) * gap) / count;
            builder.shape(75, top + 19, 3, available - 38, accent, "rect", 0.48);
            for (int index = 0; index < count; ++index)
            {
                const auto block = blocks.at(index).toObject();
                const double y = top + index * (height_each + gap);
                builder.shape(57, y + height_each / 2 - 20, 40, 40, accent, "ellipse");
                builder.text(QString::number(index + 1), 62, y + height_each / 2 - 17, 31, 34, 16, 14, true,
                    accent, QColor("#FFFFFF"));
                if (!builder.card(
                        block, index, 111, y, 791, height_each, index % 2 == 0 ? surface : soft, false))
                    return block_overflow(layout, index, block);
            }
            return builder.result(canvas);
        }
        if (layout == "comparison")
        {
            const auto first = blocks.at(0).toObject();
            const auto second = blocks.at(1).toObject();
            if (!builder.card(first, 0, 58, top, 410, available, ink, true, true))
                return block_overflow(layout, 0, first);
            if (!builder.card(second, 1, 492, top, 410, available, soft, true))
                return block_overflow(layout, 1, second);
            builder.shape(479, top + 28, 2, available - 56, mix(canvas, accent, 0.44));
            return builder.result(canvas);
        }
        if (layout == "grid")
        {
            // The same parallel facts get a style-specific structure, while every supplied word is kept.
            if (count >= 3 && theme.contains("styleId") && style_id == "research")
            {
                const double row = available / count;
                builder.shape(58, top, 844, 2, ink);
                for (int index = 0; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    const double y = top + index * row;
                    builder.shape(58, y + row - 2, 844, 1, mix(canvas, ink, 0.25));
                    builder.shape(303, y + 12, 1, row - 24, mix(canvas, accent, 0.35));
                    if (!builder.text(
                            QString::number(index + 1), 58, y + 16, 40, 32, 20, 16, true, canvas, accent) ||
                        !builder.text(block.value("heading").toString(), 112, y + 16, 177, row - 25, 22, 16,
                            true, canvas, ink) ||
                        !builder.text(
                            block_body(block), 327, y + 16, 575, row - 25, 21, 15, false, canvas, ink))
                        return block_overflow(layout, index, block);
                }
                return builder.result(canvas);
            }
            if (count >= 3 && theme.contains("styleId") && style_id == "modern")
            {
                const bool lead_right = number % 4 == 1;
                const double lead_x = lead_right ? 551 : 58;
                const double support_x = lead_right ? 58 : 445;
                builder.shape(lead_x, top, 351, available, ink);
                builder.shape(lead_x, top, 351, 5, accent);
                const auto first = blocks.first().toObject();
                if (!builder.text(first.value("heading").toString(), lead_x + 24, top + 24, 303, 68, 31, 22,
                        true, ink, QColor("#FFFFFF")) ||
                    !builder.text(block_body(first), lead_x + 24, top + 104, 303, available - 127, 25, 17,
                        false, ink, QColor("#FFFFFF")))
                    return block_overflow(layout, 0, first);
                const double row = available / (count - 1);
                for (int index = 1; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    const double y = top + (index - 1) * row;
                    builder.shape(support_x, y + 11, 22, 22, accent);
                    builder.shape(support_x + 36, y + row - 6, 421, 2, mix(canvas, accent, 0.4));
                    if (!builder.text(block.value("heading").toString(), support_x + 36, y + 4, 421, 44, 25,
                            18, true, canvas, ink) ||
                        !builder.text(block_body(block), support_x + 36, y + 55, 421, row - 65, 23, 15, false,
                            canvas, ink))
                        return block_overflow(layout, index, block);
                }
                return builder.result(canvas);
            }
            if (count >= 3 && theme.contains("styleId") && style_id == "natural")
            {
                const double row = available / count;
                builder.shape(85, top + 18, 2, available - 36, accent, "rect", 0.4);
                for (int index = 0; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    const double y = top + index * row;
                    const double offset = (index + number) % 2 == 0 ? 0 : 92;
                    builder.shape(65, y + 19, 42, 42, mix(canvas, accent, 0.3), "ellipse");
                    builder.shape(127 + offset, y + row - 9, 680 - offset, 2, mix(canvas, accent, 0.35));
                    if (!builder.text(block.value("heading").toString(), 127 + offset, y + 8, 720 - offset,
                            44, 26, 18, true, canvas, ink) ||
                        !builder.text(block_body(block), 127 + offset, y + 57, 720 - offset, row - 68, 22, 15,
                            false, canvas, ink))
                        return block_overflow(layout, index, block);
                }
                return builder.result(canvas);
            }
            if (count == 1)
            {
                const auto block = blocks.first().toObject();
                if (!builder.editorial(block, 0, 58, top, 844, available, true))
                    return block_overflow(layout, 0, block);
            }
            else if (count == 2)
            {
                builder.shape(480, top, 2, available, mix(canvas, accent, 0.30));
                for (int index = 0; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    if (!builder.editorial(block, index, 58 + index * 442, top, 402, available))
                        return block_overflow(layout, index, block);
                }
            }
            else if (count == 3)
            {
                const bool lead_right = number % 2 == 0;
                const double lead_x = lead_right ? 427 : 58;
                const double support_x = lead_right ? 58 : 565;
                builder.shape(lead_right ? 409 : 551, top, 2, available, mix(canvas, accent, 0.30));
                const auto first = blocks.first().toObject();
                if (!builder.editorial(first, 0, lead_x, top, 475, available, true))
                    return block_overflow(layout, 0, first);
                const double half = (available - 20) / 2;
                builder.shape(support_x, top + half + 10, 337, 2, mix(canvas, accent, 0.30));
                for (int index = 1; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    if (!builder.editorial(
                            block, index, support_x, top + (index - 1) * (half + 20), 337, half))
                        return block_overflow(layout, index, block);
                }
            }
            else
            {
                const double half = (available - 20) / 2;
                builder.shape(480, top, 2, available, mix(canvas, accent, 0.30));
                builder.shape(58, top + half + 10, 844, 2, mix(canvas, accent, 0.30));
                for (int index = 0; index < count; ++index)
                {
                    const auto block = blocks.at(index).toObject();
                    const double x = 58 + (index % 2) * 442;
                    const double y = top + (index / 2) * (half + 20);
                    if (!builder.editorial(block, index, x, y, 402, half))
                        return block_overflow(layout, index, block);
                }
            }
            return builder.result(canvas);
        }
        if (layout == "columns" && count <= 3)
        {
            const double gap = 18;
            const double width_each = (844 - (count - 1) * gap) / count;
            for (int index = 0; index < count; ++index)
            {
                const auto block = blocks.at(index).toObject();
                const double x = 58 + index * (width_each + gap);
                const QColor panel = index == 0 ? ink : index % 2 == 1 ? soft : surface;
                if (!builder.card(block, index, x, top, width_each, available, panel, false, index == 0))
                    return block_overflow(layout, index, block);
            }
            return builder.result(canvas);
        }
        const int columns = count > 1 ? 2 : 1;
        const int rows = (count + columns - 1) / columns;
        const double gap = 20;
        const double card_width = (844 - (columns - 1) * gap) / columns;
        const double card_height = (available - (rows - 1) * gap) / rows;
        for (int index = 0; index < count; ++index)
        {
            const auto block = blocks.at(index).toObject();
            const double x = 58 + (index % columns) * (card_width + gap);
            const double y = top + (index / columns) * (card_height + gap);
            const QColor panel = index == 0 ? ink : index % 2 == 0 ? soft : surface;
            if (!builder.card(block, index, x, y, card_width, card_height, panel, false, index == 0))
                return block_overflow(layout, index, block);
        }
        return builder.result(canvas);
    }
}
