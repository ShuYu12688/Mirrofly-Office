#include "automation_bridge.hpp"
#include "automation_contract.hpp"

#include <mirrorfly/automation.hpp>

#include <QByteArray>
#include <QCryptographicHash>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using mirrorfly::action_specs;
    using mirrorfly::ActionSpec;
    using mirrorfly::ArgumentKind;
    constexpr qsizetype maximum_request_bytes = 1024 * 1024;
    constexpr qsizetype maximum_response_bytes = 2 * 1024 * 1024;
    constexpr std::size_t maximum_json_values = 100000;
    constexpr std::size_t maximum_json_depth = 64;

    struct Registry
    {
        QMutex mutex;
        QPointer<mirrorfly::AutomationBridge> bridge;
        QThread* thread = nullptr;
        mirrorfly::OfficeChangeSubscription next_subscription = 1;
    };

    Registry& registry()
    {
        static Registry value;
        return value;
    }

    const std::set<std::string>& module_names()
    {
        static const std::set<std::string> names = {
            "app", "text", "word", "sheets", "slides", "pdf", "mindmap", "export", "images"};
        return names;
    }

    QJsonValue default_argument(const ActionSpec& spec, qsizetype index)
    {
        // Match the declared public selectCell(row, column, extend=false) interface.
        if (std::string_view(spec.module) == "sheets" && std::string_view(spec.action) == "selectCell" &&
            index == 2)
            return false;
        return QJsonValue::Undefined;
    }

    const std::vector<const char*>& properties_for(std::string_view module)
    {
        static const std::vector<const char*> app = {"ready", "notice", "recentFiles"};
        static const std::vector<const char*> text = {"zoom", "active", "modified", "busy", "locked",
            "markdown", "content", "revision", "documentName", "documentPath", "formatLabel", "saveUrl",
            "message"};
        static const std::vector<const char*> word = {"zoom", "active", "modified", "locked", "readOnly",
            "revision", "documentName", "documentPath", "message", "saveUrl", "statistics", "loadingProgress",
            "loadingStage", "formatReady"};
        static const std::vector<const char*> sheets = {"zoom", "active", "modified", "busy", "locked",
            "canUndo", "canRedo", "error", "documentName", "documentPath", "saveUrl", "sheetNames",
            "currentSheet", "rangeInfo", "cellInfo", "compatibilitySummary", "formatInfo", "revision",
            "layoutRevision"};
        static const std::vector<const char*> slides = {"active", "editable", "modified", "busy", "locked",
            "canUndo", "canRedo", "error", "message", "documentName", "documentPath", "slideCount",
            "hiddenSlides", "currentSlide", "selectedShape", "selection", "slideWidth", "slideHeight", "zoom",
            "fontSummary", "saveUrl", "loading", "loadingStage", "loadingProgress", "syncing", "pendingEdits",
            "editGeneration"};
        static const std::vector<const char*> canvas = {"zoom", "connectionMode", "connectionFrom", "active",
            "locked", "modified", "canUndo", "canRedo", "kind", "documentName", "documentPath", "message",
            "saveUrl", "viewData", "editGeneration", "selectedId", "currentPage", "pageCount"};
        static const std::vector<const char*> export_fields = {
            "active", "busy", "message", "completed", "total"};
        static const std::vector<const char*> empty;
        if (module == "app")
        {
            return app;
        }
        if (module == "text")
        {
            return text;
        }
        if (module == "word")
        {
            return word;
        }
        if (module == "sheets")
        {
            return sheets;
        }
        if (module == "slides")
        {
            return slides;
        }
        if (module == "pdf" || module == "mindmap")
        {
            return canvas;
        }
        if (module == "export")
            return export_fields;
        if (module == "images")
            return export_fields;
        return empty;
    }

    const char* kind_name(ArgumentKind kind)
    {
        switch (kind)
        {
        case ArgumentKind::String:
            return "string";
        case ArgumentKind::Url:
            return "local_file";
        case ArgumentKind::Integer:
            return "integer";
        case ArgumentKind::Boolean:
            return "boolean";
        case ArgumentKind::Number:
            return "finite_number";
        case ArgumentKind::Object:
            return "object";
        case ArgumentKind::Value:
            return "json_value";
        }
        return "unknown";
    }

    QByteArray compact(const QJsonObject& object)
    {
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }

    const char* result_kind(QMetaType type)
    {
        switch (type.id())
        {
        case QMetaType::Void:
            return "accepted_status";
        case QMetaType::Bool:
            return "boolean";
        case QMetaType::Int:
            return "integer";
        case QMetaType::Double:
            return "number";
        case QMetaType::QString:
            return "string";
        case QMetaType::QVariantMap:
            return "object";
        case QMetaType::QVariantList:
            return "array";
        default:
            return "json_value";
        }
    }

    std::string bounded_json(QJsonObject object)
    {
        QByteArray bytes = compact(object);
        if (bytes.size() <= maximum_response_bytes)
        {
            return bytes.toStdString();
        }
        object.insert("truncated", true);
        if (object.contains("modules") && object.value("modules").isObject())
        {
            auto modules = object.value("modules").toObject();
            while (compact(object).size() > maximum_response_bytes)
            {
                QString largest_module;
                QString largest_field;
                qsizetype largest_size = -1;
                for (auto module = modules.begin(); module != modules.end(); ++module)
                {
                    auto fields = module.value().toObject();
                    for (auto field = fields.begin(); field != fields.end(); ++field)
                    {
                        if (field.key() == "registered")
                        {
                            continue;
                        }
                        QJsonObject wrapper;
                        wrapper.insert("value", field.value());
                        const auto size = compact(wrapper).size();
                        if (size > largest_size)
                        {
                            largest_size = size;
                            largest_module = module.key();
                            largest_field = field.key();
                        }
                    }
                }
                if (largest_size < 0)
                {
                    break;
                }
                auto fields = modules.value(largest_module).toObject();
                fields.remove(largest_field);
                fields.insert("truncated", true);
                modules.insert(largest_module, fields);
                object.insert("modules", modules);
            }
        }
        if (compact(object).size() > maximum_response_bytes && object.contains("ui"))
        {
            object.insert("ui", QJsonObject{{"truncated", true}});
        }
        if (compact(object).size() > maximum_response_bytes && object.contains("result"))
        {
            object.insert("result", QJsonObject{{"truncated", true}});
        }
        bytes = compact(object);
        if (bytes.size() > maximum_response_bytes)
        {
            const QJsonObject failure{
                {"ok", false}, {"protocol", 1}, {"error", "response_too_large"}, {"truncated", true}};
            return compact(failure).toStdString();
        }
        return bytes.toStdString();
    }

    std::string error_json(const char* error, const std::string& revision = "0")
    {
        return bounded_json(QJsonObject{{"ok", false}, {"protocol", 1}, {"error", error},
            {"revision", QString::fromStdString(revision)}});
    }

    bool valid_json_complexity(const QJsonValue& root)
    {
        struct Pending
        {
            QJsonValue value;
            std::size_t depth = 0;
        };
        std::vector<Pending> pending{{root, 0}};
        std::size_t count = 0;
        while (!pending.empty())
        {
            auto item = std::move(pending.back());
            pending.pop_back();
            if (++count > maximum_json_values || item.depth > maximum_json_depth)
            {
                return false;
            }
            if (item.value.isArray())
            {
                const auto array = item.value.toArray();
                for (const auto& value : array)
                {
                    pending.push_back({value, item.depth + 1});
                }
            }
            else if (item.value.isObject())
            {
                const auto object = item.value.toObject();
                for (const auto& value : object)
                {
                    pending.push_back({value, item.depth + 1});
                }
            }
        }
        return true;
    }

    bool meta_type_matches(QMetaType type, ArgumentKind kind)
    {
        switch (kind)
        {
        case ArgumentKind::String:
            return type.id() == QMetaType::QString;
        case ArgumentKind::Url:
            return type.id() == QMetaType::QUrl;
        case ArgumentKind::Integer:
            return type.id() == QMetaType::Int;
        case ArgumentKind::Boolean:
            return type.id() == QMetaType::Bool;
        case ArgumentKind::Number:
            return type.id() == QMetaType::Double;
        case ArgumentKind::Object:
            return type.id() == QMetaType::QVariantMap;
        case ArgumentKind::Value:
            return type.id() == QMetaType::QVariant;
        }
        return false;
    }

    QMetaMethod find_method(QObject* object, const ActionSpec& spec)
    {
        if (!object)
            return {};
        const auto* meta = object->metaObject();
        for (int index = meta->methodCount() - 1; index >= QObject::staticMetaObject.methodCount(); --index)
        {
            const auto method = meta->method(index);
            if (method.access() != QMetaMethod::Public || method.name() != spec.method ||
                method.parameterCount() != static_cast<int>(spec.arguments.size()))
            {
                continue;
            }
            bool matches = true;
            for (int parameter = 0; parameter < method.parameterCount(); ++parameter)
            {
                matches = matches &&
                    meta_type_matches(method.parameterMetaType(parameter),
                        spec.arguments[static_cast<std::size_t>(parameter)]);
            }
            if (matches)
            {
                return method;
            }
        }
        return {};
    }

    struct PreparedArgument
    {
        QVariant value;
        bool pass_variant_object = false;
    };

    bool prepare_argument(const QJsonValue& json, ArgumentKind kind, PreparedArgument& result)
    {
        switch (kind)
        {
        case ArgumentKind::String:
            if (!json.isString())
            {
                return false;
            }
            result.value = json.toString();
            return true;
        case ArgumentKind::Url:
        {
            if (!json.isString() || json.toString().isEmpty())
            {
                return false;
            }
            const QString value = json.toString();
            QUrl url(value);
            const bool drive_path = value.size() > 2 && value[1] == ':';
            if (url.scheme().isEmpty() || drive_path)
            {
                url = QUrl::fromLocalFile(value);
            }
            if (!url.isValid() || !url.isLocalFile())
            {
                return false;
            }
            result.value = url;
            return true;
        }
        case ArgumentKind::Integer:
        {
            if (!json.isDouble())
            {
                return false;
            }
            const double number = json.toDouble();
            if (!std::isfinite(number) || std::floor(number) != number ||
                number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
            {
                return false;
            }
            result.value = static_cast<int>(number);
            return true;
        }
        case ArgumentKind::Boolean:
            if (!json.isBool())
            {
                return false;
            }
            result.value = json.toBool();
            return true;
        case ArgumentKind::Number:
            if (!json.isDouble() || !std::isfinite(json.toDouble()))
            {
                return false;
            }
            result.value = json.toDouble();
            return true;
        case ArgumentKind::Object:
            if (!json.isObject())
            {
                return false;
            }
            result.value = json.toObject().toVariantMap();
            return true;
        case ArgumentKind::Value:
            if (json.isNull() || json.isUndefined())
            {
                return false;
            }
            result.value = json.toVariant();
            result.pass_variant_object = true;
            return true;
        }
        return false;
    }

    struct InvocationResult
    {
        bool invoked = false;
        bool void_result = false;
        QVariant result;
    };

    InvocationResult invoke(QObject* object, const QMetaMethod& method, std::vector<PreparedArgument>& values)
    {
        InvocationResult result;
        std::array<QGenericArgument, 10> arguments;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            const auto type = method.parameterMetaType(static_cast<int>(index));
            arguments[index] = values[index].pass_variant_object
                ? QGenericArgument(type.name(), &values[index].value)
                : QGenericArgument(type.name(), values[index].value.constData());
        }
        const auto return_type = method.returnMetaType();
        result.void_result = return_type.id() == QMetaType::Void;
        QVariant storage;
        QGenericReturnArgument returned;
        if (!result.void_result)
        {
            if (return_type.id() == QMetaType::QVariant)
            {
                returned = QGenericReturnArgument(return_type.name(), &storage);
            }
            else
            {
                storage = QVariant(return_type, nullptr);
                returned = QGenericReturnArgument(return_type.name(), storage.data());
            }
        }
        result.invoked = method.invoke(object, Qt::DirectConnection, returned, arguments[0], arguments[1],
            arguments[2], arguments[3], arguments[4], arguments[5], arguments[6], arguments[7], arguments[8],
            arguments[9]);
        result.result = std::move(storage);
        return result;
    }

    QVariant invoke_no_argument(QObject* object, const char* name, bool& invoked)
    {
        ActionSpec spec{"", "", name, {}, mirrorfly::ActionEffect::Read, false, false};
        const auto method = find_method(object, spec);
        if (!method.isValid())
        {
            invoked = false;
            return {};
        }
        std::vector<PreparedArgument> arguments;
        auto result = invoke(object, method, arguments);
        invoked = result.invoked && !result.void_result;
        return result.result;
    }

    QJsonValue json_value(const QVariant& value)
    {
        if (!value.isValid())
        {
            return {};
        }
        if (value.metaType().id() == QMetaType::QUrl)
        {
            return value.toUrl().toString();
        }
        // QML returns JavaScript objects inside QVariant, unlike C++ QVariantMap methods.
        if (value.metaType() == QMetaType::fromType<QJSValue>())
            return QJsonValue::fromVariant(value.value<QJSValue>().toVariant());
        if (value.metaType().id() == QMetaType::LongLong || value.metaType().id() == QMetaType::ULongLong)
        {
            return value.toString();
        }
        return QJsonValue::fromVariant(value);
    }

    mirrorfly::AutomationBridge* global_bridge(const char*& error)
    {
        auto& state = registry();
        QMutexLocker lock(&state.mutex);
        if (!state.bridge)
        {
            error = "unavailable";
            return nullptr;
        }
        if (QThread::currentThread() != state.thread)
        {
            error = "wrong_thread";
            return nullptr;
        }
        error = nullptr;
        return state.bridge.data();
    }
}

