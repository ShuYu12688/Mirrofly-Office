#include "startup_request.hpp"

#include <mirrorfly/mindmap.hpp>
#include <mirrorfly/pdf.hpp>
#include <mirrorfly/presentation.hpp>
#include <mirrorfly/spreadsheet.hpp>
#include <mirrorfly/text.hpp>
#include <mirrorfly/word.hpp>

#include <QFileInfo>

namespace mirrorfly
{
    StartupRequest startup_request(const QStringList& arguments)
    {
        QString file;
        for (qsizetype i = 1; i < arguments.size(); ++i)
        {
            auto argument = arguments[i];
            if (argument == "--capture-dir")
            {
                if (++i >= arguments.size())
                    return {{}, QStringLiteral("启动参数缺少截图目录。")};
                continue;
            }
            if (argument == "--compact" || argument == "--test-session" || argument == "--quit-after-capture")
                continue;
            if (argument == "--open")
            {
                if (++i >= arguments.size())
                    return {{}, QStringLiteral("--open 后需要一个本地文件路径。")};
                argument = arguments[i];
            }
            else if (argument.startsWith('-'))
                return {{}, QStringLiteral("无法识别启动参数：") + argument};
            if (!file.isEmpty())
                return {{}, QStringLiteral("每个窗口一次打开一个文件。请分别打开所需文件。")};
            file = argument;
        }
        if (file.isEmpty())
            return {};
        if (file.startsWith("file:", Qt::CaseInsensitive))
        {
            const QUrl url(file);
            if (!url.isLocalFile())
                return {{}, QStringLiteral("仅支持本地文件。")};
            file = url.toLocalFile();
        }
        else if (file.contains("://"))
            return {{}, QStringLiteral("仅支持本地文件。")};
        const auto path = file.toUtf8().toStdString();
        if (!is_plain_text_path(path) && !is_word_path(path) && !is_spreadsheet_path(path) &&
            !is_presentation_path(path) && !is_pdf_path(path) && !is_mindmap_path(path))
            return {{}, QStringLiteral("此文件格式暂不支持。思维导图请使用 .mfg。")};
        return {QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath()), {}};
    }
}
