#include "presentation_bridge.hpp"
#include "document_path.hpp"
#include "presentation_edit_adapter.hpp"
#include "presentation_text_style_adapter.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QThread>
#include <QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <limits>
#include <set>
#include <utility>

namespace
{
    Q_LOGGING_CATEGORY(pptxBridgeLatency, "mirrorfly.pptx.latency", QtInfoMsg)

    constexpr std::size_t maximum_history_bytes = 64 * 1024 * 1024;
    constexpr std::size_t maximum_editable_scene_bytes = 64 * 1024 * 1024;
    constexpr std::size_t maximum_history_entries = 32;
    constexpr std::size_t maximum_pending_edits = 64;

    bool reparses_document(mirrorfly::PresentationEditAction action)
    {
        using A = mirrorfly::PresentationEditAction;
        return action == A::ApplyTheme || action == A::ResetPlaceholderFill ||
            action == A::ResetPlaceholderOutline || action == A::ResetTextInheritance ||
            action == A::InsertTable || action == A::InsertTableRow || action == A::InsertTableColumn ||
            action == A::DeleteTableRow || action == A::DeleteTableColumn || action == A::MergeTableCell ||
            action == A::UnmergeTableCell || action == A::FormatTableStyle ||
            action == A::SetSlideTransition || action == A::FormatTableBorder ||
            action == A::ResetTableCellFill || action == A::ResetTableBorder || action == A::GroupAdjacent ||
            action == A::AddToGroup || action == A::Ungroup || action == A::ReorderGroup;
    }

    QString presentation_error(mirrorfly::PresentationError error)
    {
        using mirrorfly::PresentationError;
        switch (error)
        {
        case PresentationError::UnsupportedType:
            return QStringLiteral("当前支持 .pptx 演示文稿；旧版 .ppt 请先转换为 .pptx。");
        case PresentationError::ReadFailed:
            return QStringLiteral("演示文稿无法读取，请检查文件路径与读取权限。");
        case PresentationError::TooLarge:
            return QStringLiteral("演示文稿超过限制：压缩包 256 MiB、展开后 768 MiB、最多 200 页。");
        case PresentationError::EncryptedArchive:
            return QStringLiteral("暂不支持加密的演示文稿，请先在原应用中取消密码保护。");
        case PresentationError::InvalidArchive:
            return QStringLiteral("演示文稿压缩包无效或校验失败，文件可能已经损坏。");
        case PresentationError::InvalidPackage:
            return QStringLiteral("文件缺少必要的 PPTX 结构，无法读取。");
        case PresentationError::InvalidXml:
            return QStringLiteral("演示文稿中的 XML 数据无效或超过解析限制。");
        case PresentationError::MissingPart:
            return QStringLiteral("演示文稿引用的页面或资源缺失，无法完成读取。");
        case PresentationError::WriteFailed:
            return QStringLiteral("未能保存演示文稿，原文件未被替换。请检查权限或另存到其他位置。");
        case PresentationError::ChangedOnDisk:
            return QStringLiteral("演示文稿已被其他程序修改或移走。请另存为，保留当前编辑内容。");
        case PresentationError::InvalidImage:
            return QStringLiteral("图片无效。请选择 PNG、JPEG、BMP、GIF 或 WebP 文件。");
        case PresentationError::None:
            return QStringLiteral("演示文稿没有可用页面。");
        }
        return QStringLiteral("演示文稿操作未完成。");
    }

    QString edit_error(mirrorfly::PresentationEditError error, const std::string& detail)
    {
        if (!detail.empty())
        {
            return QString::fromUtf8(detail.data(), static_cast<qsizetype>(detail.size()));
        }
        switch (error)
        {
        case mirrorfly::PresentationEditError::ReadOnly:
            return QStringLiteral("请先为此演示文稿创建可编辑副本。");
        case mirrorfly::PresentationEditError::TooLarge:
            return QStringLiteral("此操作会超过演示文稿编辑限制。");
        case mirrorfly::PresentationEditError::InvalidIndex:
        case mirrorfly::PresentationEditError::InvalidValue:
            return QStringLiteral("无法应用此编辑操作。");
        case mirrorfly::PresentationEditError::None:
            return {};
        }
        return QStringLiteral("无法应用此编辑操作。");
    }

    QString utf8(const std::string& text)
    {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }

    std::string narrow(const QString& text)
    {
        return text.toUtf8().toStdString();
    }

    std::size_t string_bytes(const std::string& text)
    {
        return sizeof(text) + text.capacity();
    }

    std::size_t fill_bytes(const mirrorfly::PresentationFill& fill)
    {
        std::size_t bytes =
            string_bytes(fill.color) + fill.stops.capacity() * sizeof(mirrorfly::PresentationGradientStop);
        bytes += string_bytes(fill.image_path) + string_bytes(fill.image_alignment) +
            string_bytes(fill.image_flip) + string_bytes(fill.pattern) +
            string_bytes(fill.pattern_foreground_color);
        for (const auto& stop : fill.stops)
        {
            bytes += string_bytes(stop.color);
        }
        return bytes;
    }

    std::size_t scene_bytes(const mirrorfly::PresentationScene& scene)
    {
        std::size_t bytes = sizeof(scene) + presentation_source_metadata_bytes(scene) +
            scene.slides.capacity() * sizeof(mirrorfly::PresentationSlide) +
            scene.images.capacity() * sizeof(mirrorfly::PresentationImage);
        for (const auto& warning : scene.warnings)
        {
            bytes += string_bytes(warning);
        }
        for (const auto& image : scene.images)
        {
            bytes += string_bytes(image.path) + string_bytes(image.mime_type);
        }
        for (const auto& media : scene.media)
            bytes += sizeof(mirrorfly::PresentationMedia) + string_bytes(media.path) +
                string_bytes(media.mime_type);
        for (const auto& font : scene.embedded_fonts)
            bytes += sizeof(mirrorfly::PresentationEmbeddedFont) + string_bytes(font.family) +
                string_bytes(font.style) + string_bytes(font.path);
        for (const auto& slide : scene.slides)
        {
            bytes += string_bytes(slide.title) + fill_bytes(slide.background) +
                slide.shapes.capacity() * sizeof(mirrorfly::PresentationShape);
            bytes += string_bytes(slide.source_part) +
                slide.animations.capacity() * sizeof(mirrorfly::PresentationAnimation);
            bytes += slide.groups.capacity() * sizeof(mirrorfly::PresentationGroupFrame);
            bytes += slide.media_cues.capacity() * sizeof(mirrorfly::PresentationMediaCue);
            for (const auto& cue : slide.media_cues)
                bytes += string_bytes(cue.target) + string_bytes(cue.action);
            for (const auto& group : slide.groups)
                bytes += string_bytes(group.source_id) + string_bytes(group.source_part);
            for (const auto& animation : slide.animations)
                bytes += string_bytes(animation.target) + string_bytes(animation.category) +
                    string_bytes(animation.filter) + string_bytes(animation.color) +
                    string_bytes(animation.trigger) + string_bytes(animation.iterate_type) +
                    animation.motion.capacity() * sizeof(std::array<double, 2>);
            for (const auto& warning : slide.warnings)
            {
                bytes += string_bytes(warning);
            }
            for (const auto& shape : slide.shapes)
            {
                bytes += string_bytes(shape.name) + string_bytes(shape.geometry) +
                    string_bytes(shape.image_path) + fill_bytes(shape.fill) + fill_bytes(shape.outline_fill) +
                    string_bytes(shape.outline_color);
                bytes += string_bytes(shape.geometry_definition) + string_bytes(shape.source_id) +
                    string_bytes(shape.source_part) + string_bytes(shape.media_path) +
                    string_bytes(shape.click_action.kind);
                if (shape.table_cell)
                    bytes += string_bytes(shape.table_cell->frame_id);
                bytes += fill_bytes(shape.effects.outline_fill) + string_bytes(shape.effects.outline_color) +
                    string_bytes(shape.effects.shadow_color) + string_bytes(shape.effects.glow_color);
                bytes += shape.line_style.dashes.capacity() * sizeof(double) +
                    string_bytes(shape.line_style.cap) + string_bytes(shape.line_style.join);
                for (const auto* end : {&shape.line_style.head, &shape.line_style.tail})
                    bytes += string_bytes(end->type) + string_bytes(end->width) + string_bytes(end->length);
                for (const auto& group : shape.source_groups)
                    bytes += string_bytes(group);
                if (shape.path_geometry)
                {
                    bytes += sizeof(mirrorfly::PresentationGeometry);
                    for (const auto& path : shape.path_geometry->paths)
                        bytes += sizeof(mirrorfly::PresentationPath) + string_bytes(path.fill) +
                            path.commands.capacity() * sizeof(mirrorfly::PresentationPathCommand);
                }
                bytes += shape.text.paragraphs.capacity() * sizeof(mirrorfly::PresentationParagraph);
                bytes += string_bytes(shape.text.warp) + string_bytes(shape.text.vertical);
                for (const auto& paragraph : shape.text.paragraphs)
                {
                    bytes += string_bytes(paragraph.alignment) + string_bytes(paragraph.bullet) +
                        string_bytes(paragraph.bullet_image_path) + string_bytes(paragraph.number_format) +
                        string_bytes(paragraph.bullet_font) + string_bytes(paragraph.bullet_color) +
                        paragraph.runs.capacity() * sizeof(mirrorfly::PresentationRun);
                    for (const auto& run : paragraph.runs)
                    {
                        bytes += string_bytes(run.text) + string_bytes(run.font_family) +
                            string_bytes(run.east_asian_font_family) + string_bytes(run.color) +
                            string_bytes(run.click_action.kind);
                        bytes += fill_bytes(run.fill) + fill_bytes(run.effects.outline_fill) +
                            string_bytes(run.effects.outline_color) + string_bytes(run.effects.shadow_color) +
                            string_bytes(run.effects.glow_color);
                    }
                }
            }
        }
        return bytes;
    }

