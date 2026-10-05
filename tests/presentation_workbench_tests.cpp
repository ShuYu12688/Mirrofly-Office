#include "presentation_scene.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSignalSpy>

#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>

#include <d3d11.h>
#include <wrl/client.h>

void qml_register_types_Mirrorfly_Native();

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    }

    void polish_tree(QQuickItem* item)
    {
        item->ensurePolished();
        const auto children = item->childItems();
        for (auto* child : children)
        {
            polish_tree(child);
        }
    }

    QQuickItem* find_item(QQuickItem* item, const QString& name)
    {
        if (item->objectName() == name)
            return item;
        for (auto* child : item->childItems())
            if (auto* found = find_item(child, name))
                return found;
        return nullptr;
    }

    bool test_page(QQmlEngine& engine, const QVariantMap& theme, const QVariant& document, int width,
        int height, bool editable)
    {
        const auto source = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QQmlComponent component(&engine, QUrl::fromLocalFile(source.filePath("ui/PresentationPage.qml")));
        if (!component.isReady())
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        auto static_theme = theme;
        static_theme.insert("motionEnabled", false);
        const QVariantMap properties{{"theme", static_theme}, {"document", document}, {"width", width},
            {"height", height}, {"editable", editable}, {"slideCount", 1}, {"slideWidth", 960},
            {"slideHeight", 540}, {"documentName", QStringLiteral("Test.pptx")}};
        std::unique_ptr<QObject> object(component.createWithInitialProperties(properties));
        auto* page = qobject_cast<QQuickItem*>(object.get());
        if (!check(page != nullptr, "presentation page constructs without an application window"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        for (int pass = 0; pass < 4; ++pass)
        {
            QCoreApplication::processEvents();
            polish_tree(page);
        }
        auto* canvas = page->findChild<QQuickItem*>(QStringLiteral("presentationCanvasViewport"));
        auto* tools = page->findChild<QQuickItem*>(QStringLiteral("presentationToolsPanel"));
        auto* thumbnails = page->findChild<QQuickItem*>(QStringLiteral("presentationThumbnails"));
        auto* image_export = find_item(page, QStringLiteral("presentationExportImagesTopAction"));
        bool passed = check(canvas && tools && thumbnails && image_export && image_export->isEnabled(),
            "workbench exposes its layout and image export for editable or read-only slides");
        if (!passed)
        {
            return false;
        }
        const bool usable_canvas = std::isfinite(canvas->width()) && std::isfinite(canvas->height()) &&
            canvas->width() >= width - 230 && canvas->height() >= 250;
        if (!usable_canvas)
        {
            std::cerr << "Page " << width << 'x' << height << " editable=" << editable
                      << " canvas=" << canvas->width() << 'x' << canvas->height() << '\n';
        }
        passed = check(usable_canvas, "minimum window leaves usable slide canvas space") && passed;
        if (editable)
        {
            passed = check(tools->width() >= width - 60 &&
                             tools->mapToItem(page, QPointF{}).y() < canvas->mapToItem(page, QPointF{}).y(),
                         "tools sit above the full-width canvas instead of beside it") &&
                passed;
            passed = check(tools->property("activeGroup").toString().isEmpty() &&
                             !tools->property("parametersVisible").toBool(),
                         "initial tools show categories only") &&
                passed;
            tools->setProperty("activeGroup", QStringLiteral("text"));
            passed = check(!tools->property("parametersVisible").toBool(),
                         "opening a category does not expose all parameters") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("font"));
            QCoreApplication::processEvents();
            polish_tree(page);
            passed = check(tools->property("parametersVisible").toBool() && canvas->height() >= 250,
                         "a selected subtool reveals parameters while retaining canvas space") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"fontFamilySource", "master"}, {"fontSizeSource", "layout"},
                    {"textColorSource", "slide"}, {"fontFamilyLocalOverride", true},
                    {"fontSizeLocalOverride", true}, {"textColorLocalOverride", true},
                    {"actions",
                        QVariantList{QStringLiteral("formatText"), QStringLiteral("resetTextInheritance")}}});
            page->setProperty("selectedShape", 0);
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* font_source = find_item(page, QStringLiteral("presentationFontSourceLabel"));
            auto* size_source = find_item(page, QStringLiteral("presentationFontSizeSourceLabel"));
            passed = check(font_source && size_source && font_source->isVisible() &&
                             font_source->property("text").toString().contains(QStringLiteral("母版")) &&
                             size_source->property("text").toString().contains(QStringLiteral("版式")),
                         "font layer shows inherited family and size sources") &&
                passed;
            auto* reset_font = find_item(page, QStringLiteral("presentationResetFontInheritanceAction"));
            auto* reset_size = find_item(page, QStringLiteral("presentationResetFontSizeInheritanceAction"));
            passed = check(reset_font && reset_size && reset_font->isVisible() && reset_size->isVisible() &&
                             reset_font->isEnabled() && reset_size->isEnabled(),
                         "font layer exposes property-specific inheritance restoration") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("style"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* color_source = find_item(page, QStringLiteral("presentationTextColorSourceLabel"));
            passed = check(color_source && color_source->isVisible() &&
                             color_source->property("text").toString().contains(QStringLiteral("本页")),
                         "text style layer shows the selected color source") &&
                passed;
            auto* reset_color =
                find_item(page, QStringLiteral("presentationResetTextColorInheritanceAction"));
            passed = check(reset_color && reset_color->isVisible() && reset_color->isEnabled(),
                         "text style layer exposes color inheritance restoration") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"editable", true}, {"isTableCell", true},
                    {"fontFamilyLocalOverride", true}, {"fontSizeLocalOverride", true},
                    {"textColorLocalOverride", true},
                    {"actions",
                        QVariantList{QStringLiteral("formatText"), QStringLiteral("resetTextInheritance")}}});
            QCoreApplication::processEvents();
            polish_tree(page);
            passed = check(reset_color && reset_color->isVisible() &&
                             reset_color->property("text").toString().contains(QStringLiteral("单元格")),
                         "table cell text color reset is named in the existing text hierarchy") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("font"));
            QCoreApplication::processEvents();
            polish_tree(page);
            passed = check(reset_font && reset_size && reset_font->isVisible() && reset_size->isVisible() &&
                             reset_font->property("text").toString().contains(QStringLiteral("单元格")) &&
                             reset_size->property("text").toString().contains(QStringLiteral("单元格")),
                         "table cell font and size reset use the existing text hierarchy") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("style"));
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"editable", true},
                    {"actions", QVariantList{QStringLiteral("formatText")}}});
            QCoreApplication::processEvents();
            polish_tree(page);
            passed = check(reset_color && !reset_color->isVisible(),
                         "text color reset stays hidden without a direct local override") &&
                passed;
            page->setProperty("selectedShape", -1);
            tools->setProperty("activeSection", QStringLiteral("font"));
            auto* picker = tools->findChild<QObject*>(QStringLiteral("systemFontPicker"));
            const auto families =
                picker ? picker->property("families").value<QJSValue>().toVariant().toList() : QVariantList{};
            auto expected = QFontDatabase::families();
            expected.removeIf([](const QString& family)
            {
                return family.startsWith('@');
            });
            passed = check(expected.empty() || families.size() == expected.size(),
                         "font chooser enumerates installed families without a fixed shortlist") &&
                passed;
            page->setProperty("systemFontFamilies",
                QVariantList{QStringLiteral("Arial"), QStringLiteral("Microsoft YaHei")});
            QCoreApplication::processEvents();
            const auto shared_value = picker ? picker->property("families") : QVariant{};
            const auto shared_families = shared_value.metaType().id() == QMetaType::fromType<QJSValue>().id()
                ? shared_value.value<QJSValue>().toVariant().toList()
                : shared_value.toList();
            passed = check(shared_families.size() == 2,
                         "presentation font picker uses the shared frontend catalog") &&
                passed;
            page->setProperty("systemFontFamilies", QVariantList{});
            page->setProperty("guideSettings",
                QVariantMap{{"showRulers", true}, {"showGrid", true}, {"showGuides", true},
                    {"snapToGuides", true}, {"gridSpacingPt", 12.0},
                    {"verticalGuidesPt", QVariantList{100.0}}, {"horizontalGuidesPt", QVariantList{100.0}}});
            tools->setProperty("activeGroup", QStringLiteral("page"));
            tools->setProperty("activeSection", QStringLiteral("output"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* export_images = find_item(page, QStringLiteral("presentationExportImagesAction"));
            passed = check(export_images && export_images->isVisible() &&
                             tools->property("parametersVisible").toBool(),
                         "page then export reveals image output in the existing tool hierarchy") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("guides"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* guide_tools = find_item(page, QStringLiteral("presentationGuideTools"));
            auto* aids = find_item(page, QStringLiteral("presentationCanvasAids"));
            passed = check(guide_tools && guide_tools->isVisible() && aids && aids->isVisible(),
                         "page category displays session rulers, grid and guides") &&
                passed;
            page->setProperty("slideTransition",
                QVariantMap{{"type", "push"}, {"direction", "r"}, {"durationSeconds", 1.0},
                    {"advanceOnClick", false}, {"advanceAfterSeconds", 3.0}, {"editable", true}});
            tools->setProperty("activeSection", QStringLiteral("transition"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* transition_tools = find_item(page, QStringLiteral("presentationTransitionTools"));
            auto* apply_transition = find_item(page, QStringLiteral("presentationApplyTransition"));
            passed = check(transition_tools && transition_tools->isVisible() && apply_transition &&
                             apply_transition->property("enabled").toBool() &&
                             transition_tools->property("pendingType") == QStringLiteral("push"),
                         "page hierarchy exposes current slide transition controls") &&
                passed;
            auto* transform = find_item(page, QStringLiteral("presentationTransformOverlay"));
            if (transform)
            {
                QQmlExpression expression(qmlContext(transform), transform,
                    "snapTranslation({x:97,y:97,width:10,height:10,a:1,b:0,c:0,d:1})");
                const auto snapped = expression.evaluate().toMap();
                passed = check(!expression.hasError() && std::abs(snapped.value("x").toDouble() - 95) < 0.1 &&
                                 std::abs(snapped.value("y").toDouble() - 95) < 0.1,
                             "drag snapping uses the visible guide coordinates") &&
                    passed;
            }
            else
                passed = check(false, "transform overlay exists for guide snapping") && passed;
            tools->setProperty("activeGroup", QStringLiteral("text"));
            tools->setProperty("activeSection", QStringLiteral("findReplace"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* find_tools = find_item(page, QStringLiteral("presentationFindReplaceTools"));
            auto* find_query = page->findChild<QObject*>(QStringLiteral("presentationFindQuery"));
            passed = check(find_tools && find_tools->isVisible() && find_query &&
                             tools->property("parametersVisible").toBool(),
                         "text category exposes layered find and replace controls") &&
                passed;
            tools->setProperty("activeGroup", QStringLiteral("object"));
            passed = check(!tools->property("parametersVisible").toBool(),
                         "switching category closes the previous parameter panel") &&
                passed;
            tools->setProperty("activeGroup", QStringLiteral("academic"));
            auto* subtools = tools->findChild<QQuickItem*>(QStringLiteral("presentationSubtoolGroup"));
            passed =
                check(subtools && subtools->isVisible() && !tools->property("parametersVisible").toBool(),
                    "academic tools open inside a shared frame without exposing all actions") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("reportStructure"));
            for (int pass = 0; pass < 3; ++pass)
            {
                QCoreApplication::processEvents();
                polish_tree(page);
            }
            passed = check(tools->property("parametersVisible").toBool() && canvas->height() >= 250,
                         "academic actions and subtool frame retain usable canvas space") &&
                passed;
            auto* frame = tools->findChild<QQuickItem*>(QStringLiteral("presentationFocusFrame"));
            auto* parameters = tools->findChild<QQuickItem*>(QStringLiteral("presentationToolParameters"));
            passed = check(frame && parameters && frame->y() > subtools->y() &&
                             frame->height() >= parameters->height(),
                         "rainbow frame follows the active parameter layer") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("templates"));
            for (int pass = 0; pass < 4; ++pass)
            {
                QCoreApplication::processEvents();
                polish_tree(page);
            }
            auto* gallery = tools->findChild<QQuickItem*>(QStringLiteral("presentationTemplateGallery"));
            const double choices_y = frame->y();
            gallery->setProperty("selectedKey", QStringLiteral("researchStudio"));
            for (int pass = 0; pass < 4; ++pass)
            {
                QCoreApplication::processEvents();
                polish_tree(page);
            }
            if (!(frame->y() > choices_y + 60 && canvas->height() >= 220))
            {
                std::cerr << "Template layout " << width << "x" << height << " frame=" << frame->y()
                          << " previous=" << choices_y << " canvas=" << canvas->height() << '\n';
            }
            passed = check(frame->y() > choices_y + 60 && canvas->height() >= 220,
                         "choosing a template moves the whole frame down to insertion while retaining canvas "
                         "space") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("templates"));
            tools->setProperty("activeGroup", QStringLiteral("work"));
            passed = check(gallery->property("selectedKey").toString().isEmpty(),
                         "changing template category clears stale choices") &&
                passed;
            tools->setProperty("activeGroup", QString{});
        }
        passed =
            check(!page->property("modalActive").toBool(), "page starts without a blocking dialog") && passed;
        if (editable)
        {
            page->setProperty("selectedShape", 0);
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"text", QStringLiteral("Accepted text")},
                    {"x", 12.1234567}, {"y", 24.000000001}, {"width", 200}, {"height", 80}, {"fontSize", 20},
                    {"fontFamily", QStringLiteral("Arial")}, {"rotation", 0}, {"shapeCount", 2},
                    {"isImage", true}, {"imageOpacity", 0.75}, {"imageCropLeft", 0.1}});
            tools->setProperty("activeGroup", QStringLiteral("object"));
            tools->setProperty("activeSection", QStringLiteral("geometry"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* x_input = find_item(page, QStringLiteral("presentationGeometry_x"));
            auto* y_input = find_item(page, QStringLiteral("presentationGeometry_y"));
            passed = check(x_input && y_input && x_input->property("text").toString() == "12.12" &&
                             y_input->property("text").toString() == "24" &&
                             x_input->property("cursorPosition").toInt() == 0 &&
                             page->property("selection").toMap().value("x").toDouble() == 12.1234567,
                         "geometry fields suppress floating tails without rounding committed coordinates") &&
                passed;
            tools->setProperty("activeSection", QStringLiteral("image"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* image_tools = tools->findChild<QQuickItem*>(QStringLiteral("presentationImageTools"));
            passed = check(image_tools && image_tools->isVisible(),
                         "image selection exposes the isolated crop and opacity tools") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"text", QStringLiteral("Accepted text")},
                    {"x", 12}, {"y", 24}, {"width", 200}, {"height", 80}, {"fontSize", 20},
                    {"fontFamily", QStringLiteral("Arial")}, {"rotation", 0}, {"shapeCount", 2},
                    {"isImage", false}});
            QCoreApplication::processEvents();
            passed = check(tools->property("activeSection").toString().isEmpty(),
                         "leaving an image selection closes the image-only tool layer") &&
                passed;
            tools->setProperty("activeGroup", QStringLiteral("text"));
            tools->setProperty("activeSection", QStringLiteral("textBox"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* text_box_tools = tools->findChild<QQuickItem*>(QStringLiteral("presentationTextBoxTools"));
            passed = check(text_box_tools && text_box_tools->isVisible(),
                         "text selection exposes the dedicated text-box formatting layer") &&
                passed;
            tools->setProperty("activeGroup", QStringLiteral("object"));
            tools->setProperty("activeSection", QStringLiteral("gradient"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* gradient_tools = tools->findChild<QQuickItem*>(QStringLiteral("presentationGradientTools"));
            passed = check(gradient_tools && gradient_tools->isVisible(),
                         "shape selection exposes the dedicated gradient formatting layer") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"editable", true},
                    {"placeholder",
                        QVariantMap{{"type", "body"}, {"fillSource", "slide"},
                            {"inheritedFillSource", "layout"}, {"localFillOverride", true},
                            {"outlineSource", "slide"}, {"inheritedOutlineSource", "layout"},
                            {"localOutlineOverride", true}}},
                    {"actions",
                        QVariantList{QStringLiteral("formatShape"), QStringLiteral("resetPlaceholderFill"),
                            QStringLiteral("resetPlaceholderOutline")}}});
            tools->setProperty("activeSection", QStringLiteral("appearance"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* reset_fill = find_item(page, QStringLiteral("presentationResetPlaceholderFill"));
            auto* reset_outline = find_item(page, QStringLiteral("presentationResetPlaceholderOutline"));
            passed = check(reset_fill && reset_fill->isVisible() && reset_fill->isEnabled(),
                         "object appearance exposes reset for a local placeholder fill override") &&
                passed;
            passed = check(reset_outline && reset_outline->isVisible() && reset_outline->isEnabled(),
                         "object appearance exposes reset for a local placeholder outline override") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"editable", true},
                    {"placeholder",
                        QVariantMap{{"type", "body"}, {"fillSource", "layout"},
                            {"inheritedFillSource", "layout"}, {"localFillOverride", false},
                            {"outlineSource", "layout"}, {"inheritedOutlineSource", "layout"},
                            {"localOutlineOverride", false}}}});
            QCoreApplication::processEvents();
            passed = check(reset_fill && !reset_fill->isVisible(),
                         "inherited placeholder fill hides the reset action") &&
                passed;
            passed = check(reset_outline && !reset_outline->isVisible(),
                         "inherited placeholder outline hides the reset action") &&
                passed;
            page->setProperty("sectionsEditable", true);
            tools->setProperty("activeGroup", QStringLiteral("page"));
            tools->setProperty("activeSection", QStringLiteral("sections"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* section_tools = find_item(page, QStringLiteral("presentationSectionTools"));
            passed = check(section_tools && section_tools->isVisible() && section_tools->isEnabled(),
                         "page category exposes the dedicated editable section layer") &&
                passed;
            page->setProperty("slideSections",
                QVariantList{QVariantMap{{"id", "section-1"}, {"name", QStringLiteral("课程内容")},
                    {"firstSlide", 0}, {"slideCount", 1}}});
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* section_header = find_item(page, QStringLiteral("presentationSectionHeader"));
            passed = check(section_header && section_header->isVisible() &&
                             section_header->property("text").toString() == QStringLiteral("课程内容"),
                         "thumbnail shows the current section boundary") &&
                passed;
            page->setProperty("selectedShape", 0);
            page->setProperty("slideCount", 2);
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"editable", true},
                    {"clickAction", QVariantMap{{"kind", "slide"}, {"targetSlide", 1}}},
                    {"actions", QVariantList{QStringLiteral("setClickAction")}}});
            tools->setProperty("activeGroup", QStringLiteral("object"));
            tools->setProperty("activeSection", QStringLiteral("link"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* link_tools = find_item(page, QStringLiteral("presentationLinkTools"));
            auto* link_target = find_item(page, QStringLiteral("presentationLinkTargetPage"));
            passed = check(link_tools && link_tools->isVisible() && link_tools->isEnabled() && link_target &&
                             link_target->property("value").toInt() == 2,
                         "object category exposes editable slide-show links with the current target") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"groupId", "group-1"}, {"groupEditable", true},
                    {"groupUngroupable", true},
                    {"groupLayerOptions",
                        QVariantMap{
                            {"back", false}, {"backward", false}, {"forward", true}, {"front", true}}}});
            tools->setProperty("activeSection", QStringLiteral("group"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* ungroup_action = find_item(page, QStringLiteral("presentationUngroupAction"));
            auto* group_front = find_item(page, QStringLiteral("presentationGroupLayer_front"));
            passed = check(ungroup_action && ungroup_action->isVisible() && ungroup_action->isEnabled(),
                         "safe scaled groups expose the existing ungroup action") &&
                passed;
            passed = check(group_front && group_front->isVisible() && group_front->isEnabled(),
                         "group layer controls follow the same object hierarchy") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"groupId", "group-1"}, {"groupEditable", true},
                    {"groupUngroupable", false}});
            QCoreApplication::processEvents();
            passed = check(ungroup_action && !ungroup_action->isEnabled(),
                         "visually unsafe groups keep the ungroup action disabled") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"groupId", "group-1"}, {"groupEditable", true},
                    {"groupExtensible", true}, {"groupNextIndex", 2},
                    {"actions", QVariantList{QStringLiteral("addToGroup")}}});
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* append_action = find_item(page, QStringLiteral("presentationGroupAppendAction"));
            passed = check(append_action && append_action->isVisible() && append_action->isEnabled() &&
                             append_action->property("text").toString().contains(QStringLiteral("加入")),
                         "simple group exposes the adjacent add action in the group hierarchy") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"id", "source-1"}, {"index", 0}, {"editable", true},
                    {"actions", QVariantList{QStringLiteral("applyFormat")}}});
            tools->setProperty("activeSection", QStringLiteral("formatBrush"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* brush_tools = find_item(page, QStringLiteral("presentationFormatBrushTools"));
            passed = check(brush_tools && brush_tools->isVisible() && brush_tools->isEnabled(),
                         "object category exposes the format brush layer") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"editable", true},
                    {"actions", QVariantList{QStringLiteral("formatShape")}}});
            tools->setProperty("activeGroup", QStringLiteral("table"));
            tools->setProperty("activeSection", QStringLiteral("tableStructure"));
            QCoreApplication::processEvents();
            polish_tree(page);
            auto* table_row_action = find_item(page, QStringLiteral("presentationAction_insertTableRow"));
            auto* table_merge_action =
                find_item(page, QStringLiteral("presentationAction_mergeTableCell_right"));
            auto* table_merge_down =
                find_item(page, QStringLiteral("presentationAction_mergeTableCell_down"));
            auto* table_unmerge_action =
                find_item(page, QStringLiteral("presentationAction_unmergeTableCell"));
            auto* table_delete_row = find_item(page, QStringLiteral("presentationAction_deleteTableRow"));
            auto* table_delete_column =
                find_item(page, QStringLiteral("presentationAction_deleteTableColumn"));
            passed = check(table_row_action && table_merge_action && table_merge_down &&
                             table_unmerge_action && !table_row_action->isEnabled() &&
                             !table_merge_action->isEnabled() && !table_unmerge_action->isEnabled() &&
                             table_delete_row && !table_delete_row->isEnabled() && table_delete_column &&
                             !table_delete_column->isEnabled(),
                         "table structure actions stay disabled for a non-table selection") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"isTableCell", true}, {"editable", true},
                    {"actions",
                        QVariantList{QStringLiteral("insertTableRow"), QStringLiteral("deleteTableRow"),
                            QStringLiteral("deleteTableColumn")}}});
            tools->setProperty("activeSection", QStringLiteral("tableStructure"));
            QCoreApplication::processEvents();
            polish_tree(page);
            table_row_action = find_item(page, QStringLiteral("presentationAction_insertTableRow"));
            table_merge_action = find_item(page, QStringLiteral("presentationAction_mergeTableCell_right"));
            passed =
                check(table_row_action && table_merge_action && table_unmerge_action &&
                        table_row_action->isEnabled() && !table_merge_action->isEnabled() &&
                        !table_unmerge_action->isEnabled() && table_delete_row &&
                        table_delete_row->isEnabled() && table_delete_column &&
                        table_delete_column->isEnabled() && tools->property("parametersVisible").toBool(),
                    "table structure actions follow selected cell capabilities") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"isTableCell", true}, {"editable", true},
                    {"tableStructureOptions", QVariantMap{{"mergeRight", false}, {"mergeDown", true}}},
                    {"actions", QVariantList{QStringLiteral("mergeTableCell")}}});
            QCoreApplication::processEvents();
            polish_tree(page);
            passed = check(!table_merge_action->isEnabled() && table_merge_down->isEnabled(),
                         "table hierarchy enables only the safe merge direction") &&
                passed;
            page->setProperty("selection",
                QVariantMap{{"valid", true}, {"index", 0}, {"isTableCell", true}, {"editable", true},
                    {"actions",
                        QVariantList{
                            QStringLiteral("formatTableCell"), QStringLiteral("unmergeTableCell")}}});
            QCoreApplication::processEvents();
            polish_tree(page);
            passed = check(table_unmerge_action->isEnabled() && !table_row_action->isEnabled(),
                         "merged cell exposes unmerge while structural row edits remain disabled") &&
                passed;
            page->setProperty("syncing", true);
            QCoreApplication::processEvents();
            passed = check(!tools->property("actionsEnabled").toBool(),
                         "PPTX tool actions pause while the package edit is syncing") &&
                passed;
            page->setProperty("syncing", false);
            auto* text_input = page->findChild<QQuickItem*>(QStringLiteral("presentationTextInput"));
            passed =
                check(text_input != nullptr, "workbench exposes its text input for transaction checks") &&
                passed;
            if (text_input)
            {
                text_input->setProperty("text", QStringLiteral("Rejected draft"));
                const bool restored = QMetaObject::invokeMethod(page, "restoreSelection");
                passed = check(restored &&
                                 text_input->property("text").toString() == QStringLiteral("Accepted text"),
                             "rejected edit restoration returns the control to committed text") &&
                    passed;
            }
        }
        page->setProperty("fullscreen", true);
        page->setProperty("slideCount", 4);
        page->setProperty("hiddenSlides", QVariantList{false, true, false, true});
        page->setProperty("currentSlide", 0);
        passed =
            check(page->property("nextPage").toInt() == 2 && page->property("previousPage").toInt() == -1,
                "presentation navigation skips hidden slides") &&
            passed;
        page->setProperty("currentSlide", 2);
        passed =
            check(page->property("nextPage").toInt() == -1 && page->property("previousPage").toInt() == 0,
                "presentation navigation stops at the last visible slide") &&
            passed;
        page->setProperty("hiddenSlides", QVariantList{true, true, true, true});
        passed = check(!page->property("hasVisibleSlides").toBool(),
                     "all-hidden documents disable slideshow entry") &&
            passed;
        for (int pass = 0; pass < 3; ++pass)
        {
            QCoreApplication::processEvents();
            polish_tree(page);
        }
        passed = check(!tools->isVisible() && !thumbnails->isVisible() && canvas->width() >= width - 40,
                     "fullscreen gives the slide the available width") &&
            passed;
        page->setProperty("hiddenSlides", QVariantList{false, true, false, true});
        page->setProperty("currentSlide", 0);
        page->setProperty("speakerNotes", QStringLiteral("Read the chart aloud"));
        page->setProperty("presenterMode", true);
        QCoreApplication::processEvents();
        polish_tree(page);
        auto* presenter = find_item(page, QStringLiteral("presentationPresenterPanel"));
        auto* next_slide = find_item(page, QStringLiteral("presentationPresenterNextSlide"));
        passed =
            check(presenter && presenter->isVisible() && next_slide &&
                    next_slide->property("slideIndex").toInt() == 2 && canvas->width() < width - 300 &&
                    presenter->property("speakerNotes").toString() == QStringLiteral("Read the chart aloud"),
                "presenter panel shares the document, skips hidden pages and exposes notes") &&
            passed;
        return passed;
    }

    bool capture_tools(QQmlEngine& engine, const QVariantMap& theme, const QVariant& document,
        const QString& output_directory)
    {
        constexpr int width = 1260;
        constexpr int height = 776;
        const auto source = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QQmlComponent component(&engine, QUrl::fromLocalFile(source.filePath("ui/PresentationPage.qml")));
        if (!check(component.isReady(), "load presentation page for offscreen capture"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        QQuickRenderControl control;
        QQuickWindow window(&control);
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
        const auto created =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
        if (!check(SUCCEEDED(created), "create software D3D11 device for offscreen capture"))
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (!check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())),
                "allocate offscreen render target"))
        {
            return false;
        }
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (!check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, readback.GetAddressOf())),
                "allocate offscreen readback target"))
        {
            return false;
        }
        window.setGeometry(0, 0, width, height);
        window.setColor(Qt::white);
        window.setGraphicsDevice(QQuickGraphicsDevice::fromDeviceAndContext(device.Get(), context.Get()));
        if (!check(control.initialize(), "initialize D3D11 Qt Quick render control"))
        {
            return false;
        }
        window.setRenderTarget(QQuickRenderTarget::fromD3D11Texture(texture.Get(), QSize(width, height)));
        auto static_theme = theme;
        static_theme.insert("motionEnabled", false);
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"theme", static_theme},
            {"document", document}, {"width", width}, {"height", height}, {"editable", true},
            {"slideCount", 1}, {"slideWidth", 960}, {"slideHeight", 540},
            {"documentName", QStringLiteral("Test.pptx")}, {"themeEditable", true},
            {"systemFontFamilies",
                QVariantList{
                    QStringLiteral("Arial"), QStringLiteral("Georgia"), QStringLiteral("Microsoft YaHei")}},
            {"chineseFontFamilies", QVariantList{QStringLiteral("Microsoft YaHei")}},
            {"themeState",
                QVariantMap{{"available", true}, {"name", QStringLiteral("课堂主题")},
                    {"linkedSlideCount", 2},
                    {"editableColorSlots",
                        QVariantList{QStringLiteral("dk2"), QStringLiteral("lt2"), QStringLiteral("accent1"),
                            QStringLiteral("accent2"), QStringLiteral("accent3"), QStringLiteral("accent4"),
                            QStringLiteral("accent5"), QStringLiteral("accent6"), QStringLiteral("hlink")}},
                    {"editableFontSlots",
                        QVariantList{QStringLiteral("majorLatin"), QStringLiteral("minorLatin"),
                            QStringLiteral("majorEastAsian"), QStringLiteral("minorEastAsian")}},
                    {"colors",
                        QVariantMap{{"accent1", "#3269B0"}, {"accent2", "#55A5C8"}, {"accent3", "#6C8DC6"},
                            {"accent4", "#805FA8"}, {"accent5", "#36A4A0"}, {"accent6", "#D8944E"}}},
                    {"fonts", QVariantMap{{"majorLatin", QStringLiteral("Georgia")}}}}}}));
        auto* page = qobject_cast<QQuickItem*>(object.get());
        if (!check(page != nullptr, "create presentation page for offscreen capture"))
        {
            return false;
        }
        page->setParentItem(window.contentItem());
        auto* tools = page->findChild<QQuickItem*>(QStringLiteral("presentationToolsPanel"));
        if (!check(tools != nullptr, "find presentation tools for offscreen capture"))
        {
            return false;
        }
        if (!QDir().mkpath(output_directory))
        {
            return false;
        }
        const auto capture = [&](const QString& name, const QString& group, const QString& section)
        {
            tools->setProperty("activeGroup", group);
            tools->setProperty("activeSection", section);
            for (int pass = 0; pass < 4; ++pass)
            {
                QCoreApplication::processEvents();
                polish_tree(page);
                control.polishItems();
                control.beginFrame();
                control.sync();
                control.render();
                control.endFrame();
            }
            context->CopyResource(readback.Get(), texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                return false;
            }
            QImage pixels(width, height, QImage::Format_RGBA8888);
            for (int row = 0; row < height; ++row)
            {
                memcpy(pixels.scanLine(row),
                    static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch,
                    static_cast<size_t>(width) * 4);
            }
            context->Unmap(readback.Get(), 0);
            return pixels.save(QDir(output_directory).filePath(name), "PNG");
        };
        bool passed = capture(
            QStringLiteral("insert-table.png"), QStringLiteral("insert"), QStringLiteral("tableInsert"));
        passed =
            capture(QStringLiteral("theme.png"), QStringLiteral("page"), QStringLiteral("theme")) && passed;
        auto* custom_theme = page->findChild<QObject*>(QStringLiteral("presentationThemeCustomTools"));
        if (custom_theme)
        {
            custom_theme->setProperty("editorMode", QStringLiteral("color"));
            passed = capture(QStringLiteral("theme-custom-color.png"), QStringLiteral("page"),
                         QStringLiteral("theme")) &&
                passed;
            custom_theme->setProperty("editorMode", QStringLiteral("font"));
            passed = capture(QStringLiteral("theme-custom-font.png"), QStringLiteral("page"),
                         QStringLiteral("theme")) &&
                passed;
            custom_theme->setProperty("editorMode", QString());
        }
        else
        {
            passed = check(false, "find custom theme controls for offscreen capture") && passed;
        }
        page->setProperty("slideTransition",
            QVariantMap{{"type", "push"}, {"direction", "r"}, {"durationSeconds", 1.0},
                {"advanceOnClick", false}, {"advanceAfterSeconds", 3.0}, {"editable", true}});
        passed =
            capture(QStringLiteral("transition.png"), QStringLiteral("page"), QStringLiteral("transition")) &&
            passed;
        page->setProperty("guideSettings",
            QVariantMap{{"showRulers", true}, {"showGrid", true}, {"showGuides", true},
                {"gridSpacingPt", 24.0}, {"verticalGuidesPt", QVariantList{240.0, 480.0}},
                {"horizontalGuidesPt", QVariantList{180.0}}});
        passed =
            capture(QStringLiteral("canvas-aids.png"), QStringLiteral("page"), QStringLiteral("guides")) &&
            passed;
        page->setProperty("guideSettings", QVariantMap{});
        page->setProperty("sectionsEditable", true);
        auto* section_name = page->findChild<QQuickItem*>(QStringLiteral("presentationSectionName"));
        if (section_name)
            section_name->setProperty("text", QStringLiteral("课程内容"));
        passed =
            capture(QStringLiteral("sections.png"), QStringLiteral("page"), QStringLiteral("sections")) &&
            passed;
        page->setProperty("slideSections",
            QVariantList{QVariantMap{{"id", "section-1"}, {"name", QStringLiteral("课程内容")},
                {"firstSlide", 0}, {"slideCount", 1}}});
        passed = capture(QStringLiteral("sections-existing.png"), QStringLiteral("page"),
                     QStringLiteral("sections")) &&
            passed;
        page->setProperty("selectedShape", 0);
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"groupNextIndex", 1},
                {"actions", QVariantList{QStringLiteral("groupAdjacent")}}});
        passed =
            capture(QStringLiteral("group.png"), QStringLiteral("object"), QStringLiteral("group")) && passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"groupId", "group-1"},
                {"groupEditable", true}, {"groupExtensible", true}, {"groupUngroupable", true},
                {"groupNextIndex", 2}, {"actions", QVariantList{QStringLiteral("addToGroup")}}});
        passed = capture(QStringLiteral("group-add-member.png"), QStringLiteral("object"),
                     QStringLiteral("group")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"groupId", "group-1"},
                {"groupEditable", true}, {"groupUngroupable", true},
                {"groupLayerOptions",
                    QVariantMap{{"back", false}, {"backward", false}, {"forward", true}, {"front", true}}}});
        passed =
            capture(QStringLiteral("group-layer.png"), QStringLiteral("object"), QStringLiteral("group")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0},
                {"groupId", "rotated-picture-group"}, {"groupEditable", true}, {"groupUngroupable", true}});
        passed = capture(QStringLiteral("rotated-picture-ungroup.png"), QStringLiteral("object"),
                     QStringLiteral("group")) &&
            passed;
        page->setProperty("slideCount", 3);
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0},
                {"clickAction", QVariantMap{{"kind", "slide"}, {"targetSlide", 1}}},
                {"actions", QVariantList{QStringLiteral("setClickAction")}}});
        passed =
            capture(QStringLiteral("link.png"), QStringLiteral("object"), QStringLiteral("link")) && passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"id", "source-1"}, {"index", 0},
                {"actions", QVariantList{QStringLiteral("applyFormat")}}});
        passed = capture(QStringLiteral("format-brush.png"), QStringLiteral("object"),
                     QStringLiteral("formatBrush")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isImage", false},
                {"placeholder",
                    QVariantMap{{"type", "body"}, {"fillSource", "slide"}, {"inheritedFillSource", "layout"},
                        {"localFillOverride", true}, {"outlineSource", "slide"},
                        {"inheritedOutlineSource", "layout"}, {"localOutlineOverride", true}}},
                {"actions",
                    QVariantList{QStringLiteral("formatShape"), QStringLiteral("resetPlaceholderFill"),
                        QStringLiteral("resetPlaceholderOutline")}}});
        passed = capture(QStringLiteral("placeholder-style.png"), QStringLiteral("object"),
                     QStringLiteral("appearance")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"fontFamily", "Arial"},
                {"fontSize", 34}, {"fontFamilySource", "slide"}, {"fontSizeSource", "slide"},
                {"textColorSource", "slide"}, {"fontFamilyLocalOverride", true},
                {"fontSizeLocalOverride", true}, {"textColorLocalOverride", true},
                {"actions",
                    QVariantList{QStringLiteral("formatText"), QStringLiteral("resetTextInheritance")}}});
        passed =
            capture(QStringLiteral("font-inheritance.png"), QStringLiteral("text"), QStringLiteral("font")) &&
            passed;
        passed = capture(QStringLiteral("text-color-inheritance.png"), QStringLiteral("text"),
                     QStringLiteral("style")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isTableCell", true},
                {"fontFamily", "Arial"}, {"fontSize", 24}, {"fontFamilySource", "tableCell"},
                {"fontSizeSource", "tableCell"}, {"textColorSource", "tableCell"},
                {"fontFamilyLocalOverride", true}, {"fontSizeLocalOverride", true},
                {"textColorLocalOverride", true},
                {"actions",
                    QVariantList{QStringLiteral("formatText"), QStringLiteral("resetTextInheritance")}}});
        passed = capture(QStringLiteral("table-text-inheritance.png"), QStringLiteral("text"),
                     QStringLiteral("font")) &&
            passed;
        passed = capture(QStringLiteral("table-text-color-inheritance.png"), QStringLiteral("text"),
                     QStringLiteral("style")) &&
            passed;
        passed = capture(QStringLiteral("find-replace.png"), QStringLiteral("text"),
                     QStringLiteral("findReplace")) &&
            passed;
        passed =
            capture(QStringLiteral("image-export.png"), QStringLiteral("page"), QStringLiteral("output")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isTableCell", true},
                {"tableStructureOptions", QVariantMap{{"mergeRight", true}, {"mergeDown", true}}},
                {"actions",
                    QVariantList{QStringLiteral("formatTableCell"), QStringLiteral("insertTableRow"),
                        QStringLiteral("insertTableColumn"), QStringLiteral("deleteTableRow"),
                        QStringLiteral("deleteTableColumn"), QStringLiteral("mergeTableCell"),
                        QStringLiteral("formatTableBorder"), QStringLiteral("unmergeTableCell")}}});
        passed = capture(QStringLiteral("table-structure.png"), QStringLiteral("table"),
                     QStringLiteral("tableStructure")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isTableCell", true},
                {"tableStructureOptions", QVariantMap{{"mergeRight", false}, {"mergeDown", true}}},
                {"actions",
                    QVariantList{QStringLiteral("insertTableRow"), QStringLiteral("mergeTableCell"),
                        QStringLiteral("unmergeTableCell")}}});
        passed = capture(QStringLiteral("table-merged-safe.png"), QStringLiteral("table"),
                     QStringLiteral("tableStructure")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isTableCell", true},
                {"tableStyleAvailable", true},
                {"tableStyleOptions",
                    QVariantMap{{"firstRow", true}, {"lastRow", false}, {"firstColumn", false},
                        {"lastColumn", false}, {"bandRows", true}, {"bandColumns", false}}},
                {"actions", QVariantList{QStringLiteral("formatTableStyle")}}});
        passed = capture(QStringLiteral("table-style.png"), QStringLiteral("table"),
                     QStringLiteral("tableStyle")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isTableCell", true},
                {"actions",
                    QVariantList{QStringLiteral("formatTableCell"), QStringLiteral("unmergeTableCell")}}});
        passed = capture(QStringLiteral("table-unmerge.png"), QStringLiteral("table"),
                     QStringLiteral("tableStructure")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isTableCell", true},
                {"tableLocalFillOverride", true},
                {"actions",
                    QVariantList{QStringLiteral("formatTableCell"), QStringLiteral("resetTableCellFill")}}});
        passed = capture(QStringLiteral("table-fill-inheritance.png"), QStringLiteral("table"),
                     QStringLiteral("cellFill")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isTableCell", true},
                {"tableLocalBorderOverride", true}, {"tableBorderColor", "#335577"}, {"tableBorderWidth", 2},
                {"actions",
                    QVariantList{QStringLiteral("formatTableBorder"), QStringLiteral("resetTableBorder")}}});
        passed = capture(QStringLiteral("table-border-inheritance.png"), QStringLiteral("table"),
                     QStringLiteral("cellBorder")) &&
            passed;
        page->setProperty("selection",
            QVariantMap{{"valid", true}, {"editable", true}, {"index", 0}, {"isTableCell", true},
                {"tableLocalBorderOverride", true},
                {"tableBorderEdges",
                    QVariantMap{
                        {"left", QVariantMap{{"color", "#AA5500"}, {"width", 3}, {"localOverride", true}}},
                        {"top", QVariantMap{{"color", "#335577"}, {"width", 2}, {"localOverride", true}}},
                        {"right", QVariantMap{{"color", "#335577"}, {"width", 2}, {"localOverride", true}}},
                        {"bottom",
                            QVariantMap{{"color", "#335577"}, {"width", 2}, {"localOverride", true}}}}},
                {"actions",
                    QVariantList{QStringLiteral("formatTableBorder"), QStringLiteral("resetTableBorder")}}});
        auto* table_tools = find_item(page, QStringLiteral("presentationTableTools"));
        if (table_tools)
            table_tools->setProperty("borderEdge", QStringLiteral("left"));
        QCoreApplication::processEvents();
        polish_tree(page);
        auto* border_width = find_item(page, QStringLiteral("presentationTableBorderWidth"));
        passed =
            check(border_width && border_width->isEnabled() && border_width->property("value").toInt() == 3,
                "single table edge exposes its current editable width") &&
            passed;
        passed = capture(QStringLiteral("table-single-edge.png"), QStringLiteral("table"),
                     QStringLiteral("cellBorder")) &&
            passed;
        page->setProperty("syncing", true);
        page->setProperty("pendingEdits", 1);
        passed = capture(QStringLiteral("syncing.png"), QStringLiteral("table"),
                     QStringLiteral("tableStructure")) &&
            passed;
        page->setProperty("syncing", false);
        page->setProperty("pendingEdits", 0);
        page->setProperty("speakerNotes", QStringLiteral("先介绍研究问题，再解释图表中的关键变化。"));
        page->setProperty("slideCount", 2);
        page->setProperty("fullscreen", true);
        page->setProperty("presenterMode", true);
        passed = capture(QStringLiteral("presenter.png"), {}, {}) && passed;
        control.invalidate();
        page->setParentItem(nullptr);
        return passed;
    }

    bool test_list_level_tools(QQmlEngine& engine, const QVariantMap& theme)
    {
        const auto source = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/components/PresentationTextTools.qml")));
        if (!check(component.isReady(), "paragraph controls QML compiles"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        const auto paragraph_info =
            engine.evaluate(QStringLiteral("(function(index) { return {valid:true,listLevel:index}; })"));
        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({{"theme", theme}, {"section", QStringLiteral("paragraph")},
                {"selection", QVariantMap{{"id", QStringLiteral("shape:1")}, {"paragraphCount", 3}}},
                {"paragraphInfoFunction", QVariant::fromValue(paragraph_info)}}));
        auto* selector =
            object ? object->findChild<QObject*>(QStringLiteral("presentationParagraphSelector")) : nullptr;
        auto* level =
            object ? object->findChild<QObject*>(QStringLiteral("presentationListLevelControl")) : nullptr;
        if (!check(selector && level && selector->property("to").toInt() == 3,
                "paragraph controls expose the selected object's paragraph count"))
            return false;
        object->setProperty("selectedParagraph", 2);
        QCoreApplication::processEvents();
        return check(selector->property("value").toInt() == 3 && level->property("value").toInt() == 3,
            "the expanded paragraph controls show the chosen paragraph's actual list level");
    }

    bool test_effect_source_tools(QQmlEngine& engine, const QVariantMap& theme)
    {
        const auto source = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/components/PresentationObjectTools.qml")));
        if (!check(component.isReady(), "object appearance controls QML compiles"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{"theme", theme}, {"section", QStringLiteral("appearance")},
                {"selection",
                    QVariantMap{{"effectsSource", QStringLiteral("theme")}, {"themeEffectStyleIndex", 2}}}}));
        auto* source_label =
            object ? object->findChild<QObject*>(QStringLiteral("presentationEffectSource")) : nullptr;
        if (!check(source_label &&
                    source_label->property("text").toString() == QStringLiteral("效果来源：主题样式 2"),
                "appearance controls identify inherited theme effects"))
            return false;
        object->setProperty("selection", QVariantMap{{"effectsSource", QStringLiteral("direct")}});
        QCoreApplication::processEvents();
        return check(source_label->property("text").toString() == QStringLiteral("效果来源：本页覆盖"),
            "appearance controls identify direct effect overrides");
    }

    bool test_theme_preview_tools(QQmlEngine& engine, const QVariantMap& theme)
    {
        const auto source = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/components/PresentationThemeTools.qml")));
        if (!check(component.isReady(), "theme preview QML compiles"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"theme", theme},
            {"available", true},
            {"systemFontFamilies", QVariantList{QStringLiteral("Arial"), QStringLiteral("Cambria")}},
            {"chineseFontFamilies", QVariantList{QStringLiteral("Microsoft YaHei")}},
            {"themeState",
                QVariantMap{{"available", true}, {"name", QStringLiteral("课堂主题")},
                    {"linkedSlideCount", 2}, {"editableColorSlots", QVariantList{QStringLiteral("accent1")}},
                    {"editableFontSlots",
                        QVariantList{QStringLiteral("majorLatin"), QStringLiteral("majorEastAsian")}},
                    {"colors", QVariantMap{{"accent1", QStringLiteral("#3269B0")}}},
                    {"fonts", QVariantMap{{"majorLatin", QStringLiteral("Georgia")}}}}}}));
        QCoreApplication::processEvents();
        auto* current =
            object ? object->findChild<QObject*>(QStringLiteral("presentationCurrentTheme")) : nullptr;
        bool passed =
            check(current && current->property("text").toString().contains(QStringLiteral("课堂主题")) &&
                    current->property("text").toString().contains(QStringLiteral("关联 2 页")),
                "theme controls show the linked scope before applying a preset");
        auto* custom = object->findChild<QObject*>(QStringLiteral("presentationThemeCustomTools"));
        auto* input = object->findChild<QObject*>(QStringLiteral("presentationThemeCustomColorInput"));
        auto* apply = object->findChild<QObject*>(QStringLiteral("presentationThemeApplyCustomColor"));
        auto* font_picker = object->findChild<QObject*>(QStringLiteral("presentationThemeFontPicker"));
        auto* font_slot = object->findChild<QObject*>(QStringLiteral("presentationThemeFontSlot"));
        if (!check(custom && input && apply && font_picker && font_slot,
                "theme layer includes separate color and font controls"))
            return false;
        QSignalSpy edits(object.get(), SIGNAL(editRequested(QString, QVariant)));
        passed = check(edits.isValid(), "theme controls expose a shared edit signal") && passed;
        custom->setProperty("editorMode", QStringLiteral("color"));
        input->setProperty("text", QStringLiteral("#123"));
        QCoreApplication::processEvents();
        passed = check(!apply->property("enabled").toBool(),
                     "custom theme rejects incomplete colors before dispatch") &&
            passed;
        input->setProperty("text", QStringLiteral("#123456"));
        QCoreApplication::processEvents();
        passed = check(apply->property("enabled").toBool() && QMetaObject::invokeMethod(apply, "click") &&
                         edits.size() == 1 && edits.at(0).at(0).toString() == QStringLiteral("applyTheme") &&
                         edits.at(0).at(1).toMap().value("colors").toMap().value("accent1").toString() ==
                             QStringLiteral("#123456"),
                     "custom color uses the existing theme edit command and slot") &&
            passed;
        custom->setProperty("editorMode", QStringLiteral("font"));
        const QVariant families = font_picker->property("families");
        const auto shared_families = families.metaType().id() == QMetaType::fromType<QJSValue>().id()
            ? families.value<QJSValue>().toVariant().toList()
            : families.toList();
        passed =
            check(!font_picker->property("allowLocalEnumeration").toBool() && shared_families.size() == 2 &&
                    QMetaObject::invokeMethod(
                        custom, "applyFont", Q_ARG(QVariant, QVariant(QStringLiteral("Cambria")))) &&
                    edits.size() == 2 &&
                    edits.at(1).at(1).toMap().value("fonts").toMap().value("majorLatin").toString() ==
                        QStringLiteral("Cambria"),
                "custom font reuses the shared font list and theme edit command") &&
            passed;
        font_slot->setProperty("currentIndex", 2);
        QCoreApplication::processEvents();
        passed =
            check(font_picker->property("chineseOnly").toBool() &&
                    QMetaObject::invokeMethod(
                        custom, "applyFont", Q_ARG(QVariant, QVariant(QStringLiteral("Microsoft YaHei")))) &&
                    edits.size() == 3 &&
                    edits.at(2).at(1).toMap().value("fonts").toMap().value("majorEastAsian").toString() ==
                        QStringLiteral("Microsoft YaHei"),
                "Chinese theme font slot uses the shared Chinese family filter") &&
            passed;
        object->setProperty("themeState",
            QVariantMap{{"available", true}, {"editableColorSlots", QVariantList{QStringLiteral("accent1")}},
                {"editableFontSlots", QVariantList{QStringLiteral("majorLatin")}}});
        QCoreApplication::processEvents();
        QVariant preset_allowed;
        const QVariantMap preset_definition{
            {"colors",
                QVariantMap{{"accent1", QStringLiteral("#123456")}, {"accent6", QStringLiteral("#654321")}}},
            {"fonts", QVariantMap{{"majorLatin", QStringLiteral("Georgia")}}}};
        passed = check(!font_picker->property("enabled").toBool() &&
                         QMetaObject::invokeMethod(object.get(), "canApplyPreset",
                             Q_RETURN_ARG(QVariant, preset_allowed),
                             Q_ARG(QVariant, QVariant(preset_definition))) &&
                         !preset_allowed.toBool() &&
                         QMetaObject::invokeMethod(custom, "applyFont",
                             Q_ARG(QVariant, QVariant(QStringLiteral("Microsoft YaHei")))) &&
                         edits.size() == 3,
                     "missing theme slots lock their font and full preset actions") &&
            passed;
        input->setProperty("text", QStringLiteral("#654321"));
        object->setProperty("available", false);
        QCoreApplication::processEvents();
        passed = check(!apply->property("enabled").toBool() && !font_picker->property("enabled").toBool(),
                     "theme custom controls lock when the linked theme is unavailable") &&
            passed;
        return passed;
    }

    bool test_table_style_tools(QQmlEngine& engine, const QVariantMap& theme)
    {
        const auto source = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/components/PresentationTableTools.qml")));
        if (!check(component.isReady(), "whole-table style controls QML compiles"))
        {
            std::cerr << component.errorString().toStdString();
            return false;
        }
        const QVariantMap style{{"firstRow", true}, {"lastRow", false}, {"firstColumn", false},
            {"lastColumn", false}, {"bandRows", true}, {"bandColumns", false}};
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{"theme", theme}, {"section", QStringLiteral("tableStyle")},
                {"selection",
                    QVariantMap{{"tableStyleAvailable", true}, {"tableStyleOptions", style},
                        {"actions", QVariantList{QStringLiteral("formatTableStyle")}}}}}));
        auto* apply =
            object ? object->findChild<QObject*>(QStringLiteral("presentationApplyTableStyle")) : nullptr;
        const bool ready =
            apply && apply->property("enabled").toBool() && object->property("pendingStyle").toMap() == style;
        return check(ready, "whole-table style controls reflect selection and expose one apply action");
    }

    bool test_dual_screen_playback(QQmlEngine& engine, const QVariantMap& theme, const QVariant& document)
    {
        const auto source = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
        QQmlComponent screens_component(&engine);
        screens_component.setData(R"(import QtQuick
import Mirrorfly.Native 1.0
Window {
    visible: false
    PresentationScreens { id: displays }
    property var selectedScreen: displays.screens[0]
    Component.onCompleted: screen = selectedScreen
})",
            QUrl::fromLocalFile(source.filePath("ui/PresentationScreensProbe.qml")));
        if (!check(screens_component.isReady(), "screen inventory QML compiles"))
        {
            std::cerr << "QML status=" << screens_component.status() << ' '
                      << screens_component.errorString().toStdString();
            return false;
        }
        std::unique_ptr<QObject> screen_window(screens_component.create());
        if (!check(screen_window &&
                    qobject_cast<QQuickWindow*>(screen_window.get())->screen() ==
                        QGuiApplication::primaryScreen() &&
                    !screen_window->property("visible").toBool(),
                "QML can assign an enumerated display to an unshown presentation window"))
            return false;

        QQmlComponent audience_component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/PresentationAudienceWindow.qml")));
        if (!check(audience_component.isReady(), "audience window QML compiles"))
        {
            std::cerr << audience_component.errorString().toStdString();
            return false;
        }
        std::unique_ptr<QObject> audience(
            audience_component.createWithInitialProperties({{"theme", theme}, {"document", document},
                {"currentSlide", 0}, {"slideWidth", 960}, {"playing", false}, {"busy", false}}));
        auto* audience_slide = audience
            ? audience->findChild<QQuickItem*>(QStringLiteral("presentationAudienceSlide"))
            : nullptr;
        if (!check(audience_slide && !audience->property("visible").toBool(),
                "audience slide exists inside a hidden window"))
            return false;

        QQmlComponent page_component(
            &engine, QUrl::fromLocalFile(source.filePath("ui/PresentationPage.qml")));
        if (!check(page_component.isReady(), "presenter page QML compiles"))
        {
            std::cerr << page_component.errorString().toStdString();
            return false;
        }
        std::unique_ptr<QObject> page(page_component.createWithInitialProperties({{"theme", theme},
            {"document", document}, {"width", 1040}, {"height", 676}, {"slideCount", 2}, {"slideWidth", 960},
            {"slideHeight", 540}, {"externalPlayback", QVariant::fromValue(audience_slide)}}));
        auto* presenter = qobject_cast<QQuickItem*>(page.get());
        auto* full_slide =
            presenter ? presenter->findChild<QQuickItem*>(QStringLiteral("presentationFullSlide")) : nullptr;
        auto* current_thumbnail = presenter
            ? presenter->findChild<QQuickItem*>(QStringLiteral("presentationPresenterCurrentSlide"))
            : nullptr;
        if (!check(full_slide && current_thumbnail, "presenter page provides both preview surfaces"))
            return false;
        presenter->setProperty("dualScreenPresenter", true);
        presenter->setProperty("presenterMode", true);
        presenter->setProperty("fullscreen", true);
        QCoreApplication::processEvents();
        return check(presenter->property("playbackView").value<QObject*>() == audience_slide &&
                !full_slide->isVisible() && current_thumbnail->isVisible() &&
                !full_slide->property("mediaEnabled").toBool() &&
                !full_slide->property("animationEnabled").toBool() &&
                !full_slide->property("transitionsEnabled").toBool() &&
                !full_slide->property("deferredFrames").toBool(),
            "dual-screen mode owns active playback on the audience slide and uses a cached preview");
    }
}

