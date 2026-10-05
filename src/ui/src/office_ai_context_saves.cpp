#include "office_ai_context.hpp"

#include <QJsonDocument>

namespace mirrorfly
{
    void OfficeAiContext::recordMilestone(const QString& key, const QJsonObject& value)
    {
        milestone_order_.removeAll(key);
        milestone_order_.append(key);
        milestones_.insert(key, value);
        while (QJsonDocument(milestones_).toJson(QJsonDocument::Compact).size() > 4096 &&
            milestone_order_.size() > 1)
            milestones_.remove(milestone_order_.takeFirst());
    }

    QJsonArray OfficeAiContext::savedFiles() const
    {
        QJsonArray files;
        for (const auto& key : milestone_order_)
        {
            if (!key.startsWith("file:"))
                continue;
            const auto receipt = milestones_.value(key).toObject();
            if (receipt.value("saved").toBool() && !receipt.value("path").toString().isEmpty() &&
                !receipt.value("module").toString().isEmpty())
                files.append(receipt);
        }
        return files;
    }

    QJsonObject OfficeAiContext::currentMilestones(const QJsonObject& workspace) const
    {
        auto milestones = milestones_;
        const auto documents = workspace.value("documents").toObject();
        for (auto it = milestones.begin(); it != milestones.end(); ++it)
        {
            if (!it.key().startsWith("file:"))
                continue;
            auto receipt = it.value().toObject();
            const auto document = documents.value(receipt.value("module").toString()).toObject();
            if (document.value("active").toBool() && document.value("modified").toBool() &&
                it.key() == "file:" + document.value("documentSession").toString())
            {
                // A creation/save receipt proves only the earlier disk version, not later edits.
                receipt.insert("saved", false);
                receipt.insert("previouslySaved", true);
                receipt.insert("saveRequired", true);
                receipt.insert("nextSave",
                    QJsonObject{{"tool", "office_save"}, {"input", QJsonObject{{"current", true}}}});
                it.value() = receipt;
            }
        }
        return milestones;
    }
}