    std::size_t detached_image_bytes(
        const mirrorfly::PresentationScene& scene, const mirrorfly::PresentationScene* retained)
    {
        std::set<const std::string*> shared;
        if (retained)
        {
            for (const auto& image : retained->images)
            {
                if (image.bytes)
                {
                    shared.insert(image.bytes.get());
                }
            }
        }
        std::set<const std::string*> counted;
        std::size_t bytes = 0;
        for (const auto& image : scene.images)
        {
            if (image.bytes && !shared.count(image.bytes.get()) && counted.insert(image.bytes.get()).second)
            {
                if (image.bytes->size() > std::numeric_limits<std::size_t>::max() - bytes)
                {
                    return std::numeric_limits<std::size_t>::max();
                }
                bytes += image.bytes->size();
            }
        }
        return bytes;
    }

    bool reuse_render_analysis(const mirrorfly::PresentationEditCommand& command)
    {
        using Action = mirrorfly::PresentationEditAction;
        if (command.action == Action::AddSlide || command.action == Action::AddText ||
            command.action == Action::AddImage || command.action == Action::ReplaceImage ||
            command.action == Action::DeleteSlide || command.action == Action::DeleteShape)
        {
            return false;
        }
        return command.action != Action::FormatText || !command.font_family;
    }

    mirrorfly::PresentationPrepareOptions edit_render_options(
        const mirrorfly::RenderPresentationPtr& document, const mirrorfly::PresentationEditCommand& command,
        const mirrorfly::PresentationEditResult& result)
    {
        using Action = mirrorfly::PresentationEditAction;
        mirrorfly::PresentationPrepareOptions options;
        options.reuse_analysis = reuse_render_analysis(command);
        options.eager_image_analysis = false;
        const bool local_shape_edit = command.action == Action::UpdateText ||
            (command.action == Action::ReplaceTextMatches && !command.find_all) ||
            command.action == Action::FormatText || command.action == Action::FormatTextStyle ||
            command.action == Action::FormatParagraph || command.action == Action::FormatTextBox ||
            command.action == Action::FormatShape || command.action == Action::FormatTableCell ||
            command.action == Action::FormatImage || command.action == Action::ReplaceImage ||
            command.action == Action::TransformShape || command.action == Action::AlignShape ||
            command.action == Action::DeleteShape || command.action == Action::ApplyFormat;
        if (local_shape_edit)
        {
            options.edited_slide = static_cast<int>(command.slide_index);
            options.edited_shape = static_cast<int>(command.shape_index);
            if (command.action == Action::DeleteShape)
            {
                options.edit_layer_change = mirrorfly::PresentationEditLayerChange::Remove;
            }
        }
        auto& revisions = options.thumbnail_revisions;
        revisions = document->thumbnail_revisions;
        const auto position = static_cast<std::ptrdiff_t>(command.slide_index);
        if (command.action == Action::AddSlide || command.action == Action::DuplicateSlide)
        {
            revisions.insert(revisions.begin() + static_cast<std::ptrdiff_t>(result.slide_index), 0);
        }
        else if (command.action == Action::DeleteSlide)
        {
            revisions.erase(revisions.begin() + position);
        }
        else if (command.action == Action::MoveSlide)
        {
            std::swap(revisions[command.slide_index], revisions[result.slide_index]);
        }
        else if (command.action == Action::ReplaceTextMatches && command.find_all)
        {
            std::fill(revisions.begin(), revisions.end(), 0);
        }
        else if (command.action != Action::SetSlideHidden && command.action != Action::SetClickAction)
        {
            revisions[command.slide_index] = 0;
        }
        return options;
    }

    std::uint64_t selected_id(const mirrorfly::PresentationScene& scene, int slide_index, int shape_index)
    {
        if (slide_index < 0 || slide_index >= static_cast<int>(scene.slides.size()) || shape_index < 0)
        {
            return 0;
        }
        const auto& shapes = scene.slides[static_cast<std::size_t>(slide_index)].shapes;
        return shape_index < static_cast<int>(shapes.size())
            ? shapes[static_cast<std::size_t>(shape_index)].id
            : 0;
    }

    std::pair<std::string, std::string> selected_source(
        const mirrorfly::PresentationScene& scene, int slide_index, int shape_index)
    {
        if (slide_index < 0 || slide_index >= static_cast<int>(scene.slides.size()) || shape_index < 0)
            return {};
        const auto& shapes = scene.slides[static_cast<std::size_t>(slide_index)].shapes;
        if (shape_index >= static_cast<int>(shapes.size()))
            return {};
        const auto& shape = shapes[static_cast<std::size_t>(shape_index)];
        return {shape.source_part, shape.source_id};
    }

    void restore_selection(const mirrorfly::PresentationScene& scene, std::uint64_t id, int& slide_index,
        int& shape_index, const std::string& source_part = {}, const std::string& source_id = {})
    {
        shape_index = -1;
        slide_index = std::clamp(slide_index, 0, static_cast<int>(scene.slides.size()) - 1);
        if (!source_part.empty() && !source_id.empty())
        {
            int found_page = -1;
            int found_index = -1;
            for (std::size_t page = 0; page < scene.slides.size(); ++page)
                for (std::size_t index = 0; index < scene.slides[page].shapes.size(); ++index)
                {
                    const auto& shape = scene.slides[page].shapes[index];
                    if (shape.source_part == source_part && shape.source_id == source_id)
                    {
                        if (found_page >= 0)
                            return;
                        found_page = static_cast<int>(page);
                        found_index = static_cast<int>(index);
                    }
                }
            if (found_page >= 0)
            {
                slide_index = found_page;
                shape_index = found_index;
                return;
            }
            if (scene.source_package)
                return;
        }
        if (id == 0)
        {
            return;
        }
        for (std::size_t page = 0; page < scene.slides.size(); ++page)
        {
            const auto& shapes = scene.slides[page].shapes;
            for (std::size_t index = 0; index < shapes.size(); ++index)
            {
                if (shapes[index].id == id)
                {
                    slide_index = static_cast<int>(page);
                    shape_index = static_cast<int>(index);
                    return;
                }
            }
        }
    }

}

