#include "theme.hpp"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

#include <iostream>

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

    QString configuration_path(const QString& directory)
    {
        return QDir(directory).filePath(QStringLiteral("config/theme.json"));
    }

    bool write_configuration(const QString& directory, const QByteArray& bytes)
    {
        const QString path = configuration_path(directory);

        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        {
            return false;
        }

        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

    bool test_missing_configuration(const QString& directory, const mirrorfly::ThemeResult& baseline)
    {
        QFile defaults(QStringLiteral(":/mirrorfly/config/theme.json"));

        if (!check(defaults.open(QIODevice::ReadOnly), "bundled theme resource is available"))
        {
            return false;
        }

        const auto expected = QJsonDocument::fromJson(defaults.readAll()).object().toVariantMap();
        bool passed = check(
            !QFileInfo::exists(configuration_path(directory)), "default test has no external configuration");
        passed = check(!expected.isEmpty() && baseline.values.size() == expected.size(),
                     "missing configuration loads the complete bundled theme") &&
            passed;
        passed =
            check(baseline.warning.isEmpty(), "missing optional configuration produces no warning") && passed;

        for (auto iterator = expected.cbegin(); iterator != expected.cend(); ++iterator)
        {
            // Font fallback depends on the fonts installed on each test host.
            if (iterator.key() != QStringLiteral("fontFamily") &&
                iterator.key() != QStringLiteral("editorFontFamily"))
            {
                passed = check(baseline.values.value(iterator.key()) == iterator.value(),
                             "bundled defaults are preserved") &&
                    passed;
            }
        }

        passed = check(!baseline.values.value("fontFamily").toString().isEmpty() &&
                         !baseline.values.value("editorFontFamily").toString().isEmpty(),
                     "font selection resolves to a usable family") &&
            passed;
        return passed;
    }

    bool test_invalid_json(const QString& directory, const mirrorfly::ThemeResult& baseline)
    {
        if (!check(write_configuration(directory, QByteArray("{invalid json")), "write invalid JSON fixture"))
        {
            return false;
        }

        const auto theme = mirrorfly::load_theme(directory);
        bool passed = check(theme.values == baseline.values, "invalid JSON restores bundled defaults");
        passed = check(!theme.warning.isEmpty(), "invalid JSON reports a warning") && passed;
        return passed;
    }

    bool test_invalid_colors(const QString& directory, const mirrorfly::ThemeResult& baseline)
    {
        const QJsonObject overrides{
            {"accent", "#not-a-color"}, {"transparentColor", "not-a-color"}, {"backgroundMode", "static"}};

        if (!check(write_configuration(directory, QJsonDocument(overrides).toJson()),
                "write invalid color fixture"))
        {
            return false;
        }

        const auto theme = mirrorfly::load_theme(directory);
        bool passed = check(theme.values.value("accent") == baseline.values.value("accent"),
            "invalid ordinary color falls back to its default");
        passed = check(theme.values.value("transparentColor") == baseline.values.value("transparentColor"),
                     "invalid transparent color falls back to its default") &&
            passed;
        passed = check(theme.values.value("backgroundMode").toString() == QStringLiteral("static"),
                     "valid settings still apply beside invalid colors") &&
            passed;
        passed = check(!theme.warning.isEmpty(), "invalid colors report a warning") && passed;
        return passed;
    }

    bool test_valid_overrides(const QString& directory)
    {
        const QJsonObject overrides{{"backgroundMode", "static"}, {"motionEnabled", false},
            {"windowOpacity", 0.75}, {"accent", "#123456"}, {"transparentColor", "transparent"}};

        if (!check(write_configuration(directory, QJsonDocument(overrides).toJson()),
                "write valid theme fixture"))
        {
            return false;
        }

        const auto theme = mirrorfly::load_theme(directory);
        bool passed = check(theme.warning.isEmpty(), "valid theme overrides produce no warning");
        passed = check(theme.values.value("backgroundMode").toString() == QStringLiteral("static") &&
                         !theme.values.value("motionEnabled").toBool(),
                     "static background and disabled motion apply") &&
            passed;
        passed = check(theme.values.value("windowOpacity").toDouble() == 0.75,
                     "window opacity override applies") &&
            passed;
        passed = check(QColor(theme.values.value("accent").toString()) == QColor(QStringLiteral("#123456")),
                     "valid color override applies") &&
            passed;
        return passed;
    }

    bool test_relative_background(const QString& directory)
    {
        const QString relative_path = QString::fromUtf8(u8"assets/背景 texture.png");
        const QString image_path =
            QDir(QFileInfo(configuration_path(directory)).absolutePath()).filePath(relative_path);

        if (!check(
                QDir().mkpath(QFileInfo(image_path).absolutePath()), "create temporary background directory"))
        {
            return false;
        }

        QImage image(2, 2, QImage::Format_ARGB32);
        image.fill(QColor(QStringLiteral("#123456")));

        if (!check(image.save(image_path), "create real temporary background image"))
        {
            return false;
        }

        const QJsonObject overrides{{"backgroundImage", relative_path}};

        if (!check(write_configuration(directory, QJsonDocument(overrides).toJson()),
                "write relative background fixture"))
        {
            return false;
        }

        const auto theme = mirrorfly::load_theme(directory);
        const QUrl resolved(theme.values.value("backgroundImage").toString());
        bool passed = check(theme.warning.isEmpty(), "existing relative background produces no warning");
        passed = check(resolved.isLocalFile() &&
                         QFileInfo(resolved.toLocalFile()).canonicalFilePath() ==
                             QFileInfo(image_path).canonicalFilePath(),
                     "relative UTF-8 background resolves beside config to a local file URL") &&
            passed;
        return passed;
    }

    bool test_size_limit(const QString& directory, const mirrorfly::ThemeResult& baseline)
    {
        constexpr qsizetype maximum_bytes = 64 * 1024;
        QByteArray bytes("{\"windowOpacity\":0.5}");
        bytes.append(QByteArray(maximum_bytes - bytes.size(), ' '));

        if (!check(write_configuration(directory, bytes), "write valid theme at size limit"))
        {
            return false;
        }

        const auto at_limit = mirrorfly::load_theme(directory);
        bool passed =
            check(at_limit.warning.isEmpty() && at_limit.values.value("windowOpacity").toDouble() == 0.5,
                "valid 64 KiB theme remains readable");
        bytes.append(' ');

        if (!check(write_configuration(directory, bytes), "write valid theme beyond size limit"))
        {
            return false;
        }

        const auto oversized = mirrorfly::load_theme(directory);
        passed =
            check(oversized.values == baseline.values, "theme over 64 KiB falls back to defaults") && passed;
        passed = check(!oversized.warning.isEmpty(), "oversized theme reports a warning") && passed;
        return passed;
    }

}

int run_theme_tests(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QTemporaryDir directory;

    if (!check(directory.isValid(), "create isolated theme test directory"))
    {
        return 1;
    }

    const auto baseline = mirrorfly::load_theme(directory.path());
    bool passed = test_missing_configuration(directory.path(), baseline);
    QFile packaged_defaults(QStringLiteral(":/mirrorfly/config/theme.json"));
    passed = check(packaged_defaults.open(QIODevice::ReadOnly), "open packaged defaults") && passed;
    passed = check(write_configuration(directory.path(), packaged_defaults.readAll()),
                 "install complete default theme") &&
        passed;
    passed = check(mirrorfly::load_theme(directory.path()).warning.isEmpty(),
                 "shipping defaults are accepted as external configuration, including the resize margin") &&
        passed;
    passed = test_invalid_json(directory.path(), baseline) && passed;
    passed = test_invalid_colors(directory.path(), baseline) && passed;
    passed = test_valid_overrides(directory.path()) && passed;
    passed = test_relative_background(directory.path()) && passed;
    passed = test_size_limit(directory.path(), baseline) && passed;

    if (passed)
    {
        std::cout << "Theme tests passed.\n";
    }

    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_theme_tests(argc, argv);
}
