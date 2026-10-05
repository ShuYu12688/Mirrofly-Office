#include "spreadsheet_text_renderer.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }

    double covariance(const QImage& image)
    {
        double count = 0, x = 0, y = 0, xy = 0;
        for (int row = 0; row < image.height(); ++row)
            for (int column = 0; column < image.width(); ++column)
                if (image.pixelColor(column, row).lightness() < 150)
                {
                    ++count;
                    x += column;
                    y += row;
                    xy += column * row;
                }
        check(count > 50, "headless orientation paint contains visible text");
        return count > 0 ? xy / count - (x / count) * (y / count) : 0;
    }
}

int run_spreadsheet_text_layout_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    using namespace mirrorfly;
    QFont font("Calibri");
    font.setPixelSize(20);
    const QString text = "ABCDEFGH12345";
    const auto horizontal = layout_spreadsheet_cell_text(text, font, {300, 300}, {});
    for (int angle = 0; angle <= 180; ++angle)
    {
        const auto oriented = layout_spreadsheet_cell_text(text, font, {300, 300}, {{"textRotation", angle}});
        const auto baseline =
            oriented.transform.map(QPointF(horizontal.width, 0)) - oriented.transform.map(QPointF(0, 0));
        check(oriented.glyphs.size() == horizontal.glyphs.size() && oriented.scale == 1,
            "rotation preserves shaped glyph runs and stored font size");
        check(angle == 0 || (angle <= 90 ? baseline.y() < 0 : baseline.y() > 0),
            "OOXML positive and negative angle encodings point in the correct direction");
        const auto bounds = oriented.transform.mapRect(QRectF(0, 0, horizontal.width, horizontal.height));
        check(std::abs(bounds.left()) < .001 && std::abs(bounds.top()) < .001 &&
                std::abs(bounds.width() - oriented.width) < .001 &&
                std::abs(bounds.height() - oriented.height) < .001,
            "rotated bounds are normalized for cell alignment without shifting the cell origin");
    }
    const auto up = layout_spreadsheet_cell_text(text, font, {300, 300}, {{"textRotation", 90}});
    check(std::abs(up.width - horizontal.height) < .001 && std::abs(up.height - horizontal.width) < .001,
        "90-degree rotation exchanges text width and height");
    for (int angle : {45, 90, 135, 180})
    {
        const auto fitted = layout_spreadsheet_cell_text(
            text, font, {40, 60}, {{"textRotation", angle}, {"shrinkToFit", "1"}});
        check(fitted.scale < 1 && fitted.width <= 40.001 && fitted.height <= 60.001,
            "rotated shrink fits both axes without changing the font");
        const auto wrapped = layout_spreadsheet_cell_text("First words\nSecond words", font, {80, 100},
            {{"textRotation", angle}, {"shrinkToFit", "1"}, {"wrap", "1"}});
        check(wrapped.scale == 1, "rotated multiline cells do not silently shrink");
    }
    const auto one = layout_spreadsheet_text("A", font, 80, {});
    const auto combining =
        layout_spreadsheet_cell_text(QString::fromUtf8("áb"), font, {80, 200}, {{"textRotation", 255}});
    const auto plain = layout_spreadsheet_cell_text("ab", font, {80, 200}, {{"textRotation", 255}});
    check(std::abs(combining.height - plain.height) < .001 && plain.height >= one.height * 2 - .001,
        "combining marks share one vertical line with their base character");
    const auto emoji = layout_spreadsheet_cell_text(
        QString::fromUtf8("👩‍💻"), font, {100, 200}, {{"textRotation", 255}});
    const auto emoji_plain = layout_spreadsheet_text(QString::fromUtf8("👩‍💻"), font, 100, {});
    check(std::abs(emoji.height - emoji_plain.height) < .001,
        "joined emoji is one vertical grapheme rather than separated code units");
    for (int angle : {45, 135, 255})
    {
        QImage image(240, 240, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setFont(font);
        painter.setPen(Qt::black);
        paint_spreadsheet_aligned_text(painter, {20, 20, 200, 200}, text,
            {{"textRotation", angle}, {"align", "center"}, {"valign", "center"}});
        painter.end();
        const auto direction = covariance(image);
        if (angle != 255)
            check(angle == 45 ? direction < -100 : direction > 100,
                "actual painted diagonal direction agrees with Excel encoding");
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 240; ++x)
                if (x < 20 || x >= 220 || y < 20 || y >= 220)
                    check(image.pixelColor(x, y) == QColor(Qt::white),
                        "transformed glyphs remain clipped to their own cell");
    }
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_spreadsheet_text_layout_tests(argc, argv);
}
