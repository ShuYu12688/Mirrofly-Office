#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <mirrorfly/office_ai.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace mirrorfly
{
    class AutomationBridge final : public QObject
    {
        Q_OBJECT

    public:
        explicit AutomationBridge(QObject* parent = nullptr);
        ~AutomationBridge() override;

        bool registerModule(const QString& name, QObject* module);
        bool setUiRoot(QObject* root);

        std::string actionCatalog();
        std::string aiContract();
        std::string snapshot(bool lightweight = false);
        std::string execute(const std::string& json);
        OfficeChangeSubscription subscribe(OfficeChangeObserver observer);
        bool unsubscribe(OfficeChangeSubscription subscription);

    private slots:
        void observeChange();
        void observeDocumentActivated();

    private:
        bool connectSignals(QObject* object, bool composed_ui = false);
        QJsonObject collectState(bool lightweight);
        void refreshRevision();
        void queueNotification();
        void publishChanges();
        std::string revisionToken() const;

        std::unordered_map<std::string, QPointer<QObject>> modules_;
        std::unordered_map<std::string, QString> document_sessions_;
        QPointer<QObject> ui_root_;
        std::string state_fingerprint_;
        std::string epoch_;
        std::uint64_t revision_ = 1;
        bool fingerprint_initialized_ = false;
        bool globally_bound_ = false;
        bool notification_pending_ = false;
        std::unordered_map<OfficeChangeSubscription, OfficeChangeObserver> observers_;
    };
}
