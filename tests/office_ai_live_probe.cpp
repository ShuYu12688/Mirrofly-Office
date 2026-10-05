#include "automation_bridge.hpp"
#include "canvas_bridge.hpp"
#include "office_ai_agent.hpp"
#include "presentation_bridge.hpp"
#include "presentation_image_export.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/presentation_storage.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace
{
    class ProbeRoot final : public QObject
    {
        Q_OBJECT
    public:
        QString module = "home";
        mirrorfly::CanvasBridge* map = nullptr;
        mirrorfly::PresentationBridge* slides = nullptr;

        Q_INVOKABLE bool automationReady() const
        {
            return true;
        }

        Q_INVOKABLE QVariantMap automationState() const
        {
            if (module == "slides" && slides && !slides->active())
                return {{"module", "home"}, {"pendingInput", false}};
            if (module == "mindmap" && map && !map->active())
                return {{"module", "home"}, {"pendingInput", false}};
            return {{"module", module}, {"pendingInput", false}};
        }

        Q_INVOKABLE void requestCreate(const QString& kind)
        {
            if (kind == "slides" && slides)
                slides->requestNew();
            else if (kind == "mindmap" && map)
                map->requestNew();
            module = kind;
        }

        Q_INVOKABLE void selectFile(const QUrl& url)
        {
            if (slides && url.isLocalFile() && url.toLocalFile().endsWith(".pptx", Qt::CaseInsensitive))
            {
                slides->requestOpen(url);
                module = "slides";
            }
        }
    };

    void summarize(mirrorfly::OfficeAiAgent& agent)
    {
        auto* trace = agent.trace();
        for (int index = 0; index < trace->rowCount(); ++index)
        {
            const auto row = trace->data(trace->index(index, 0), Qt::UserRole).toMap();
            if (row.value("kind") != "tool" && row.value("state") != "error")
                continue;
            std::cout << row.value("title").toString().toStdString() << ' '
                      << row.value("state").toString().toStdString() << '\n';
            if (row.value("state") == "error")
                std::cout << row.value("detail").toString().right(1000).toStdString() << '\n';
        }
    }
}