namespace mirrorfly
{
    PresentationBridge::PresentationBridge(QObject* parent)
        : QObject(parent), render_environment_(presentation_render_environment())
    {
        edit_pool_.setMaxThreadCount(1);
        edit_pool_.setExpiryTimeout(10000);
        edit_pool_.setThreadPriority(QThread::NormalPriority);
        connect(
            &worker_, &QFutureWatcher<SaveResult>::finished, this, &PresentationBridge::completeOperation);
        connect(&load_worker_, &QFutureWatcher<PreparedPresentationLoad>::finished, this,
            &PresentationBridge::completeLoad);
        connect(&image_worker_, &QFutureWatcher<PresentationImageFileResult>::finished, this,
            &PresentationBridge::completeImageLoad);
        connect(&edit_worker_, &QFutureWatcher<EditCommitResult>::finished, this,
            &PresentationBridge::completeEditCommit);
    }

    PresentationBridge::~PresentationBridge()
    {
        // Progress callbacks may still be queuing events against this context during shutdown.
        for (auto* watcher :
            {static_cast<QFutureWatcherBase*>(&worker_), static_cast<QFutureWatcherBase*>(&load_worker_),
                static_cast<QFutureWatcherBase*>(&image_worker_),
                static_cast<QFutureWatcherBase*>(&edit_worker_)})
        {
            try
            {
                watcher->waitForFinished();
            }
            catch (...)
            {
                // A finished worker failure must not escape a destructor.
            }
        }
    }

    QVariantMap PresentationBridge::snapshot() const
    {
        auto result = semanticTree(current_slide_, 0);
        result.insert("generation", QString::number(generation_));
        result.insert("syncing", syncing());
        result.insert("pendingEdits", pendingEdits());
        result.insert("guideSettings", guideSettings());
        result.insert("slideTransition", slideTransition());
        result.insert("slideSections", slideSections());
        result.insert("sectionsEditable", sectionsEditable());
        result.insert("busy", busy());
        result.insert("loadingStage", loadingStage());
        result.insert("loadingProgress", loadingProgress());
        result.insert("selectedId",
            scene_ ? QString::number(selected_id(*scene_, current_slide_, selected_shape_)) : "0");
        return result;
    }

    bool PresentationBridge::selectObject(const QString& id)
    {
        if (!scene_ || locked())
            return false;
        for (std::size_t page = 0; page < scene_->slides.size(); ++page)
            for (std::size_t index = 0; index < scene_->slides[page].shapes.size(); ++index)
                if (QString::number(scene_->slides[page].shapes[index].id) == id)
                {
                    setSlide(static_cast<int>(page));
                    selectShape(static_cast<int>(index));
                    return true;
                }
        return false;
    }

    void PresentationBridge::setLoadingProgress(const QString& stage, qreal progress)
    {
        const qreal bounded = std::clamp(progress, 0.0, 1.0);
        const bool changed = loading_stage_ != stage || bounded > loading_progress_;
        loading_stage_ = stage;
        loading_progress_ = std::max(loading_progress_, bounded);
        if (changed)
        {
            emit stateChanged();
        }
    }

    void PresentationBridge::updateLoadingProgress(const PresentationLoadProgress& progress)
    {
        if (operation_ != Operation::Load)
        {
            return;
        }
        const qreal fraction = progress.total > 0
            ? std::clamp(static_cast<qreal>(progress.completed) / progress.total, 0.0, 1.0)
            : 0;
        QString stage;
        qreal value = 0;
        switch (progress.stage)
        {
        case PresentationLoadStage::Reading:
            stage = QStringLiteral("正在读取演示文稿");
            value = 0.02 + fraction * 0.06;
            break;
        case PresentationLoadStage::Validating:
            stage = QStringLiteral("正在校验文件结构");
            value = 0.08 + fraction * 0.07;
            break;
        case PresentationLoadStage::Extracting:
            stage = progress.total > 0
                ? QStringLiteral("正在解压资源 %1/%2").arg(progress.completed).arg(progress.total)
                : QStringLiteral("正在解压资源");
            value = 0.15 + fraction * 0.55;
            break;
        case PresentationLoadStage::Parsing:
            stage = progress.total > 0
                ? QStringLiteral("正在解析幻灯片 %1/%2").arg(progress.completed).arg(progress.total)
                : QStringLiteral("正在解析幻灯片");
            value = 0.70 + fraction * 0.20;
            break;
        }
        setLoadingProgress(stage, value);
    }

    void PresentationBridge::setMessage(const QString& message)
    {
        if (message_ == message)
        {
            return;
        }
        message_ = message;
        emit messageChanged();
    }

    void PresentationBridge::setError(const QString& error)
    {
        if (!error.isEmpty() && !message_.isEmpty())
        {
            setMessage({});
        }
        if (error_ == error)
        {
            return;
        }
        error_ = error;
        emit errorChanged();
    }

    void PresentationBridge::clearMessage()
    {
        setMessage({});
    }

    void PresentationBridge::clearError()
    {
        setError({});
    }

    bool PresentationBridge::requestOpen(const QUrl& url)
    {
        if (locked() || syncing())
        {
            return false;
        }
        if (!url.isLocalFile() || url.toLocalFile().isEmpty())
        {
            setError(QStringLiteral("请选择本地 .pptx 演示文稿。"));
            emit openCompleted(false);
            return true;
        }
        requestAction(Action::Open, url);
        return true;
    }

    void PresentationBridge::requestNew()
    {
        requestAction(Action::New);
    }

    void PresentationBridge::createEditableCopy()
    {
        if (!active_ || locked())
        {
            return;
        }
        if (editable())
        {
            saveAs();
            return;
        }
        if (!scene_ || scene_bytes(*scene_) > maximum_editable_scene_bytes)
        {
            setError(QStringLiteral("此演示文稿转换后会超过编辑内存上限，当前仅可预览。"));
            return;
        }
        setMessage(
            QStringLiteral("副本保留原包中的媒体、动画、备注和复杂对象；只开放已支持的对象与属性编辑。"));
        openSaveDialog(true);
    }

    bool PresentationBridge::createEditableCopyTo(const QUrl& destination)
    {
        if (!active_ || editable() || locked() || syncing() || save_dialog_open_ || !scene_ ||
            scene_bytes(*scene_) > maximum_editable_scene_bytes ||
            !new_document_destination(destination, QStringList{"pptx"}) ||
            same_document_path(imported_source_path_, destination.toLocalFile()))
        {
            setError(QStringLiteral("请为只读 PPT 指定已有目录下尚不存在的 PPTX 副本路径。"));
            return false;
        }
        beginSave(QFileInfo(destination.toLocalFile()).absoluteFilePath(), true, true);
        return operation_ == Operation::Save;
    }

    void PresentationBridge::showHome()
    {
        requestAction(Action::Home);
    }

    void PresentationBridge::resetDocument()
    {
        scene_.reset();
        committed_scene_.reset();
        document_.reset();
        saving_scene_.reset();
        path_.clear();
        pending_path_.clear();
        loading_stage_.clear();
        loading_progress_ = 0;
        disk_revision_.clear();
        imported_source_path_.clear();
        undo_.clear();
        redo_.clear();
        pending_edits_.clear();
        undo_bytes_ = 0;
        redo_bytes_ = 0;
        generation_ = 0;
        next_generation_ = 1;
        saved_generation_ = 0;
        committed_generation_ = 0;
        current_slide_ = 0;
        selected_shape_ = -1;
        zoom_ = 0;
        guide_settings_ = {};
        active_ = false;
        replacing_image_ = false;
    }

    void PresentationBridge::clear()
    {
        if (busy_ || syncing())
        {
            return;
        }
        resetDocument();
        clearMessage();
        clearError();
        emit documentChanged();
        emit selectionChanged();
        emit stateChanged();
    }