namespace mirrorfly
{
    AutomationBridge::AutomationBridge(QObject* parent) : QObject(parent)
    {
        epoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        auto& state = registry();
        QMutexLocker lock(&state.mutex);
        if (!state.bridge)
        {
            state.bridge = this;
            state.thread = thread();
            globally_bound_ = true;
        }
    }

    AutomationBridge::~AutomationBridge()
    {
        if (!globally_bound_)
        {
            return;
        }
        auto& state = registry();
        QMutexLocker lock(&state.mutex);
        if (state.bridge == this)
        {
            state.bridge.clear();
            state.thread = nullptr;
        }
    }

    bool AutomationBridge::connectSignals(QObject* object, bool composed_ui)
    {
        const int slot_index = metaObject()->indexOfSlot("observeChange()");
        if (slot_index < 0)
        {
            return false;
        }
        const auto slot = metaObject()->method(slot_index);
        const auto* meta = object->metaObject();
        for (int index = QObject::staticMetaObject.methodCount(); index < meta->methodCount(); ++index)
        {
            const auto signal = meta->method(index);
            if (composed_ui && signal.name() != "automationViewStateChanged" &&
                signal.name() != "automationChanged")
                continue;
            // Canvas preview pixels are not document or selection state. Rendering completion must
            // not invalidate a command prepared against an otherwise unchanged public snapshot.
            if (!composed_ui && signal.name() == "imageChanged")
                continue;
            if (signal.methodType() == QMetaMethod::Signal)
            {
                QObject::connect(object, signal, this, slot, Qt::DirectConnection);
            }
        }
        QObject::connect(object, &QObject::destroyed, this, &AutomationBridge::observeChange);
        return true;
    }

