#include "office_ai_page_layout.hpp"
#include "office_ai_style.hpp"
#include "office_ai_toolbox.hpp"
#include "office_ai_workspace.hpp"

#include <mirrorfly/automation.hpp>
#include <mirrorfly/presentation_storage.hpp>

#include <QCryptographicHash>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtConcurrent>

#include <cmath>

namespace
{
    QJsonObject runtime()
    {
        return QJsonDocument::fromJson(QByteArray::fromStdString(mirrorfly::office_runtime_snapshot()))
            .object();
    }

    QJsonObject failure(const QString& code, const QString& hint)
    {
        return {{"ok", false}, {"error", code}, {"hint", hint}, {"executed", 0}};
    }

    QJsonArray preflight_images(const QJsonArray& pages)
    {
        QJsonArray sizes;
        for (const auto& value : pages)
        {
            const auto page = value.toObject();
            if (!page.contains("imagePath"))
            {
                sizes.append(QJsonObject{});
                continue;
            }
            const auto path = page.value("imagePath").toString();
            const QFileInfo file(path);
            if (!page.value("imagePath").isString() || !file.isAbsolute())
            {
                sizes.append(QJsonObject{{"ok", false}});
                continue;
            }
            try
            {
                const auto loaded = mirrorfly::load_presentation_image_file(path.toUtf8().toStdString());
                sizes.append(loaded.error == mirrorfly::PresentationError::None
                        ? QJsonObject{{"ok", true}, {"width", loaded.width}, {"height", loaded.height}}
                        : QJsonObject{{"ok", false}});
            }
            catch (...)
            {
                sizes.append(QJsonObject{{"ok", false}});
            }
        }
        return sizes;
    }

}

namespace mirrorfly
{
    void OfficeAiToolbox::executePages(
        const QString& name, const QJsonObject& arguments, OfficeAiSequence::Completion completion)
    {
        const auto pages = arguments.value("pages").toArray();
        bool has_image = false;
        if (name == "office_compose_slides")
            for (const auto& value : pages)
                has_image = has_image || value.toObject().contains("imagePath");
        if (!has_image)
        {
            executePagesReady(name, arguments, {}, std::move(completion));
            return;
        }
        const auto epoch = page_preflight_epoch_;
        auto* watcher = new QFutureWatcher<QJsonArray>(this);
        connect(watcher, &QFutureWatcher<QJsonArray>::finished, this,
            [this, watcher, epoch, name, arguments, completion = std::move(completion)]() mutable
        {
            QJsonArray image_sizes;
            try
            {
                image_sizes = watcher->future().takeResult();
            }
            catch (...)
            {
                watcher->deleteLater();
                if (epoch == page_preflight_epoch_)
                    completion(
                        failure("invalid_page_image", "Image preflight failed; no pages were changed."));
                return;
            }
            watcher->deleteLater();
            if (epoch == page_preflight_epoch_)
                executePagesReady(name, arguments, image_sizes, std::move(completion));
        });
        watcher->setFuture(QtConcurrent::run([pages]()
        {
            return preflight_images(pages);
        }));
    }

