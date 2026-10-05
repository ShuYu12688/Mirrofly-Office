#include "presentation_screens.hpp"

#include <QGuiApplication>
#include <QScreen>

namespace mirrorfly
{
    PresentationScreens::PresentationScreens(QObject* parent) : QObject(parent)
    {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, &PresentationScreens::screensChanged);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PresentationScreens::screensChanged);
    }

    QVariantList PresentationScreens::screens() const
    {
        QVariantList result;
        for (auto* screen : QGuiApplication::screens())
            result.append(QVariant::fromValue(screen));
        return result;
    }
}