    bool AutomationBridge::registerModule(const QString& name, QObject* module)
    {
        const auto key = name.toUtf8().toStdString();
        if (QThread::currentThread() != thread() || !module || module->thread() != thread() ||
            module_names().count(key) == 0)
        {
            return false;
        }
        const auto old = modules_.find(key);
        if (old != modules_.end() && old->second)
        {
            QObject::disconnect(old->second, nullptr, this, nullptr);
        }
        modules_[key] = module;
        document_sessions_[key] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const int activated = module->metaObject()->indexOfSignal("documentActivated()");
        if (activated >= 0)
            QObject::connect(module, module->metaObject()->method(activated), this,
                metaObject()->method(metaObject()->indexOfSlot("observeDocumentActivated()")),
                Qt::DirectConnection);
        if (!connectSignals(module))
        {
            modules_.erase(key);
            return false;
        }
        observeChange();
        return true;
    }

    bool AutomationBridge::setUiRoot(QObject* root)
    {
        if (QThread::currentThread() != thread() || !root || root->thread() != thread())
        {
            return false;
        }
        if (ui_root_)
        {
            QObject::disconnect(ui_root_, nullptr, this, nullptr);
        }
        ui_root_ = root;
        if (!connectSignals(root, true))
        {
            ui_root_.clear();
            return false;
        }
        observeChange();
        return true;
    }