int run_presentation_workbench_tests(int argc, char* argv[])
{
    const bool capture =
        argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--capture-presentation-tools");
    if (capture)
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    }
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
    QGuiApplication application(argc, argv);
    qml_register_types_Mirrorfly_Native();
    QQmlEngine engine;
    engine.addImportPath(QString::fromUtf8(MIRRORFLY_TEST_BUILD_DIRECTORY));
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& errors)
    {
        for (const auto& error : errors)
        {
            warnings.append(error.toString());
        }
    });
    const auto source = QDir(QString::fromUtf8(MIRRORFLY_TEST_SOURCE_DIRECTORY));
    QFile theme_file(source.filePath("config/theme.json"));
    if (!theme_file.open(QIODevice::ReadOnly))
    {
        return 1;
    }
    const auto theme = QJsonDocument::fromJson(theme_file.readAll()).object().toVariantMap();
    QFile main_file(source.filePath("ui/Main.qml"));
    if (!main_file.open(QIODevice::ReadOnly))
    {
        return 1;
    }
    const QByteArray main_source = main_file.readAll();
    bool passed = check(main_source.contains("property bool presentationFullscreen: false") &&
            main_source.contains("fullscreen: root.presentationFullscreen") &&
            !main_source.contains("fullscreen: root.visibility === Window.FullScreen"),
        "slideshow mode is explicit and cannot be inferred from an incidental window state");
    auto scene = std::make_shared<mirrorfly::PresentationScene>();
    scene->width = 960;
    scene->height = 540;
    scene->slides.emplace_back();
    mirrorfly::PresentationShape shape;
    shape.id = 1;
    shape.width = 400;
    shape.height = 120;
    mirrorfly::PresentationParagraph paragraph;
    mirrorfly::PresentationRun run;
    run.text = "Accepted text";
    paragraph.runs.push_back(run);
    shape.text.paragraphs.push_back(paragraph);
    scene->slides.front().shapes.push_back(shape);
    scene->slides.push_back(scene->slides.front());
    const auto document = QVariant::fromValue(mirrorfly::prepare_presentation(scene));
    if (capture)
    {
        return capture_tools(engine, theme, document, QString::fromLocal8Bit(argv[2])) ? 0 : 1;
    }
    passed = test_page(engine, theme, document, 1040, 676, true) && passed;
    passed = test_page(engine, theme, document, 1260, 776, true) && passed;
    passed = test_page(engine, theme, document, 1040, 676, false) && passed;
    passed = test_list_level_tools(engine, theme) && passed;
    passed = test_effect_source_tools(engine, theme) && passed;
    passed = test_theme_preview_tools(engine, theme) && passed;
    passed = test_table_style_tools(engine, theme) && passed;
    passed = test_dual_screen_playback(engine, theme, document) && passed;
    for (const auto& warning : warnings)
    {
        std::cerr << warning.toStdString() << '\n';
    }
    passed =
        check(warnings.empty(), "standalone workbench has no QML binding or construction warnings") && passed;
    return passed ? 0 : 1;
}

int main(int argc, char* argv[])
{
    return run_presentation_workbench_tests(argc, argv);
}