int run_office_ai_live_probe(int argc, char* argv[])
{
    const bool resume_test = argc >= 2 && std::strcmp(argv[1], "revise-resume") == 0;
    const bool revise = argc >= 2 && (std::strcmp(argv[1], "revise") == 0 || resume_test);
    const bool visual = argc >= 2 && std::strcmp(argv[1], "visual") == 0;
    const bool online_image = argc >= 2 && std::strcmp(argv[1], "image") == 0;
    if ((revise || visual ? argc != 4 : argc != 3) ||
        (std::strcmp(argv[1], "slides") != 0 && std::strcmp(argv[1], "mindmap") != 0 &&
            std::strcmp(argv[1], "cross") != 0 && std::strcmp(argv[1], "flow") != 0 &&
            std::strcmp(argv[1], "concept") != 0 && !revise && !visual && !online_image))
        return 2;
    QGuiApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    const QString directory = QString::fromLocal8Bit(argv[2]);
    if (!QDir().mkpath(directory) || !QDir(directory).entryList(QDir::Files).isEmpty())
        return 2;
    std::string key;
    if (!std::getline(std::cin, key) || key.empty())
        return 2;
    mirrorfly::PresentationBridge slides;
    mirrorfly::CanvasBridge map(false);
    ProbeRoot root;
    root.slides = &slides;
    root.map = &map;
    mirrorfly::AutomationBridge automation;
    automation.registerModule("app", &root);
    automation.registerModule("slides", &slides);
    automation.registerModule("mindmap", &map);
    automation.setUiRoot(&root);
    mirrorfly::OfficeAiAgent agent(nullptr, nullptr, directory + "/diagnostics.jsonl", directory);
    const bool configured =
        agent.configure("https://api.deepseek.com", "deepseek-flash", QString::fromStdString(key), "none");
    std::fill(key.begin(), key.end(), '\0');
    if (!configured)
        return 2;
    QString prompt;
    if (std::strcmp(argv[1], "slides") == 0)
        prompt = QStringLiteral("为中学生制作3页禁毒科普PPT：封面、常见误区与风险、求助与行动。"
                                "内容具体，版式各异，不编造统计数字；完成后保存到桌面。");
    else if (std::strcmp(argv[1], "mindmap") == 0)
        prompt = QStringLiteral("制作一份学习 C++ 的思维导图，至少包含基础语法与练习两个分支，"
                                "每个分支写一项具体学习任务，完成后保存到桌面。");
    else if (std::strcmp(argv[1], "flow") == 0)
        prompt = QStringLiteral("为初学者制作3页 C++ 从源码到运行的 PPT：封面；第2页用横向流程图"
                                "呈现写源码→编译→运行三个阶段，每阶段给短标题和简短具体说明；"
                                "第3页用一个可执行的小例子说明如何验证输出。完成后保存到桌面。");
    else if (std::strcmp(argv[1], "concept") == 0)
        prompt = QStringLiteral("为零基础听众制作3页《一杯咖啡的风味从哪里来》PPT：封面；"
                                "第2页用中心关系图解释咖啡风味与豆种、烘焙、研磨、萃取的关系，"
                                "每个要点给简短具体说明；第3页用简单流程说明从咖啡豆到一杯咖啡。"
                                "不编造数据，版式各异，完成后保存到桌面。");
    else if (online_image)
        prompt = QStringLiteral("为零基础听众制作3页《森林为什么需要保护》PPT：封面、森林中的生命、"
                                "每个人可以做什么。请按主题选合适风格，在第2页搜索一张开放授权的森林照片，"
                                "核对来源后下载并作为主要视觉；若图片服务不可用，继续用无图版式完成。"
                                "内容具体，不编造数字，完成后保存到桌面。");
    else if (visual)
    {
        const QString image = QString::fromLocal8Bit(argv[3]);
        if (!QFileInfo(image).isFile())
            return 2;
        prompt = QStringLiteral("为零基础听众制作3页《一杯咖啡的风味从哪里来》PPT：封面；"
                                "第2页用这张我提供的本地图片作主要视觉，并在旁边解释研磨和萃取"
                                "如何影响风味；第3页用简短流程说明从咖啡豆到一杯咖啡。"
                                "图片绝对路径：%1。只使用这张图片，不搜索其他图片。"
                                "内容具体、版式各异，不编造数据，完成后保存到桌面。")
                     .arg(image);
    }
    else if (revise)
    {
        const QString source = QString::fromLocal8Bit(argv[3]);
        if (!QFileInfo(source).isFile())
            return 2;
        prompt =
            QStringLiteral("请打开这个现有 PPTX：%1。只修改第 2 页的标题，把它改成“C++ 学习：从练习到调试”。"
                           "第 1 页和第 2 页其他内容保持原样；不要新建演示文稿或追加页面。"
                           "导入文件只读时先建立可编辑副本，保留原件，然后保存修改后的副本。"
                           "完成后给出保存路径。")
                .arg(source);
    }
    else
        prompt = QStringLiteral("完成两份独立文件并分别保存到桌面：先制作一份2页的 C++ 入门 PPT，"
                                "包含封面与三条具体学习建议；保存后再制作一份 C++ 学习思维导图，"
                                "包含基础语法和练习两个分支，每个分支有具体任务。"
                                "两份文件都完成后再回答，列出各自保存路径。");
    agent.start(prompt);
    QElapsedTimer clock;
    clock.start();
    QString last_status;
    bool resumed = false;
    while (agent.busy() && clock.elapsed() < 240000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (resume_test && !resumed)
        {
            auto* trace = agent.trace();
            for (int index = 0; index < trace->rowCount(); ++index)
            {
                const auto row = trace->data(trace->index(index, 0), Qt::UserRole).toMap();
                if (row.value("title") != "office_editable_copy" || row.value("state") != "done")
                    continue;
                agent.cancel();
                if (!agent.resumable())
                    return 1;
                std::cout << "cancelled after editable copy; resumable: " << agent.resumable() << '\n';
                resumed = true;
                agent.resume();
                break;
            }
        }
        if (agent.status() != last_status)
        {
            last_status = agent.status();
            std::cout << "status: " << last_status.toStdString() << '\n';
        }
        QThread::msleep(5);
    }
    if (agent.busy())
        agent.cancel();
    std::cout << "directory: " << directory.toStdString() << '\n';
    std::cout << "final: " << agent.status().toStdString() << '\n';
    std::cout << "answer: " << agent.answer().toStdString() << '\n';
    std::cout << "resumable: " << agent.resumable() << '\n';
    summarize(agent);
    if (std::strcmp(argv[1], "slides") == 0 || std::strcmp(argv[1], "cross") == 0 ||
        std::strcmp(argv[1], "flow") == 0 || std::strcmp(argv[1], "concept") == 0 || online_image || visual ||
        revise)
    {
        const QDir folder(directory);
        const auto files = folder.entryList({"*.pptx"}, QDir::Files);
        if (files.size() == 1)
        {
            const auto loaded =
                mirrorfly::load_presentation_file(folder.filePath(files.front()).toStdString());
            if (loaded.error == mirrorfly::PresentationError::None)
            {
                const auto document = mirrorfly::prepare_presentation(
                    std::make_shared<mirrorfly::PresentationScene>(loaded.scene));
                mirrorfly::PresentationImageExportOptions options;
                options.long_edge = 1280;
                mirrorfly::PresentationImageExportProgress progress;
                const auto result = mirrorfly::render_presentation_images(document, 0,
                    {{"fontFamily", "Microsoft YaHei"}, {"fontSize", 14}}, folder.path(), files.front(),
                    options, progress);
                std::cout << "rendered: " << progress.completed << " path: " << result.path.toStdString()
                          << '\n';
            }
        }
    }
    return agent.answer().isEmpty() || (resume_test && !resumed) ? 1 : 0;
}

int main(int argc, char* argv[])
{
    return run_office_ai_live_probe(argc, argv);
}

#include "office_ai_live_probe.moc"