    void AutomationBridge::observeChange()
    {
        ++revision_;
        fingerprint_initialized_ = false;
        queueNotification();
    }

    void AutomationBridge::observeDocumentActivated()
    {
        for (const auto& entry : modules_)
            if (entry.second == sender())
                document_sessions_[entry.first] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        observeChange();
    }

    void AutomationBridge::queueNotification()
    {
        if (notification_pending_ || observers_.empty())
            return;
        notification_pending_ = true;
        QTimer::singleShot(0, this, &AutomationBridge::publishChanges);
    }

    void AutomationBridge::publishChanges()
    {
        refreshRevision();
        notification_pending_ = false;
        std::vector<OfficeChangeSubscription> subscriptions;
        for (const auto& [id, observer] : observers_)
            subscriptions.push_back(id);
        const QPointer<AutomationBridge> guard(this);
        for (const auto id : subscriptions)
        {
            const auto found = observers_.find(id);
            if (found == observers_.end())
                continue;
            const auto observer = found->second;
            try
            {
                observer(revisionToken());
            }
            catch (...)
            {
                // Observers cannot abort the document operation or another binding's notification.
            }
            if (!guard)
                return;
        }
    }

    OfficeChangeSubscription AutomationBridge::subscribe(OfficeChangeObserver observer)
    {
        if (QThread::currentThread() != thread() || !observer || observers_.size() >= 64)
            return 0;
        auto& state = registry();
        QMutexLocker lock(&state.mutex);
        if (state.next_subscription == std::numeric_limits<OfficeChangeSubscription>::max())
            return 0;
        const auto id = state.next_subscription++;
        observers_.emplace(id, std::move(observer));
        queueNotification();
        return id;
    }

