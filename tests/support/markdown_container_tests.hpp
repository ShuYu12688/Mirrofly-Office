#pragma once
#include <QVariantMap>
class QQuickTextDocument;
namespace mirrorfly
{
    class EditorTools;
}
bool test_markdown_containers(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme);
