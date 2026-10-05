#pragma once
#include <QVariantMap>
class QQuickTextDocument;
namespace mirrorfly
{
    class EditorTools;
}
bool test_markdown_ordered_lists(
    mirrorfly::EditorTools& tools, QQuickTextDocument* wrapper, const QVariantMap& theme);