    bool AutomationBridge::unsubscribe(OfficeChangeSubscription subscription)
    {
        return QThread::currentThread() == thread() && observers_.erase(subscription) != 0;
    }

    QJsonObject AutomationBridge::collectState(bool lightweight)
    {
        QJsonObject modules;
        for (const auto& name : module_names())
        {
            QJsonObject state;
            const auto found = modules_.find(name);
            QObject* object = found == modules_.end() ? nullptr : found->second.data();
            state.insert("registered", object != nullptr);
            if (object)
            {
                state.insert("documentSession", document_sessions_[name]);
                const auto* meta = object->metaObject();
                for (const auto* property_name : properties_for(name))
                {
                    if (lightweight &&
                        QStringList{"recentFiles", "content", "statistics", "viewData", "fontSummary",
                            "compatibilitySummary", "formatInfo", "selection", "hiddenSlides"}
                            .contains(property_name))
                        continue;
                    const int property_index = meta->indexOfProperty(property_name);
                    if (property_index >= 0)
                    {
                        state.insert(property_name, json_value(meta->property(property_index).read(object)));
                    }
                }
                if ((!lightweight || name == "export" || name == "images") &&
                    (name == "word" || name == "sheets" || name == "pdf" || name == "mindmap" ||
                        name == "export" || name == "images" || name == "slides"))
                {
                    bool invoked = false;
                    const auto value = invoke_no_argument(object, "snapshot", invoked);
                    if (invoked)
                    {
                        state.insert("snapshot", json_value(value));
                    }
                }
                if (!lightweight && name == "mindmap")
                {
                    bool invoked = false;
                    const auto value = invoke_no_argument(object, "outline", invoked);
                    if (invoked)
                    {
                        state.insert("outline", json_value(value));
                    }
                }
            }
            modules.insert(QString::fromStdString(name), state);
        }

        QJsonObject ui;
        ui.insert("registered", ui_root_ != nullptr);
        if (ui_root_)
        {
            bool invoked = false;
            const auto ready = invoke_no_argument(ui_root_, "automationReady", invoked);
            ui.insert("ready", invoked && json_value(ready).toBool());
            const auto state = invoke_no_argument(ui_root_, "automationState", invoked);
            if (invoked)
            {
                ui.insert("state", json_value(state));
            }
            ui.insert("stateValid", invoked && ui.value("state").isObject());
            if (!ui.value("stateValid").toBool())
                ui.insert("ready", false);
        }
        return QJsonObject{{"modules", modules}, {"ui", ui}};
    }