    void OfficeAiToolbox::executePagesReady(const QString& name, const QJsonObject& arguments,
        const QJsonArray& image_sizes, OfficeAiSequence::Completion completion)
    {
        for (const auto& key : arguments.keys())
            if (!QStringList{"batchId", "expectedRevision", "pages", "theme", "targetPages", "styleId"}
                    .contains(key) ||
                (name == "office_continue_slides" &&
                    (key == "pages" || key == "theme" || key == "targetPages" || key == "styleId")))
            {
                completion(failure("invalid_batch_field", key));
                return;
            }
        const auto state = runtime();
        const auto slides = state.value("modules").toObject().value("slides").toObject();
        const auto session = slides.value("documentSession").toString();
        const auto id = arguments.value("batchId").toString();
        if (id.isEmpty() || id.size() > 64 || !arguments.value("batchId").isString())
        {
            completion(failure("invalid_batch_id", "Use a unique short batchId for each 1..8 page group."));
            return;
        }
        auto input = arguments;
        input.remove("expectedRevision");
        const auto fingerprint = QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(input).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
                .toHex());
        if (page_batches_.contains(id))
        {
            const auto plan = page_batches_.value(id);
            if (plan.value("session") != session ||
                (name == "office_compose_slides" && plan.value("fingerprint") != fingerprint))
            {
                completion(
                    failure("batch_id_reused", "This batchId belongs to different content or document."));
                return;
            }
            if (plan.value("complete").toBool())
            {
                auto result = plan.value("result").toObject();
                result.insert("alreadyApplied", true);
                if (plan.value("generation") != slides.value("editGeneration"))
                {
                    result.insert("ok", false);
                    result.insert("complete", false);
                    result.insert("historicalReceipt", true);
                    result.insert("error", "completed_batch_changed");
                    result.insert("hint",
                        "This batch was applied earlier. Read current pages before "
                        "repairing; never recreate the batch.");
                }
                result.insert("revision", state.value("revision"));
                completion(result);
                return;
            }
            if (plan.value("blocked").toBool() || plan.value("revision") != state.value("revision"))
            {
                completion(failure("batch_state_changed",
                    "Earlier pages remain applied. Read current content and finish the incomplete page with "
                    "office_batch; do not recreate it."));
                return;
            }
        }
        else
        {
            if (name == "office_continue_slides")
            {
                completion(failure("unknown_batch", "No retained page batch has this ID."));
                return;
            }
            const auto pages = arguments.value("pages").toArray();
            if (!arguments.value("pages").isArray() || pages.isEmpty() || pages.size() > 8)
            {
                auto error = failure("invalid_page_batch_size",
                    "Submit 1..8 pages now; keep remaining pages for the next batchId. No pages changed.");
                error.insert("submittedPages", pages.size());
                error.insert("maximum", 8);
                completion(error);
                return;
            }
            if (!loaded_groups_.contains("slides") ||
                office_ai_workspace(state).value("currentModule") != "slides" ||
                !slides.value("editable").toBool())
            {
                completion(failure("new_presentation_required",
                    "Load slides and use an editable PPT. A saved imported file needs an editable copy."));
                return;
            }
            if (page_batches_.size() >= 128)
            {
                completion(failure(
                    "page_batch_limit", "Finish and save this presentation before starting another task."));
                return;
            }
            const QString style_id =
                arguments.value("styleId").toString(page_theme_.value("styleId").toString("modern"));
            if ((arguments.contains("styleId") && !arguments.value("styleId").isString()) ||
                !office_ai_style_catalog(style_id).value("ok").toBool() ||
                (!page_theme_.isEmpty() && page_theme_.value("styleId") != style_id))
            {
                auto error = failure("invalid_style",
                    "Use the existing styleId for this deck, or omit styleId to inherit it. New deck styles "
                    "come from office_style.");
                if (!page_theme_.isEmpty())
                    error.insert("expectedStyleId", page_theme_.value("styleId"));
                completion(error);
                return;
            }
            auto theme = page_theme_.isEmpty() ? office_ai_style_theme(style_id) : page_theme_;
            if (arguments.contains("theme") && !arguments.value("theme").isObject())
            {
                completion(
                    failure("invalid_theme", "theme is an object with optional ink, paper, accent colors."));
                return;
            }
            const auto patch = arguments.value("theme").toObject();
            for (auto it = patch.begin(); it != patch.end(); ++it)
            {
                if (!QStringList{"ink", "paper", "accent"}.contains(it.key()) || !it.value().isString() ||
                    !QRegularExpression("^#[0-9a-fA-F]{6}$").match(it.value().toString()).hasMatch())
                {
                    auto rejected = failure("invalid_theme",
                        "Theme colors need exactly # followed by six ASCII hex digits 0-9 or A-F, "
                        "for example #2E1F1A. Correct this field or omit theme for the default palette.");
                    rejected.insert("field", QStringLiteral("theme.%1").arg(it.key()));
                    completion(rejected);
                    return;
                }
                theme.insert(it.key(), it.value());
            }
            const bool first = slides.value("documentPath").toString().isEmpty() &&
                slides.value("editGeneration") == "0" && slides.value("slideCount") == 1;
            const int start = first ? 0 : slides.value("slideCount").toInt();
            if (deck_document_session_ != session)
            {
                deck_document_session_ = session;
                deck_target_pages_ = 0;
            }
            const auto target_value = arguments.value("targetPages");
            if (arguments.contains("targetPages") &&
                (!target_value.isDouble() || !std::isfinite(target_value.toDouble()) ||
                    std::floor(target_value.toDouble()) != target_value.toDouble() ||
                    target_value.toInt() < 1 || target_value.toInt() > 200))
            {
                completion(failure("invalid_deck_pages", "targetPages must be an integer from 1 to 200."));
                return;
            }
            const int target_pages =
                arguments.contains("targetPages") ? target_value.toInt() : deck_target_pages_;
            if (target_pages == 0)
            {
                completion(failure("deck_pages_required",
                    "Set targetPages to the total intended slide count on the first segment."));
                return;
            }
            if (requested_slide_pages_ > 0 && target_pages != requested_slide_pages_)
            {
                auto result = failure("requested_deck_pages_mismatch",
                    "The user explicitly requested this total slide count. Set targetPages to "
                    "expectedPages and plan segments to reach it.");
                result.insert("expectedPages", requested_slide_pages_);
                result.insert("receivedPages", target_pages);
                completion(result);
                return;
            }
            if (deck_target_pages_ != 0 && target_pages != deck_target_pages_)
            {
                auto result = failure("deck_page_plan_mismatch",
                    "Keep the original targetPages. The existing pages are unchanged.");
                result.insert("expectedPages", deck_target_pages_);
                result.insert("receivedPages", target_pages);
                result.insert("currentPages", start);
                completion(result);
                return;
            }
            const int remaining_pages = target_pages - start;
            if (remaining_pages == 0)
            {
                auto result = failure("deck_already_complete",
                    "The planned deck already has every page. Do not compose another segment; "
                    "save it or read and edit an existing page.");
                result.insert("targetPages", target_pages);
                result.insert("currentPages", start);
                result.insert("remainingPages", 0);
                completion(result);
                return;
            }
            if (pages.size() > remaining_pages)
            {
                auto result = failure("deck_page_plan_mismatch",
                    "This segment exceeds the remaining page count. Submit only the missing pages; "
                    "completed pages are unchanged.");
                result.insert("targetPages", target_pages);
                result.insert("currentPages", start);
                result.insert("remainingPages", remaining_pages);
                result.insert("submittedPages", pages.size());
                completion(result);
                return;
            }
            QJsonArray steps;
            QJsonArray receipts;
            for (int index = 0; index < pages.size(); ++index)
            {
                auto page = pages.at(index).toObject();
                QSize image_size;
                if (page.contains("imagePath"))
                {
                    const auto checked = image_sizes.at(index).toObject();
                    if (!checked.value("ok").toBool())
                    {
                        auto rejected = failure("invalid_page_image",
                            "The local image is unavailable, damaged or too large. No pages were changed.");
                        rejected.insert("pageIndex", index);
                        rejected.insert("field", "imagePath");
                        completion(rejected);
                        return;
                    }
                    image_size = QSize(checked.value("width").toInt(), checked.value("height").toInt());
                    const auto credit = image_library_.creditFor(page.value("imagePath").toString());
                    if (!credit.isEmpty())
                    {
                        const QString creator = credit.value("creator").toString();
                        const QString title = credit.value("title").toString();
                        page.insert("imageCredit",
                            QStringLiteral("图源：%1 · %2 · Openverse %3")
                                .arg(creator.isEmpty() ? title : creator,
                                    credit.value("license").toString().toUpper(),
                                    credit.value("id").toString().left(8)));
                    }
                }
                auto recipe = office_ai_page_recipe(page, theme, slides.value("slideWidth").toDouble(),
                    slides.value("slideHeight").toDouble(), start + index + 1, image_size);
                if (!recipe.value("ok").toBool())
                {
                    recipe.insert("pageIndex", index);
                    completion(recipe);
                    return;
                }
                recipe.remove("ok");
                recipe.insert("page", first && index == 0 ? 0 : -1);
                recipe.insert("expectedRevision", arguments.value("expectedRevision"));
                const auto prepared = prepareWorkflow("office_compose_slide", recipe);
                if (!prepared.value("ok").toBool())
                {
                    completion(prepared);
                    return;
                }
                auto page_steps = prepared.value("steps").toArray();
                if (!(first && index == 0))
                {
                    auto select = page_steps.first().toObject();
                    select.insert("args", QJsonArray{start + index - 1});
                    page_steps[0] = select;
                }
                for (const auto& step : page_steps)
                    steps.append(step);
                receipts.append(QJsonObject{{"page", start + index},
                    {"title", pages.at(index).toObject().value("title")},
                    {"elementCount", recipe.value("elements").toArray().size()}, {"endStep", steps.size()}});
            }
            page_theme_ = theme;
            deck_target_pages_ = target_pages;
            page_batches_.insert(id,
                QJsonObject{{"session", session}, {"fingerprint", fingerprint}, {"steps", steps},
                    {"receipts", receipts}, {"nextStep", 0}, {"revision", state.value("revision")},
                    {"startPage", start}, {"targetPages", target_pages}, {"styleId", style_id}});
        }
        const auto plan = page_batches_.value(id);
        const auto steps = plan.value("steps").toArray();
        QJsonArray remaining;
        const int begin = plan.value("nextStep").toInt();
        // A page recipe may expand into hundreds of public actions; chunk locally, not through the model.
        for (int index = begin; index < qMin(begin + 256, static_cast<int>(steps.size())); ++index)
            remaining.append(steps.at(index));
        active_page_batch_ = id;
        execute("office_batch",
            {{"steps", remaining}, {"expectedRevision", arguments.value("expectedRevision")}},
            [this, id, completion = std::move(completion)](const QJsonObject& result)
        {
            auto recorded = recordPageBatch(id, result);
            active_page_batch_.clear();
            auto plan = page_batches_.value(id);
            const int auto_continues = plan.value("autoContinues").toInt();
            if (recorded.value("ok").toBool() && !recorded.value("complete").toBool() &&
                recorded.value("remaining").toInt() > 0 && result.value("executed").toInt() > 0 &&
                auto_continues < plan.value("steps").toArray().size())
            {
                plan.insert("autoContinues", auto_continues + 1);
                page_batches_.insert(id, plan);
                executePages("office_continue_slides",
                    {{"batchId", id}, {"expectedRevision", recorded.value("revision")}},
                    std::move(completion));
                return;
            }
            completion(recorded);
        });
    }

    QJsonObject OfficeAiToolbox::recordPageBatch(const QString& id, const QJsonObject& result)
    {
        auto plan = page_batches_.value(id);
        int succeeded = 0;
        for (const auto& entry : result.value("results").toArray())
        {
            if (!entry.toObject().value("response").toObject().value("ok").toBool())
                break;
            ++succeeded;
        }
        const int next = plan.value("nextStep").toInt() + succeeded;
        const auto state = runtime();
        const auto slides = state.value("modules").toObject().value("slides").toObject();
        QJsonArray verified;
        bool mismatch = slides.value("documentSession") != plan.value("session");
        for (const auto& entry : plan.value("receipts").toArray())
        {
            auto receipt = entry.toObject();
            if (receipt.value("endStep").toInt() > next)
                break;
            const auto read = query("office_read",
                {{"module", "slides"}, {"view", "overview"}, {"offset", receipt.value("page")},
                    {"limit", 1}});
            const auto page = read.value("items").toArray().first().toObject();
            const bool invalid_page = !read.value("ok").toBool() ||
                page.value("index") != receipt.value("page") ||
                page.value("objects") != receipt.value("elementCount");
            if (invalid_page || mismatch)
            {
                mismatch = true;
                break;
            }
            receipt.remove("endStep");
            verified.append(receipt);
        }
        const int remaining = plan.value("steps").toArray().size() - next;
        const bool complete = result.value("ok").toBool() && remaining == 0 && !mismatch;
        int verified_pages = plan.value("startPage").toInt();
        if (!verified.isEmpty())
            verified_pages = verified.last().toObject().value("page").toInt() + 1;
        const int target_pages = plan.value("targetPages").toInt();
        QJsonObject receipt{{"ok", result.value("ok").toBool() && !mismatch}, {"batchId", id},
            {"error", mismatch ? QJsonValue("page_verification_mismatch") : result.value("error")},
            {"pages", verified}, {"complete", complete}, {"executed", next}, {"remaining", remaining},
            {"nextStep", next}, {"revision", state.value("revision")},
            {"documentSession", plan.value("session")}};
        auto timing = plan.value("result").toObject().value("timing").toObject();
        const auto segment_timing = result.value("timing").toObject();
        for (auto it = segment_timing.begin(); it != segment_timing.end(); ++it)
            timing.insert(it.key(), timing.value(it.key()).toDouble() + it.value().toDouble());
        receipt.insert("timing", timing);
        receipt.insert("deckProgress",
            QJsonObject{{"documentSession", plan.value("session")}, {"targetPages", target_pages},
                {"verifiedPages", verified_pages}, {"styleId", plan.value("styleId")}, {"lastBatchId", id},
                {"complete", complete && verified_pages >= target_pages}});
        if (complete && verified_pages >= target_pages)
            receipt.insert("next",
                "The planned deck is complete. Do not append pages. Save now, or read and edit "
                "an existing page if a correction is needed.");
        else if (complete)
            receipt.insert(
                "next", "This page segment is complete. Append only the remaining pages with a new batchId.");
        else
            receipt.insert("next",
                "Use office_continue_slides(batchId) only while state is unchanged; otherwise read and "
                "repair the incomplete page.");
        plan.insert("nextStep", next);
        const bool interrupted = result.value("error") == "interrupted" &&
            result.value("revision") == state.value("revision") && !slides.value("syncing").toBool() &&
            slides.value("pendingEdits").toInt() == 0;
        plan.insert("blocked", mismatch || (!result.value("ok").toBool() && !interrupted));
        plan.insert("revision", state.value("revision"));
        plan.insert("complete", complete);
        plan.insert("generation", slides.value("editGeneration"));
        plan.insert("result", receipt);
        page_batches_.insert(id, plan);
        if (!verified.isEmpty())
            emit milestone("presentation",
                QJsonObject{{"documentSession", plan.value("session")},
                    {"slideCount", slides.value("slideCount")},
                    {"lastCompletedPage", verified.last().toObject().value("page")},
                    {"generation", slides.value("editGeneration")}});
        return receipt;
    }
}
