#pragma once
#include <QVariantMap>
class QQuickTextDocument;
namespace mirrorfly
{
    class EditorTools;
}
bool test_markdown_import_boundaries(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme);
