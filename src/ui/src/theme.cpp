#include "theme.hpp"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QtNumeric>

namespace
{
    QVariantMap read_theme_file(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        constexpr qint64 maximum_size = 64 * 1024;
        if (file.size() > maximum_size)
        {
            return {};
        }
        const QByteArray bytes = file.read(maximum_size + 1);
        if (bytes.size() > maximum_size)
        {
            return {};
        }
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !json.isObject())
        {
            return {};
        }
        return json.object().toVariantMap();
    }

    bool valid_theme_value(const QString& key, const QVariant& value, const QVariant& original)
    {
        if (original.metaType().id() == QMetaType::Bool)
        {
            return value.metaType().id() == QMetaType::Bool;
        }
        if (original.metaType().id() == QMetaType::QString)
        {
            if (value.metaType().id() != QMetaType::QString)
            {
                return false;
            }
            if (original.toString().startsWith('#') || key == "transparentColor")
            {
                return QColor::isValidColorName(value.toString());
            }
            if (key == "backgroundMode")
            {
                return value == "static" || value == "dynamic";
            }
            return true;
        }
        bool converted = false;
        const double number = value.toDouble(&converted);
        if (!converted || !qIsFinite(number))
        {
            return false;
        }
        if (key.contains("opacity", Qt::CaseInsensitive))
        {
            return number >= (key == "windowOpacity" ? 0.2 : 0.0) && number <= 1.0;
        }
        if (key.endsWith("fontSize", Qt::CaseInsensitive))
        {
            return number >= 8.0 && number <= 96.0;
        }
        return number >= 0.0 && number <= 20000.0;
    }
}

namespace mirrorfly
{
    ThemeResult load_theme(const QString& application_directory)
    {
        ThemeResult result{read_theme_file(":/mirrorfly/config/theme.json"), {}};
        const QString path = QDir(application_directory).filePath("config/theme.json");
        if (QFileInfo::exists(path))
        {
            const auto overrides = read_theme_file(path);
            bool invalid = overrides.isEmpty();
            for (auto iterator = overrides.cbegin(); iterator != overrides.cend(); ++iterator)
            {
                if (!result.values.contains(iterator.key()) ||
                    !valid_theme_value(iterator.key(), iterator.value(), result.values[iterator.key()]))
                {
                    invalid = true;
                    continue;
                }
                result.values[iterator.key()] = iterator.value();
            }
            if (invalid)
            {
                result.warning = QStringLiteral("部分主题设置无效，已为这些项目使用默认样式。");
            }
        }
        const QString image = result.values.value("backgroundImage").toString();
        if (!image.isEmpty())
        {
            const QString absolute = QFileInfo(image).isAbsolute()
                ? image
                : QDir(QFileInfo(path).absolutePath()).absoluteFilePath(image);
            if (QFileInfo::exists(absolute))
            {
                result.values["backgroundImage"] = QUrl::fromLocalFile(absolute).toString();
            }
            else
            {
                result.values["backgroundImage"] = QString{};
                result.warning = QStringLiteral("背景图片未找到，已使用默认背景。");
            }
        }
        const auto families = QFontDatabase::families();
        if (!families.contains(result.values.value("editorFontFamily").toString()))
        {
            result.values["editorFontFamily"] = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        }
        if (!families.contains(result.values.value("fontFamily").toString()))
        {
            const QStringList fallbacks{"Noto Sans CJK SC", "PingFang SC", "Microsoft YaHei UI"};
            QString family = QGuiApplication::font().family();
            for (const auto& candidate : fallbacks)
            {
                if (families.contains(candidate))
                {
                    family = candidate;
                    break;
                }
            }
            result.values["fontFamily"] = family;
        }
        return result;
    }
}