    void PresentationBridge::setSlide(int index)
    {
        if (!locked() && index >= 0 && index < slideCount() && index != current_slide_)
        {
            current_slide_ = index;
            selected_shape_ = -1;
            emit selectionChanged();
            emit stateChanged();
        }
    }

    void PresentationBridge::nextSlide()
    {
        setSlide(current_slide_ + 1);
    }

    void PresentationBridge::previousSlide()
    {
        setSlide(current_slide_ - 1);
    }

    void PresentationBridge::selectShape(int index)
    {
        if (locked() || !scene_ || current_slide_ < 0 || current_slide_ >= slideCount())
        {
            return;
        }
        const auto size = scene_->slides[static_cast<std::size_t>(current_slide_)].shapes.size();
        const int bounded = index >= 0 && index < static_cast<int>(size) ? index : -1;
        if (selected_shape_ != bounded)
        {
            selected_shape_ = bounded;
            emit selectionChanged();
        }
    }

    bool PresentationBridge::applyEdit(const QString& action, const QVariantMap& options)
    {
        static const std::set<QString> actions{QStringLiteral("addSlide"), QStringLiteral("duplicateSlide"),
            QStringLiteral("deleteSlide"), QStringLiteral("moveSlide"), QStringLiteral("createSection"),
            QStringLiteral("renameSection"), QStringLiteral("removeSection"), QStringLiteral("addText"),
            QStringLiteral("addShape"), QStringLiteral("updateText"), QStringLiteral("formatText"),
            QStringLiteral("formatParagraph"), QStringLiteral("formatTextBox"), QStringLiteral("formatShape"),
            QStringLiteral("resetPlaceholderFill"), QStringLiteral("resetPlaceholderOutline"),
            QStringLiteral("resetTextInheritance"), QStringLiteral("formatImage"),
            QStringLiteral("transformShape"), QStringLiteral("deleteShape"), QStringLiteral("duplicateShape"),
            QStringLiteral("moveShape"), QStringLiteral("background"), QStringLiteral("alignShape"),
            QStringLiteral("slideHidden"), QStringLiteral("setSlideTransition"),
            QStringLiteral("formatTextStyle"), QStringLiteral("formatTableCell"),
            QStringLiteral("formatTableStyle"), QStringLiteral("moveGroup"), QStringLiteral("reorderGroup"),
            QStringLiteral("applyTheme"), QStringLiteral("resetTableCellFill"), QStringLiteral("insertTable"),
            QStringLiteral("insertTableRow"), QStringLiteral("insertTableColumn"),
            QStringLiteral("deleteTableRow"), QStringLiteral("deleteTableColumn"),
            QStringLiteral("mergeTableCell"), QStringLiteral("unmergeTableCell"),
            QStringLiteral("formatTableBorder"), QStringLiteral("resetTableBorder"),
            QStringLiteral("groupAdjacent"), QStringLiteral("addToGroup"), QStringLiteral("ungroup"),
            QStringLiteral("setClickAction"), QStringLiteral("applyFormat"),
            QStringLiteral("replaceTextMatches")};
        if (!actions.count(action))
        {
            setError(QStringLiteral("未知的演示文稿编辑操作。"));
            return false;
        }
        const auto invalid_option = presentation_option_error(action, options);
        if (!invalid_option.isEmpty())
        {
            setError(QStringLiteral("编辑参数名称或类型无效：%1").arg(invalid_option));
            return false;
        }
        static const std::set<QString> selection_actions{QStringLiteral("updateText"),
            QStringLiteral("formatText"), QStringLiteral("formatParagraph"), QStringLiteral("formatTextBox"),
            QStringLiteral("formatShape"), QStringLiteral("resetPlaceholderFill"),
            QStringLiteral("resetPlaceholderOutline"), QStringLiteral("formatImage"),
            QStringLiteral("resetTextInheritance"), QStringLiteral("transformShape"),
            QStringLiteral("deleteShape"), QStringLiteral("duplicateShape"), QStringLiteral("moveShape"),
            QStringLiteral("alignShape"), QStringLiteral("formatTextStyle"),
            QStringLiteral("formatTableCell"), QStringLiteral("formatTableStyle"),
            QStringLiteral("insertTableRow"), QStringLiteral("resetTableCellFill"),
            QStringLiteral("insertTableColumn"), QStringLiteral("deleteTableRow"),
            QStringLiteral("deleteTableColumn"), QStringLiteral("mergeTableCell"),
            QStringLiteral("unmergeTableCell"), QStringLiteral("formatTableBorder"),
            QStringLiteral("resetTableBorder"), QStringLiteral("groupAdjacent"), QStringLiteral("addToGroup"),
            QStringLiteral("setClickAction"), QStringLiteral("applyFormat")};
        if (selection_actions.count(action) && selected_shape_ < 0)
        {
            setError(QStringLiteral("请先在当前幻灯片中选择对象。"));
            return false;
        }
        if (action == QStringLiteral("addSlide"))
        {
            const QString layout = options.value(QStringLiteral("layout")).toString();
            if (!presentation_slide_layout(layout))
            {
                setError(QStringLiteral("不支持此幻灯片版式。"));
                return false;
            }
        }
        if (action == QStringLiteral("replaceTextMatches"))
        {
            const QString scope = options.value(QStringLiteral("scope")).toString();
            if (!options.contains(QStringLiteral("expectedGeneration")) ||
                options.value(QStringLiteral("expectedGeneration")).toString() !=
                    QString::number(generation_) ||
                (scope != QStringLiteral("all") && scope != QStringLiteral("match")))
            {
                setError(QStringLiteral("查找结果已失效，请重新查找后再替换。"));
                return false;
            }
        }
        auto command = presentation_edit_command(action, options, current_slide_, selected_shape_);
        if (action == QStringLiteral("applyFormat"))
        {
            const auto source_id = options.value(QStringLiteral("sourceId")).toString();
            bool found = false;
            if (scene_ && !source_id.isEmpty())
                for (std::size_t page = 0; page < scene_->slides.size() && !found; ++page)
                    for (std::size_t index = 0; index < scene_->slides[page].shapes.size(); ++index)
                        if (QString::number(scene_->slides[page].shapes[index].id) == source_id)
                        {
                            command.format_source_slide = page;
                            command.format_source_shape = index;
                            found = true;
                            break;
                        }
            if (!found)
            {
                setError(QStringLiteral("格式刷源对象已失效，请重新取样。"));
                return false;
            }
        }
        if (action == QStringLiteral("formatTextStyle"))
        {
            const auto style = presentation_text_style_command(options);
            if (!style)
            {
                setError(QStringLiteral("文字效果参数格式无效。"));
                return false;
            }
            command.text_style = *style;
        }
        return commitEdit(command);
    }

    void PresentationBridge::pushUndo(const std::shared_ptr<PresentationScene>& scene,
        std::uint64_t generation, const PresentationScene* retained)
    {
        const auto structural = scene_bytes(*scene);
        const auto detached = detached_image_bytes(*scene, retained);
        if (structural > maximum_history_bytes || detached > maximum_history_bytes - structural)
        {
            return;
        }
        const auto bytes = structural + detached;
        undo_.push_back({scene, generation, bytes, document_->thumbnail_revisions});
        undo_bytes_ += bytes;
        trimHistory(undo_, undo_bytes_);
    }

    void PresentationBridge::pushRedo(const std::shared_ptr<PresentationScene>& scene,
        std::uint64_t generation, const PresentationScene* retained)
    {
        const auto structural = scene_bytes(*scene);
        const auto detached = detached_image_bytes(*scene, retained);
        if (structural > maximum_history_bytes || detached > maximum_history_bytes - structural)
        {
            return;
        }
        const auto bytes = structural + detached;
        redo_.push_back({scene, generation, bytes, document_->thumbnail_revisions});
        redo_bytes_ += bytes;
        trimHistory(redo_, redo_bytes_);
    }

    void PresentationBridge::trimHistory(std::deque<HistoryEntry>& history, std::size_t& bytes)
    {
        while (
            !history.empty() && (history.size() > maximum_history_entries || bytes > maximum_history_bytes))
        {
            bytes -= history.front().bytes;
            history.pop_front();
        }
    }