    void AutomationBridge::refreshRevision()
    {
        QJsonObject ui;
        if (ui_root_)
        {
            bool invoked = false;
            ui.insert("state", json_value(invoke_no_argument(ui_root_, "automationState", invoked)));
            ui.insert("ready", json_value(invoke_no_argument(ui_root_, "automationReady", invoked)));
        }
        const auto fingerprint =
            QCryptographicHash::hash(compact(ui), QCryptographicHash::Sha256).toHex().toStdString();
        if (fingerprint_initialized_ && fingerprint != state_fingerprint_)
        {
            ++revision_;
            queueNotification();
        }
        state_fingerprint_ = fingerprint;
        fingerprint_initialized_ = true;
    }

    std::string AutomationBridge::revisionToken() const
    {
        return epoch_ + ":" + std::to_string(revision_);
    }

    std::string AutomationBridge::actionCatalog()
    {
        if (QThread::currentThread() != thread())
        {
            return error_json("wrong_thread");
        }
        refreshRevision();
        QJsonObject modules;
        for (const auto& name : module_names())
        {
            QJsonArray actions;
            const auto module = modules_.find(name);
            QObject* object = module == modules_.end() ? nullptr : module->second.data();
            for (const auto& spec : action_specs())
            {
                if (name != spec.module)
                {
                    continue;
                }
                QJsonArray arguments;
                for (const auto kind : spec.arguments)
                {
                    arguments.append(kind_name(kind));
                }
                const auto method = find_method(spec.ui_action ? ui_root_.data() : object, spec);
                QJsonArray parameters;
                const auto names = method.parameterNames();
                for (qsizetype index = 0; index < arguments.size(); ++index)
                {
                    const auto parameter = index < names.size() && !names[index].isEmpty()
                        ? QString::fromUtf8(names[index])
                        : QStringLiteral("arg%1").arg(index);
                    QJsonObject description{{"name", parameter}, {"type", arguments[index]}};
                    const auto fallback = default_argument(spec, index);
                    if (!fallback.isUndefined())
                        description.insert("default", fallback);
                    parameters.append(description);
                }
                actions.append(QJsonObject{{"name", spec.action}, {"args", arguments},
                    {"parameters", parameters}, {"available", object && method.isValid()},
                    {"resultType", spec.pending ? "pending_status" : result_kind(method.returnMetaType())},
                    {"target", spec.ui_action ? "uiComposition" : "moduleSession"},
                    {"metadata", office_action_metadata(spec)}, {"mutating", spec.mutating()},
                    {"completion", spec.pending ? "pending" : "immediate"}});
            }
            modules.insert(QString::fromStdString(name), actions);
        }
        return bounded_json(
            QJsonObject{{"ok", true}, {"protocol", 1}, {"revision", QString::fromStdString(revisionToken())},
                {"maximumRequestBytes", maximum_request_bytes},
                {"maximumSnapshotBytes", maximum_response_bytes}, {"modules", modules}});
    }

    std::string AutomationBridge::aiContract()
    {
        if (QThread::currentThread() != thread())
            return error_json("wrong_thread");
        const auto catalog = QJsonDocument::fromJson(QByteArray::fromStdString(actionCatalog())).object();
        QJsonObject schemas;
        QJsonObject fields;
        for (const auto* name :
            {"app", "text", "word", "sheets", "slides", "pdf", "mindmap", "export", "images"})
        {
            const auto found = modules_.find(name);
            QObject* object = found == modules_.end() ? nullptr : found->second.data();
            if (!object)
                continue;
            QJsonArray properties;
            for (const auto* property : properties_for(name))
            {
                if (std::string_view(property) == "document")
                    continue;
                const auto index = object->metaObject()->indexOfProperty(property);
                if (index < 0)
                    continue;
                const auto meta = object->metaObject()->property(index);
                properties.append(QJsonObject{{"name", property}, {"observable", meta.hasNotifySignal()},
                    {"constant", meta.isConstant()}});
            }
            fields.insert(name, properties);
            bool invoked = false;
            const auto schema = invoke_no_argument(object, "editSchema", invoked);
            if (invoked)
                schemas.insert(name, json_value(schema));
        }
        auto contract = make_office_ai_contract(catalog, schemas);
        contract.insert("stateFields", fields);
        return bounded_json(std::move(contract));
    }

