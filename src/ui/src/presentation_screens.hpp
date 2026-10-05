#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace mirrorfly
{
    class PresentationScreens : public QObject
    {
        Q_OBJECT
        QML_NAMED_ELEMENT(PresentationScreens)
        Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

    public:
        explicit PresentationScreens(QObject* parent = nullptr);

        QVariantList screens() const;

    signals:
        void screensChanged();
    };
}
