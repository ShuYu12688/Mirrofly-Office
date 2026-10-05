#pragma once

#include <QSizeF>
#include <QVariantMap>
#include <mirrorfly/pdf_storage.hpp>

namespace mirrorfly
{
    PdfBytesResult make_pdf_stamp(
        const QString& action, const QSizeF& size, const QVariantMap& options, int page_rotation = 0);
}