    std::string AutomationBridge::snapshot(bool lightweight)
    {
        if (QThread::currentThread() != thread())
        {
            return error_json("wrong_thread");
        }
        refreshRevision();
        auto result = collectState(lightweight);
        result.insert("ok", true);
        result.insert("protocol", 1);
        result.insert("revision", QString::fromStdString(revisionToken()));
        result.insert("truncated", false);
        return bounded_json(std::move(result));
    }

    std::string AutomationBridge::execute(const std::string& json)
    {
        if (QThread::currentThread() != thread())
        {
            return error_json("wrong_thread");
        }
        refreshRevision();
        if (json.size() > static_cast<std::size_t>(maximum_request_bytes))
        {
            return error_json("request_too_large", revisionToken());
        }
        QJsonParseError parse_error;
        const auto parsed = QJsonDocument::fromJson(
            QByteArray(json.data(), static_cast<qsizetype>(json.size())), &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !parsed.isObject() ||
            !valid_json_complexity(parsed.object()))
        {
            return error_json("invalid_json", revisionToken());
        }
        const auto request = parsed.object();
        static const std::set<QString> allowed_keys = {
            "version", "module", "action", "args", "expectedRevision"};
        for (auto field = request.begin(); field != request.end(); ++field)
        {
            if (allowed_keys.count(field.key()) == 0)
            {
                return error_json("invalid_schema", revisionToken());
            }
        }
        if (request.contains("version") && !request.value("version").isDouble())
        {
            return error_json("invalid_schema", revisionToken());
        }
        if (request.contains("version") && request.value("version").toDouble() != 1)
        {
            return error_json("unsupported_version", revisionToken());
        }
        const int required_fields =
            static_cast<int>(allowed_keys.size()) - (request.contains("version") ? 0 : 1);
        if (request.size() != required_fields || !request.value("module").isString() ||
            !request.value("action").isString() || !request.value("args").isArray() ||
            !request.value("expectedRevision").isString())
        {
            return error_json("invalid_schema", revisionToken());
        }
        const QString module_name = request.value("module").toString();
        const QString action_name = request.value("action").toString();
        const auto* spec = mirrorfly::office_action_spec(module_name, action_name);
        if (!spec)
        {
            return error_json("unknown_action", revisionToken());
        }
        const auto module = modules_.find(module_name.toUtf8().toStdString());
        QObject* object = module == modules_.end() ? nullptr : module->second.data();
        if (!object)
        {
            return error_json("module_unavailable", revisionToken());
        }
        if (request.value("expectedRevision").toString().toStdString() != revisionToken())
        {
            return error_json("stale_revision", revisionToken());
        }
        const bool resolution = action_name == "selectSaveFile" || action_name == "cancelSaveDialog" ||
            action_name == "resolveUnsaved" ||
            ((module_name == "export" || module_name == "images") && action_name == "cancel");
        if (spec->mutating() && module_name != "app" && !object->property("active").toBool())
            return error_json("inactive_module", revisionToken());
        if (spec->mutating() && !resolution)
        {
            if (!ui_root_)
            {
                return error_json("ui_unavailable", revisionToken());
            }
            bool invoked = false;
            const auto ready = invoke_no_argument(ui_root_, "automationReady", invoked);
            if (!invoked || !json_value(ready).toBool())
            {
                return error_json("not_ready", revisionToken());
            }
            const auto location = json_value(invoke_no_argument(ui_root_, "automationState", invoked));
            if (!invoked || !location.isObject())
                return error_json("ui_state_unavailable", revisionToken());
        }
        QObject* invocation_target = spec->ui_action ? ui_root_.data() : object;
        if (!invocation_target)
            return error_json("ui_unavailable", revisionToken());
        const auto method = find_method(invocation_target, *spec);
        if (!method.isValid())
        {
            return error_json("method_unavailable", revisionToken());
        }
        auto json_arguments = request.value("args").toArray();
        while (json_arguments.size() < static_cast<qsizetype>(spec->arguments.size()))
        {
            const auto fallback = default_argument(*spec, json_arguments.size());
            if (fallback.isUndefined())
                break;
            json_arguments.append(fallback);
        }
        if (json_arguments.size() != static_cast<qsizetype>(spec->arguments.size()))
        {
            return error_json("invalid_arguments", revisionToken());
        }
        std::vector<PreparedArgument> arguments(spec->arguments.size());
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            if (!prepare_argument(json_arguments.at(static_cast<qsizetype>(index)), spec->arguments[index],
                    arguments[index]))
            {
                return error_json("invalid_arguments", revisionToken());
            }
        }
        if (module_name == "app" && action_name == "new")
        {
            const QStringList kinds{"writer", "markdown", "word", "sheets", "slides", "pdf", "mindmap"};
            if (arguments.empty() || !kinds.contains(arguments[0].value.toString()))
                return error_json("invalid_arguments", revisionToken());
        }
        if (action_name == "resolveUnsaved" &&
            !QStringList{"save", "discard", "cancel"}.contains(arguments[0].value.toString()))
            return error_json("invalid_arguments", revisionToken());
        const auto invoked = invoke(invocation_target, method, arguments);
        if (!invoked.invoked)
        {
            return error_json("invoke_failed", revisionToken());
        }
        const bool rejected_boolean = !invoked.void_result &&
            invoked.result.metaType().id() == QMetaType::Bool && !invoked.result.toBool();
        const bool rejected = rejected_boolean ||
            (module_name == "word" && action_name == "insertParagraph" && invoked.result.toInt() < 0);
        if (ui_root_ &&
            ui_root_->metaObject()->indexOfMethod(
                "automationDidExecute(QVariant,QVariant,QVariant,QVariant)") >= 0 &&
            spec->mutating() && !rejected)
        {
            QVariant ignored;
            QMetaObject::invokeMethod(ui_root_, "automationDidExecute", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, ignored), Q_ARG(QVariant, module_name), Q_ARG(QVariant, action_name),
                Q_ARG(QVariant, request.value("args").toArray().toVariantList()),
                Q_ARG(QVariant, invoked.result));
        }
        refreshRevision();
        if (rejected)
        {
            auto result = QJsonObject{{"ok", false}, {"protocol", 1}, {"error", "action_rejected"},
                {"result", false}, {"revision", QString::fromStdString(revisionToken())}};
            return bounded_json(std::move(result));
        }
        QJsonValue value;
        if (spec->pending)
        {
            QJsonObject pending{{"status", "pending"}};
            if (!invoked.void_result)
            {
                pending.insert("accepted", json_value(invoked.result));
            }
            value = pending;
        }
        else if (invoked.void_result)
        {
            value = QJsonObject{{"status", "accepted"}};
        }
        else
        {
            value = json_value(invoked.result);
        }
        return bounded_json(QJsonObject{{"ok", true}, {"protocol", 1}, {"error", QJsonValue()},
            {"result", value}, {"revision", QString::fromStdString(revisionToken())}});
    }

    std::string office_action_catalog()
    {
        const char* error = nullptr;
        auto* bridge = global_bridge(error);
        return bridge ? bridge->actionCatalog() : error_json(error);
    }

    std::string office_snapshot()
    {
        const char* error = nullptr;
        auto* bridge = global_bridge(error);
        return bridge ? bridge->snapshot() : error_json(error);
    }

    std::string office_runtime_snapshot()
    {
        const char* error = nullptr;
        auto* bridge = global_bridge(error);
        return bridge ? bridge->snapshot(true) : error_json(error);
    }

    std::string office_execute(const std::string& json)
    {
        const char* error = nullptr;
        auto* bridge = global_bridge(error);
        return bridge ? bridge->execute(json) : error_json(error);
    }

    std::string office_ai_contract()
    {
        const char* error = nullptr;
        auto* bridge = global_bridge(error);
        return bridge ? bridge->aiContract() : error_json(error);
    }

    OfficeChangeSubscription office_subscribe_changes(OfficeChangeObserver observer)
    {
        const char* error = nullptr;
        auto* bridge = global_bridge(error);
        return bridge ? bridge->subscribe(std::move(observer)) : 0;
    }

    bool office_unsubscribe_changes(OfficeChangeSubscription subscription)
    {
        const char* error = nullptr;
        auto* bridge = global_bridge(error);
        return bridge && bridge->unsubscribe(subscription);
    }
}
