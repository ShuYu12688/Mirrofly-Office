#include "office_ai_agent.hpp"
#include "office_ai_settings.hpp"

#include <QStringList>

namespace mirrorfly
{
    bool OfficeAiAgent::configure(
        const QString& base_url, const QString& model, const QString& key, const QString& effort)
    {
        if (busy_)
            return false;
        if (key.trimmed().isEmpty() && settings_ != nullptr && !settings_->configuration().key.isEmpty())
        {
            const QUrl previous(settings_->configuration().address);
            const QUrl next(base_url.trimmed());
            if (previous.scheme() != next.scheme() || previous.host() != next.host() ||
                previous.port(previous.scheme() == "https" ? 443 : 80) !=
                    next.port(next.scheme() == "https" ? 443 : 80))
            {
                status_ = QStringLiteral("更换服务时，请输入对应的访问密钥。");
                emit stateChanged();
                return false;
            }
        }
        const QString effective_key =
            key.trimmed().isEmpty() && settings_ != nullptr ? settings_->configuration().key : key.trimmed();
        if (!OfficeAiProvider::validConfiguration(base_url, model, effective_key, effort))
        {
            status_ = QStringLiteral("请检查接口地址、模型名称和密钥；本地服务可使用 HTTP。");
            emit stateChanged();
            return false;
        }
        if (settings_ != nullptr &&
            !settings_->save({base_url.trimmed(), model.trimmed(), effective_key, effort}))
        {
            status_ = QStringLiteral("模型配置无法保存，原配置已保留。");
            emit stateChanged();
            return false;
        }
        if (!provider_.configure(base_url, model, effective_key, effort))
            return false;
        activity_ = "ready";
        status_ =
            settings_ != nullptr ? QStringLiteral("模型配置已保存。") : QStringLiteral("模型配置已载入。");
        emit stateChanged();
        return true;
    }

    QString OfficeAiAgent::modelAddress() const
    {
        return provider_.address();
    }

    QString OfficeAiAgent::modelName() const
    {
        return provider_.model();
    }

    QString OfficeAiAgent::thinkingEffort() const
    {
        return provider_.effort();
    }

    bool OfficeAiAgent::setThinkingEffort(const QString& effort)
    {
        if (busy_ || !configured() || !QStringList{"none", "low", "high", "max"}.contains(effort))
            return false;
        if (settings_ != nullptr)
        {
            auto configuration = settings_->configuration();
            configuration.effort = effort;
            if (!settings_->save(configuration))
            {
                status_ = QStringLiteral("思考强度无法保存，请稍后重试。");
                emit stateChanged();
                return false;
            }
        }
        const bool changed = provider_.setEffort(effort);
        emit stateChanged();
        return changed;
    }

}
