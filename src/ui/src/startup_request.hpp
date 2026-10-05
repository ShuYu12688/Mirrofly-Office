#pragma once

#include <QStringList>
#include <QUrl>

namespace mirrorfly
{
    struct StartupRequest
    {
        QUrl file;
        QString error;
    };

    StartupRequest startup_request(const QStringList& arguments);
}