    bool PresentationBridge::commitEdit(const PresentationEditCommand& command)
    {
        if (!editable() || locked() || !scene_)
        {
            if (active_ && !editable())
            {
                setError(QStringLiteral("此文件以只读方式打开。请先创建可编辑副本。"));
            }
            return false;
        }
        const bool had_source_package = static_cast<bool>(scene_->source_package);
        if (had_source_package && pending_edits_.size() >= maximum_pending_edits)
        {
            setError(QStringLiteral("后台编辑队列已满，请等待当前操作完成。"));
            return false;
        }
        if (std::any_of(pending_edits_.begin(), pending_edits_.end(), [](const auto& pending)
        {
            return reparses_document(pending.command.action);
        }))
        {
            setError(QStringLiteral("文稿结构正在更新，请等待当前同步完成。"));
            return false;
        }
        const bool trace_delete =
            command.action == PresentationEditAction::DeleteShape && pptxBridgeLatency().isDebugEnabled();
        QElapsedTimer edit_timer;
        if (trace_delete)
        {
            edit_timer.start();
            qCDebug(pptxBridgeLatency) << "delete.begin" << current_slide_ << selected_shape_;
        }
        std::shared_ptr<PresentationScene> next;
        try
        {
            next = std::make_shared<PresentationScene>(*scene_);
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("没有足够内存完成此编辑操作。"));
            return false;
        }
        PresentationEditResult result;
        try
        {
            result = had_source_package ? apply_presentation_model_edit(*next, command)
                                        : apply_presentation_edit(*next, command);
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("没有足够内存完成此编辑操作。"));
            return false;
        }
        if (result.error != PresentationEditError::None)
        {
            setError(edit_error(result.error, result.message));
            return false;
        }
        if (scene_bytes(*next) > maximum_editable_scene_bytes)
        {
            setError(QStringLiteral("此操作会超过演示文稿编辑内存上限。"));
            return false;
        }
        RenderPresentationPtr next_document;
        try
        {
            if (reparses_document(command.action) && had_source_package)
                next_document = document_;
            else if (reparses_document(command.action))
            {
                PresentationPrepareOptions options;
                options.environment = render_environment_;
                options.eager_image_analysis = false;
                next_document = prepare_presentation(next, document_, options);
            }
            else
                next_document =
                    prepare_presentation(next, document_, edit_render_options(document_, command, result));
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("没有足够内存准备编辑后的页面。"));
            return false;
        }
        if (trace_delete)
        {
            qCDebug(pptxBridgeLatency) << "delete.prepared.ms" << edit_timer.elapsed();
        }
        PendingEdit pending;
        if (had_source_package)
        {
            pending.command = command;
            pending.undo_before = undo_;
            pending.undo_bytes_before = undo_bytes_;
            pending.redo_before = redo_;
            pending.redo_bytes_before = redo_bytes_;
            pending.slide_before = current_slide_;
            pending.selection_before = selected_id(*scene_, current_slide_, selected_shape_);
            const auto source = selected_source(*scene_, current_slide_, selected_shape_);
            pending.selection_source_part = source.first;
            pending.selection_source_id = source.second;
        }
        pushUndo(scene_, generation_, next.get());
        redo_.clear();
        redo_bytes_ = 0;
        scene_ = std::move(next);
        generation_ = next_generation_++;
        current_slide_ = static_cast<int>(result.slide_index);
        selected_shape_ = result.shape_index ? static_cast<int>(*result.shape_index) : -1;
        clearMessage();
        clearError();
        document_ = std::move(next_document);
        if (had_source_package)
        {
            pending.generation = generation_;
            pending_edits_.push_back(std::move(pending));
        }
        else
        {
            committed_scene_ = scene_;
            committed_generation_ = generation_;
        }
        if (trace_delete)
        {
            qCDebug(pptxBridgeLatency) << "delete.before-document-signal.ms" << edit_timer.elapsed();
        }
        emit documentChanged();
        if (trace_delete)
        {
            qCDebug(pptxBridgeLatency) << "delete.after-document-signal.ms" << edit_timer.elapsed();
        }
        emit selectionChanged();
        emit stateChanged();
        startNextEditCommit();
        if (trace_delete)
        {
            qCDebug(pptxBridgeLatency) << "delete.return.ms" << edit_timer.elapsed();
        }
        return true;
    }

    void PresentationBridge::startNextEditCommit()
    {
        if (pending_edits_.empty() || edit_worker_.isRunning())
        {
            return;
        }
        const auto base = committed_scene_;
        const auto command = pending_edits_.front().command;
        const auto generation = pending_edits_.front().generation;
        edit_worker_.setFuture(QtConcurrent::run(&edit_pool_, [base, command, generation]()
        {
            EditCommitResult committed;
            committed.generation = generation;
            if (!base)
            {
                committed.result.error = PresentationEditError::InvalidValue;
                committed.result.message = "后台编辑缺少已提交的基础版本。";
                return committed;
            }
            try
            {
                committed.scene = std::make_shared<PresentationScene>(*base);
                committed.result = apply_presentation_edit(*committed.scene, command);
                if (committed.result.error != PresentationEditError::None)
                {
                    committed.scene.reset();
                }
            }
            catch (const std::bad_alloc&)
            {
                committed.scene.reset();
                committed.result.error = PresentationEditError::TooLarge;
                committed.result.message = "没有足够内存提交此编辑操作。";
            }
            catch (...)
            {
                committed.scene.reset();
                committed.result.error = PresentationEditError::InvalidValue;
                committed.result.message = "后台提交编辑时发生异常。";
            }
            return committed;
        }));
    }

    void PresentationBridge::completeEditCommit()
    {
        const bool trace = pptxBridgeLatency().isDebugEnabled();
        QElapsedTimer commit_timer;
        if (trace)
        {
            commit_timer.start();
            qCDebug(pptxBridgeLatency) << "edit-complete.begin";
        }
        EditCommitResult committed;
        try
        {
            committed = edit_worker_.future().takeResult();
        }
        catch (...)
        {
            committed.result.error = PresentationEditError::InvalidValue;
            committed.result.message = "无法取得后台编辑结果。";
        }
        if (trace)
        {
            qCDebug(pptxBridgeLatency) << "edit-complete.result.ms" << commit_timer.elapsed();
        }
        if (pending_edits_.empty())
        {
            emit stateChanged();
            return;
        }
        const auto pending = pending_edits_.front();
        if (!committed.scene || committed.result.error != PresentationEditError::None ||
            committed.generation != pending.generation)
        {
            auto failure = committed.result;
            if (failure.error == PresentationEditError::None)
            {
                failure.error = PresentationEditError::InvalidValue;
                failure.message = "后台编辑顺序校验失败。";
            }
            rollbackPendingEdits(pending, failure);
            return;
        }
        RenderPresentationPtr refreshed_document;
        if (reparses_document(pending.command.action))
        {
            try
            {
                PresentationPrepareOptions options;
                options.environment = render_environment_;
                options.eager_image_analysis = false;
                refreshed_document = prepare_presentation(committed.scene, document_, options);
            }
            catch (const std::bad_alloc&)
            {
                PresentationEditResult failure;
                failure.error = PresentationEditError::TooLarge;
                failure.message = "没有足够内存显示更新后的演示文稿。";
                rollbackPendingEdits(pending, failure);
                return;
            }
        }
        committed_scene_ = std::move(committed.scene);
        committed_generation_ = committed.generation;
        updateCommittedHistory(committed_generation_, committed_scene_);
        if (trace)
        {
            qCDebug(pptxBridgeLatency) << "edit-complete.history.ms" << commit_timer.elapsed();
        }
        pending_edits_.pop_front();
        if (pending_edits_.empty() && generation_ == committed_generation_)
        {
            scene_ = committed_scene_;
            if (refreshed_document)
            {
                document_ = std::move(refreshed_document);
                if (pending.command.action == PresentationEditAction::ReorderGroup ||
                    pending.command.action == PresentationEditAction::SetSlideTransition ||
                    pending.command.action == PresentationEditAction::FormatTableStyle ||
                    pending.command.action == PresentationEditAction::InsertTableRow ||
                    pending.command.action == PresentationEditAction::InsertTableColumn ||
                    pending.command.action == PresentationEditAction::MergeTableCell ||
                    pending.command.action == PresentationEditAction::UnmergeTableCell)
                    restore_selection(*scene_, pending.selection_before, current_slide_, selected_shape_,
                        pending.selection_source_part, pending.selection_source_id);
                else
                    selected_shape_ = -1;
                emit documentChanged();
                emit selectionChanged();
            }
        }
        emit stateChanged();
        startNextEditCommit();
        if (trace)
        {
            qCDebug(pptxBridgeLatency) << "edit-complete.end.ms" << commit_timer.elapsed();
        }
    }

    void PresentationBridge::updateCommittedHistory(
        std::uint64_t generation, const std::shared_ptr<PresentationScene>& scene)
    {
        const auto update = [generation, &scene](std::deque<HistoryEntry>& history)
        {
            for (auto& entry : history)
            {
                if (entry.generation == generation)
                {
                    entry.scene = scene;
                }
            }
        };
        update(undo_);
        update(redo_);
        for (auto& pending : pending_edits_)
        {
            update(pending.undo_before);
            update(pending.redo_before);
        }
    }

    void PresentationBridge::rollbackPendingEdits(
        const PendingEdit& failed, const PresentationEditResult& result)
    {
        undo_ = failed.undo_before;
        undo_bytes_ = failed.undo_bytes_before;
        redo_ = failed.redo_before;
        redo_bytes_ = failed.redo_bytes_before;
        pending_edits_.clear();
        scene_ = committed_scene_;
        generation_ = committed_generation_;
        current_slide_ = failed.slide_before;
        selected_shape_ = -1;
        if (scene_)
        {
            restore_selection(*scene_, failed.selection_before, current_slide_, selected_shape_,
                failed.selection_source_part, failed.selection_source_id);
            try
            {
                PresentationPrepareOptions options;
                options.environment = render_environment_;
                options.eager_image_analysis = false;
                document_ = prepare_presentation(scene_, document_, options);
            }
            catch (...)
            {
                document_.reset();
            }
        }
        setError(edit_error(result.error, result.message));
        emit documentChanged();
        emit selectionChanged();
        emit stateChanged();
    }

    void PresentationBridge::addImage(const QUrl& url)
    {
        loadImage(url, false);
    }

    void PresentationBridge::replaceImage(const QUrl& url)
    {
        if (!scene_ || current_slide_ < 0 || current_slide_ >= slideCount() || selected_shape_ < 0 ||
            selected_shape_ >=
                static_cast<int>(scene_->slides[static_cast<std::size_t>(current_slide_)].shapes.size()) ||
            scene_->slides[static_cast<std::size_t>(current_slide_)]
                .shapes[static_cast<std::size_t>(selected_shape_)]
                .image_path.empty())
        {
            setError(QStringLiteral("请先选择要替换的图片。"));
            return;
        }
        loadImage(url, true);
    }

    void PresentationBridge::loadImage(const QUrl& url, bool replacement)
    {
        if (!editable() || locked() || !url.isLocalFile() || url.toLocalFile().isEmpty())
        {
            return;
        }
        busy_ = true;
        replacing_image_ = replacement;
        operation_ = Operation::ImageLoad;
        clearMessage();
        clearError();
        emit stateChanged();
        const std::string path = narrow(QFileInfo(url.toLocalFile()).absoluteFilePath());
        image_worker_.setFuture(QtConcurrent::run([path]()
        {
            return load_presentation_image_file(path);
        }));
    }

    void PresentationBridge::completeImageLoad()
    {
        PresentationImageFileResult loaded;
        try
        {
            loaded = image_worker_.future().takeResult();
        }
        catch (...)
        {
            loaded.error = PresentationError::InvalidImage;
        }
        busy_ = false;
        operation_ = Operation::None;
        const bool replacement = replacing_image_;
        replacing_image_ = false;
        if (loaded.error != PresentationError::None)
        {
            setError(presentation_error(loaded.error));
            emit stateChanged();
            return;
        }
        PresentationEditCommand command;
        command.action =
            replacement ? PresentationEditAction::ReplaceImage : PresentationEditAction::AddImage;
        command.slide_index = static_cast<std::size_t>(std::max(0, current_slide_));
        command.shape_index = static_cast<std::size_t>(std::max(0, selected_shape_));
        command.image_path = std::move(loaded.path);
        command.image_mime_type = std::move(loaded.mime_type);
        command.image_bytes = std::move(loaded.bytes);
        if (!replacement)
        {
            const double scale = std::min({1.0, 480.0 / loaded.width, 270.0 / loaded.height});
            command.width = loaded.width * scale;
            command.height = loaded.height * scale;
        }
        commitEdit(command);
    }

    void PresentationBridge::undo()
    {
        if (!canUndo())
        {
            return;
        }
        const auto previous = undo_.back();
        RenderPresentationPtr previous_document;
        try
        {
            PresentationPrepareOptions options;
            options.thumbnail_revisions = previous.thumbnail_revisions;
            previous_document = prepare_presentation(previous.scene, document_, options);
            pushRedo(scene_, generation_, previous.scene.get());
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("没有足够内存撤销此操作。"));
            return;
        }
        catch (...)
        {
            setError(QStringLiteral("无法准备撤销后的页面。"));
            return;
        }
        undo_.pop_back();
        undo_bytes_ -= previous.bytes;
        const auto identity = selected_id(*scene_, current_slide_, selected_shape_);
        const auto source = selected_source(*scene_, current_slide_, selected_shape_);
        scene_ = previous.scene;
        generation_ = previous.generation;
        committed_scene_ = scene_;
        committed_generation_ = generation_;
        restore_selection(*scene_, identity, current_slide_, selected_shape_, source.first, source.second);
        document_ = std::move(previous_document);
        emit documentChanged();
        emit selectionChanged();
        emit stateChanged();
    }

    void PresentationBridge::redo()
    {
        if (!canRedo())
        {
            return;
        }
        const auto next = redo_.back();
        RenderPresentationPtr next_document;
        try
        {
            PresentationPrepareOptions options;
            options.thumbnail_revisions = next.thumbnail_revisions;
            next_document = prepare_presentation(next.scene, document_, options);
            pushUndo(scene_, generation_, next.scene.get());
        }
        catch (const std::bad_alloc&)
        {
            setError(QStringLiteral("没有足够内存重做此操作。"));
            return;
        }
        catch (...)
        {
            setError(QStringLiteral("无法准备重做后的页面。"));
            return;
        }
        redo_.pop_back();
        redo_bytes_ -= next.bytes;
        const auto identity = selected_id(*scene_, current_slide_, selected_shape_);
        const auto source = selected_source(*scene_, current_slide_, selected_shape_);
        scene_ = next.scene;
        generation_ = next.generation;
        committed_scene_ = scene_;
        committed_generation_ = generation_;
        restore_selection(*scene_, identity, current_slide_, selected_shape_, source.first, source.second);
        document_ = std::move(next_document);
        emit documentChanged();
        emit selectionChanged();
        emit stateChanged();
    }

    void PresentationBridge::setZoom(qreal zoom)
    {
        if (locked() || !std::isfinite(zoom))
        {
            return;
        }
        const qreal bounded = zoom == 0 ? 0 : std::clamp(zoom, qreal(0.25), qreal(3));
        if (bounded != zoom_)
        {
            zoom_ = bounded;
            emit stateChanged();
        }
    }

    bool PresentationBridge::setGuideSettings(const QVariantMap& patch)
    {
        if (!active_ || locked() || !guide_settings_.update(patch, slideWidth(), slideHeight()))
        {
            setError(QStringLiteral("标尺、网格或参考线设置无效。"));
            return false;
        }
        clearError();
        emit stateChanged();
        return true;
    }

    void PresentationBridge::save()
    {
        if (!active_ || locked())
        {
            return;
        }
        if (syncing())
        {
            setMessage(QStringLiteral("正在后台提交编辑，请稍后保存。"));
            return;
        }
        if (!editable())
        {
            createEditableCopy();
        }
        else if (path_.isEmpty())
        {
            openSaveDialog(false);
        }
        else
        {
            beginSave(path_, false);
        }
    }

    void PresentationBridge::saveAs()
    {
        if (!active_ || locked())
        {
            return;
        }
        if (syncing())
        {
            setMessage(QStringLiteral("正在后台提交编辑，请稍后另存。"));
            return;
        }
        if (!editable())
        {
            createEditableCopy();
        }
        else
        {
            openSaveDialog(false);
        }
    }

    bool PresentationBridge::saveTo(const QUrl& destination)
    {
        if (!active_ || !editable() || locked() || syncing() || save_dialog_open_ ||
            !destination.isLocalFile())
        {
            setError(QStringLiteral("请等待编辑完成，并为可编辑 PPT 指定本地新文件路径。"));
            return false;
        }
        const QFileInfo file(destination.toLocalFile());
        if (!file.isAbsolute() || file.suffix().compare("pptx", Qt::CaseInsensitive) != 0 || file.exists() ||
            !file.dir().exists() ||
            (!imported_source_path_.isEmpty() &&
                same_document_path(file.absoluteFilePath(), imported_source_path_)))
        {
            setError(QStringLiteral("保存目标必须是已有目录下尚不存在的 .pptx 文件，不能覆盖原文件。"));
            return false;
        }
        beginSave(file.absoluteFilePath(), false, true);
        return true;
    }

    void PresentationBridge::openSaveDialog(bool conversion)
    {
        save_dialog_open_ = true;
        save_dialog_conversion_ = conversion;
        emit stateChanged();
        emit saveDialogRequested();
    }

    void PresentationBridge::selectSaveFile(const QUrl& url)
    {
        if (!save_dialog_open_)
        {
            return;
        }
        const bool conversion = save_dialog_conversion_;
        save_dialog_open_ = false;
        save_dialog_conversion_ = false;
        emit stateChanged();
        if (!url.isLocalFile() || url.toLocalFile().isEmpty())
        {
            cancelSaveDialog();
            return;
        }
        QString destination = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (QFileInfo(destination).suffix().isEmpty())
        {
            destination += QStringLiteral(".pptx");
        }
        if (!imported_source_path_.isEmpty() && same_document_path(destination, imported_source_path_))
        {
            setError(QStringLiteral("可编辑副本及后续另存不能覆盖导入的原文件，请选择其他名称或位置。"));
            return;
        }
        beginSave(destination, conversion);
    }

    void PresentationBridge::cancelSaveDialog()
    {
        save_dialog_open_ = false;
        save_dialog_conversion_ = false;
        pending_action_ = Action::None;
        pending_url_ = {};
        emit stateChanged();
    }

    void PresentationBridge::beginSave(const QString& path, bool conversion, bool new_file)
    {
        if (!scene_)
        {
            return;
        }
        if (conversion)
        {
            loading_progress_ = 0;
            loading_stage_ = QStringLiteral("正在准备 PPTX 可编辑副本");
        }
        busy_ = true;
        operation_ = Operation::Save;
        saving_conversion_ = conversion;
        pending_path_ = path;
        clearMessage();
        clearError();
        emit stateChanged();
        if (conversion)
        {
            emit loadStarted();
        }
        const auto source = scene_;
        const std::string utf8_path = narrow(path);
        std::string expected_revision =
            !conversion && same_document_path(path_, path) ? disk_revision_ : std::string{};
        if (new_file)
            expected_revision = "missing";
        const QPointer<PresentationBridge> guard(this);
        worker_.setFuture(QtConcurrent::run([source, conversion, utf8_path, expected_revision, guard]()
        {
            auto scene = conversion ? std::make_shared<PresentationScene>(*source) : source;
            if (conversion)
            {
                scene->native_editable = true;
                scene->warnings.push_back(
                    "此副本保留原始文件部件；支持普通组合内部和表格文字编辑，母版与未支持的复杂对象仍锁定。");
            }
            auto package = serialize_presentation(*scene);
            if (package.error != PresentationError::None)
            {
                PresentationResult result;
                result.error = package.error;
                result.message = std::move(package.message);
                return SaveResult{std::move(result), {}};
            }
            const auto progress = [conversion, guard](auto stage, auto completed, auto total)
            {
                if (!conversion || !guard)
                    return;
                const auto value = total ? qreal(completed) / total : 1.0;
                const auto amount =
                    stage == OfficeSaveStage::Committing ? 0.95 + 0.04 * value : 0.20 + 0.75 * value;
                const auto text = stage == OfficeSaveStage::Committing
                    ? QStringLiteral("正在提交 PPTX 副本")
                    : QStringLiteral("正在写入 PPTX 副本 %1/%2").arg(completed).arg(total);
                QMetaObject::invokeMethod(guard, [guard, text, amount]()
                {
                    if (guard && guard->saving_conversion_ && guard->operation_ == Operation::Save)
                        guard->setLoadingProgress(text, amount);
                }, Qt::QueuedConnection);
            };
            return SaveResult{save_presentation_file(utf8_path, package.parts, expected_revision, progress),
                std::move(scene)};
        }));
    }

    void PresentationBridge::requestAction(Action action, const QUrl& destination)
    {
        if (locked())
        {
            return;
        }
        if (syncing())
        {
            setMessage(QStringLiteral("正在后台提交编辑，完成后再切换文档。"));
            return;
        }
        pending_action_ = action;
        pending_url_ = destination;
        if (modified())
        {
            confirmation_open_ = true;
            emit stateChanged();
            emit confirmUnsavedRequested();
            return;
        }
        performPendingAction();
    }

    void PresentationBridge::performPendingAction()
    {
        const auto action = pending_action_;
        const auto destination = pending_url_;
        pending_action_ = Action::None;
        pending_url_ = {};
        switch (action)
        {
        case Action::Open:
            beginLoad(destination.toLocalFile());
            return;
        case Action::New:
        {
            std::shared_ptr<PresentationScene> scene;
            RenderPresentationPtr document;
            try
            {
                scene =
                    std::make_shared<PresentationScene>(make_presentation(PresentationSlideLayout::Title));
                document = prepare_presentation(scene, document_);
            }
            catch (const std::bad_alloc&)
            {
                setError(QStringLiteral("没有足够内存新建演示文稿。"));
                return;
            }
            catch (...)
            {
                setError(QStringLiteral("无法准备新演示文稿。"));
                return;
            }
            path_.clear();
            disk_revision_.clear();
            imported_source_path_.clear();
            activateScene(std::move(scene), std::move(document));
            clearMessage();
            clearError();
            emit documentActivated();
            return;
        }
        case Action::Home:
            clear();
            return;
        case Action::Handoff:
            busy_ = true;
            operation_ = Operation::Handoff;
            emit stateChanged();
            emit handoffRequested(destination);
            return;
        case Action::Quit:
            emit windowCloseAllowed();
            return;
        case Action::None:
            return;
        }
    }

    void PresentationBridge::resolveUnsaved(const QString& decision)
    {
        if (!confirmation_open_)
        {
            return;
        }
        confirmation_open_ = false;
        emit stateChanged();
        if (decision == QStringLiteral("save"))
        {
            save();
        }
        else if (decision == QStringLiteral("discard"))
        {
            performPendingAction();
        }
        else
        {
            pending_action_ = Action::None;
            pending_url_ = {};
        }
    }

    void PresentationBridge::requestHandoff(const QUrl& destination)
    {
        if (destination.isValid() && !destination.isEmpty())
        {
            requestAction(Action::Handoff, destination);
        }
    }

    void PresentationBridge::finishHandoff(bool accepted)
    {
        if (operation_ != Operation::Handoff)
        {
            return;
        }
        operation_ = Operation::None;
        busy_ = false;
        if (accepted)
        {
            resetDocument();
            clearMessage();
            clearError();
            emit documentChanged();
            emit selectionChanged();
        }
        emit stateChanged();
    }

    bool PresentationBridge::requestWindowClose()
    {
        if (locked() || syncing())
        {
            if (syncing())
            {
                setMessage(QStringLiteral("正在后台提交编辑，完成后即可关闭。"));
            }
            return false;
        }
        if (!modified())
        {
            return true;
        }
        requestAction(Action::Quit);
        return false;
    }

    void PresentationBridge::beginLoad(const QString& path)
    {
        busy_ = true;
        operation_ = Operation::Load;
        pending_path_ = QFileInfo(path).absoluteFilePath();
        loading_stage_ = QStringLiteral("正在读取演示文稿");
        loading_progress_ = 0;
        clearMessage();
        clearError();
        emit stateChanged();
        emit loadStarted();
        const std::string utf8_path = narrow(pending_path_);
        const auto environment = render_environment_;
        const QPointer<PresentationBridge> guard(this);
        load_worker_.setFuture(QtConcurrent::run([utf8_path, environment, guard]()
        {
            PreparedPresentationLoad prepared;
            const auto post_progress = [guard](const PresentationLoadProgress& progress)
            {
                if (!guard)
                {
                    return;
                }
                QMetaObject::invokeMethod(guard, [guard, progress]()
                {
                    if (guard)
                    {
                        guard->updateLoadingProgress(progress);
                    }
                }, Qt::QueuedConnection);
            };
            prepared.result = load_presentation_file(utf8_path, post_progress);
            if (prepared.result.error != PresentationError::None)
            {
                return prepared;
            }
            try
            {
                prepared.scene = std::make_shared<PresentationScene>(std::move(prepared.result.scene));
                PresentationPrepareOptions options;
                options.environment = environment;
                options.eager_image_analysis = false;
                options.progress = [guard](std::size_t completed, std::size_t total)
                {
                    if (!guard)
                    {
                        return;
                    }
                    const qreal fraction =
                        total > 0 ? std::clamp(static_cast<qreal>(completed) / total, 0.0, 1.0) : 1;
                    QMetaObject::invokeMethod(guard, [guard, fraction]()
                    {
                        if (guard)
                        {
                            guard->setLoadingProgress(
                                QStringLiteral("正在准备字体与页面"), 0.90 + fraction * 0.07);
                        }
                    }, Qt::QueuedConnection);
                };
                prepared.document = prepare_presentation(prepared.scene, {}, options);
            }
            catch (const std::bad_alloc&)
            {
                prepared.scene.reset();
                prepared.document.reset();
                prepared.result.error = PresentationError::TooLarge;
                prepared.result.message = "没有足够内存准备此演示文稿。";
            }
            catch (...)
            {
                prepared.scene.reset();
                prepared.document.reset();
                prepared.result.error = PresentationError::ReadFailed;
                prepared.result.message = "无法准备此演示文稿的页面。";
            }
            return prepared;
        }));
    }

    void PresentationBridge::finishLoadingFrame()
    {
        if (!busy() && loading_progress_ >= 0.98 && loading_progress_ < 1.0)
            setLoadingProgress(QStringLiteral("PPTX 已准备完成"), 1.0);
    }

    void PresentationBridge::completeLoad()
    {
        PreparedPresentationLoad prepared;
        try
        {
            prepared = load_worker_.future().takeResult();
        }
        catch (...)
        {
            prepared.result.error = PresentationError::ReadFailed;
            prepared.result.message = "无法读取此演示文稿。";
        }
        if (operation_ != Operation::Load)
        {
            return;
        }
        if (prepared.result.error != PresentationError::None || !prepared.scene || !prepared.document)
        {
            busy_ = false;
            operation_ = Operation::None;
            QString detail = presentation_error(prepared.result.error);
            if (!prepared.result.message.empty())
            {
                detail = utf8(prepared.result.message);
            }
            pending_path_.clear();
            pending_action_ = Action::None;
            pending_url_ = {};
            loading_stage_.clear();
            loading_progress_ = 0;
            setError(detail);
            emit stateChanged();
            emit openCompleted(false);
            return;
        }

        setLoadingProgress(QStringLiteral("正在绘制第一页"), 0.98);
        path_ = pending_path_;
        imported_source_path_ = path_;
        disk_revision_ = std::move(prepared.result.revision);
        pending_path_.clear();
        activateScene(std::move(prepared.scene), std::move(prepared.document));
        busy_ = false;
        operation_ = Operation::None;
        setMessage(QStringLiteral("此文件以只读方式打开。创建可编辑副本后可修改当前支持的内容。"));
        emit stateChanged();
        emit fileRecorded(path_);
        emit openCompleted(true);
        emit documentActivated();
    }

    void PresentationBridge::activateScene(
        std::shared_ptr<PresentationScene> scene, RenderPresentationPtr document)
    {
        scene_ = std::move(scene);
        committed_scene_ = scene_;
        document_ = std::move(document);
        active_ = true;
        generation_ = 0;
        next_generation_ = 1;
        saved_generation_ = 0;
        committed_generation_ = 0;
        current_slide_ = 0;
        selected_shape_ = !scene_->slides.empty() && !scene_->slides.front().shapes.empty() ? 0 : -1;
        zoom_ = 0;
        guide_settings_ = {};
        undo_.clear();
        redo_.clear();
        pending_edits_.clear();
        undo_bytes_ = 0;
        redo_bytes_ = 0;
        emit documentChanged();
        emit selectionChanged();
        emit stateChanged();
    }

    void PresentationBridge::refreshDocument(bool reuse_analysis, bool reuse_thumbnails)
    {
        PresentationPrepareOptions options;
        options.environment = render_environment_;
        options.reuse_analysis = reuse_analysis;
        options.reuse_thumbnails = reuse_thumbnails;
        document_ = scene_ ? prepare_presentation(scene_, document_, options) : RenderPresentationPtr{};
        emit documentChanged();
    }

    void PresentationBridge::completeOperation()
    {
        PresentationResult result;
        const bool conversion = saving_conversion_;
        try
        {
            auto saved = worker_.future().takeResult();
            result = std::move(saved.file);
            saving_scene_ = std::move(saved.scene);
        }
        catch (...)
        {
            result.error = PresentationError::WriteFailed;
        }
        busy_ = false;
        operation_ = Operation::None;
        if (result.error != PresentationError::None)
        {
            const QString detail =
                result.message.empty() ? presentation_error(result.error) : utf8(result.message);
            saving_scene_.reset();
            saving_conversion_ = false;
            pending_path_.clear();
            pending_action_ = Action::None;
            pending_url_ = {};
            setError(detail);
            emit stateChanged();
            if (conversion)
                emit copyCompleted(false);
            return;
        }

        path_ = pending_path_;
        pending_path_.clear();
        disk_revision_ = std::move(result.revision);
        if (saving_conversion_)
        {
            scene_ = std::move(saving_scene_);
            committed_scene_ = scene_;
            committed_generation_ = generation_;
            undo_.clear();
            redo_.clear();
            undo_bytes_ = 0;
            redo_bytes_ = 0;
            refreshDocument(true, true);
        }
        else
        {
            saving_scene_.reset();
        }
        saving_conversion_ = false;
        saved_generation_ = generation_;
        setMessage(QStringLiteral("已保存演示文稿。"));
        emit documentChanged();
        emit selectionChanged();
        emit stateChanged();
        emit fileRecorded(path_);
        if (conversion)
        {
            setLoadingProgress(QStringLiteral("PPTX 副本已准备完成"), 1);
            emit copyCompleted(true);
        }
        performPendingAction();
    }

    void PresentationBridge::failOpen(const QString& message)
    {
        busy_ = false;
        operation_ = Operation::None;
        pending_path_.clear();
        setError(message);
        emit stateChanged();
        emit openCompleted(false);
    }
}
