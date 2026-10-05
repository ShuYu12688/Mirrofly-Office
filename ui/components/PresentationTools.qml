pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle
{
    id: root

    property var theme
    property var templatePreviews: ({})
    property var templateOptions: ({})
    property var chineseFontFamilies: []
    property var systemFontFamilies: []
    property var searchFunction
    property var paragraphInfoFunction
    property var guideSettings: ({})
    property var slideTransition: ({})
    property real slideWidth: 0
    property real slideHeight: 0
    property bool syncing: false
    property bool editable: false
    property bool themeEditable: false
    property var themeState: ({})
    property bool actionsEnabled: true
    property bool exportBusy: false
    property int slideCount: 0
    property int currentSlide: 0
    property var slideSections: []
    property bool sectionsEditable: false
    property bool currentSlideHidden: false
    property int selectedShape: -1
    property var selection: ({})
    property string formatBrushSourceId: ""
    signal formatBrushArmed(string sourceId)
    signal formatBrushApplyRequested()
    signal formatBrushCancelled()
    property string activeGroup: ""
    property string activeSection: ""
    readonly property bool parametersVisible: activeGroup.length > 0 && activeSection.length > 0
    readonly property bool hasSelection: selectedShape >= 0 && selection.editable !== false
    function supports(action)
    {
        return hasSelection && (!selection.actions || selection.actions.indexOf(action) >= 0);
    }
    readonly property var sections:
    {
        const groups = {
            page: [{key: "layout", label: "新建页面"}, {key: "pages", label: "页面顺序"},
                {key: "sections", label: "节"},
                {key: "background", label: "纸张颜色"}, {key: "theme", label: "主题配色"},
                {key: "transition", label: "页面切换"},
                {key: "guides", label: "标尺与辅助线"},
                {key: "output", label: "导出"}],
            insert: [{key: "textBox", label: "文字框"}, {key: "shapes", label: "基础图形"},
                {key: "image", label: "本地图片"}, {key: "tableInsert", label: "表格"}],
            text: [{key: "findReplace", label: "查找替换"},
                {key: "font", label: "字体与字号"}, {key: "style", label: "字形与颜色"},
                {key: "paragraph", label: "段落与列表"}, {key: "textBox", label: "文本框"},
                {key: "wordArt", label: "艺术字与文字效果"}],
            object: selection.isTableCell ? [] : [{key: "geometry", label: "位置、尺寸与旋转"}, {key: "align", label: "画布对齐"}]
                .concat(selection.isImage ? [{key: "image", label: "图片裁剪"}] : [])
                .concat([{key: "appearance", label: selection.isImage ? "图片轮廓与效果" : "填充与轮廓"}])
                .concat(selection.isImage ? [] : [{key: "gradient", label: "渐变填充"}])
                .concat([{key: "group", label: "组合与拆分"}])
                .concat([{key: "formatBrush", label: "格式刷"}])
                .concat([{key: "link", label: "放映跳转"}])
                .concat([{key: "layers", label: "复制与层次"}]),
            table: selection.isTableCell ? [{key: "tableStyle", label: "整表样式"},
                {key: "cellFill", label: "单元格底色"},
                {key: "tableStructure", label: "行列与合并"},
                {key: "cellBorder", label: "单元格边框"}] : [],
            academic: [{key: "templates", label: "精选模板"}, {key: "reportStructure", label: "汇报结构"}, {key: "analysis", label: "分析与总结"},
                {key: "references", label: "参考资料"}],
            work: [{key: "templates", label: "精选模板"}, {key: "project", label: "项目进展"}, {key: "meeting", label: "会议纪要"}, {key: "plan", label: "阶段计划"}]
        };
        return groups[activeGroup] || [];
    }
    readonly property var actions:
    {
        const items = {
            layout: [{label: "标题页", action: "addSlide", options: {layout: "title"}},
                {label: "标题与正文", action: "addSlide", options: {layout: "titleContent"}},
                {label: "双栏", action: "addSlide", options: {layout: "twoColumns"}},
                {label: "空白纸张", action: "addSlide", options: {layout: "blank"}}],
            pages: [{label: "复制页面", action: "duplicateSlide", options: {}, allowed: slideCount < 200},
                {label: "向前一页", action: "moveSlide", options: {offset: -1}, allowed: currentSlide > 0},
                {label: "向后一页", action: "moveSlide", options: {offset: 1}, allowed: currentSlide + 1 < slideCount},
                {label: "删除页面", action: "deleteSlide", options: {}, allowed: slideCount > 1},
                {label: currentSlideHidden ? "放映中显示" : "放映中隐藏", action: "slideHidden", options: {hidden: !currentSlideHidden}}],
            shapes: [{label: "矩形", action: "addShape", options: {geometry: "rect"}},
                {label: "圆角矩形", action: "addShape", options: {geometry: "roundRect"}},
                {label: "椭圆", action: "addShape", options: {geometry: "ellipse"}},
                {label: "直线", action: "addShape", options: {geometry: "line"}},
                {label: "右箭头", action: "addShape", options: {geometry: "rightArrow"}}],
            layers: [{label: "复制对象", action: "duplicateShape", options: {}},
                {label: "置于底层", action: "moveShape", options: {targetIndex: 0}, allowed: selection.index > 0},
                {label: "后移一层", action: "moveShape", options: {offset: -1}, allowed: selection.index > 0},
                {label: "前移一层", action: "moveShape", options: {offset: 1}, allowed: selection.index + 1 < selection.shapeCount},
                {label: "置于顶层", action: "moveShape", options: {targetIndex: selection.shapeCount - 1}, allowed: selection.index + 1 < selection.shapeCount},
                {label: "删除对象", action: "deleteShape", options: {}}],
            reportStructure: [{label: "插入汇报提纲", action: "addSlide", options: {layout: "reportOutline"}},
                {label: "插入研究思路", action: "addSlide", options: {layout: "researchPlan"}}],
            analysis: [{label: "插入对比分析", action: "addSlide", options: {layout: "comparison"}},
                {label: "插入总结与交流", action: "addSlide", options: {layout: "conclusion"}}],
            references: [{label: "插入参考资料页", action: "addSlide", options: {layout: "references"}}],
            project: [{label: "插入项目进展页", action: "addSlide", options: {layout: "projectStatus"}}],
            meeting: [{label: "插入会议纪要页", action: "addSlide", options: {layout: "meetingSummary"}}],
            plan: [{label: "插入阶段计划页", action: "addSlide", options: {layout: "milestones"}}],
            align: [{label: "靠左", action: "alignShape", options: {alignment: "left"}},
                {label: "水平居中", action: "alignShape", options: {alignment: "center"}},
                {label: "靠右", action: "alignShape", options: {alignment: "right"}},
                {label: "靠上", action: "alignShape", options: {alignment: "top"}},
                {label: "垂直居中", action: "alignShape", options: {alignment: "middle"}},
                {label: "靠下", action: "alignShape", options: {alignment: "bottom"}}],
            tableStructure: [{label: "下方加一行", action: "insertTableRow", options: {}},
                {label: "右侧加一列", action: "insertTableColumn", options: {}},
                {label: "删除当前行", action: "deleteTableRow", options: {}},
                {label: "删除当前列", action: "deleteTableColumn", options: {}},
                {label: "合并右侧空格", action: "mergeTableCell", options: {direction: "right"}},
                {label: "合并下方空格", action: "mergeTableCell", options: {direction: "down"}},
                {label: "拆分当前合并格", action: "unmergeTableCell", options: {}}]
        };
        return items[activeSection] || [];
    }

    signal editRequested(string action, var options)
    signal imageRequested()
    signal imageReplaceRequested()
    signal canvasTextRequested()
    signal searchNavigateRequested(int slideIndex, int shapeIndex)
    signal guideSettingsRequested(var patch)
    signal imageExportRequested()
    signal pdfRequested()

    function refreshSearch()
    {
        if (activeGroup === "text" && activeSection === "findReplace")
            findReplaceTools.refresh(0);
    }

    implicitHeight: stack.implicitHeight + 20
    radius: theme.radius
    color: theme.slidesToolSurface
    border.color: theme.borderColor

    function chooseGroup(group)
    {
        activeGroup = activeGroup === group ? "" : group;
        activeSection = "";
    }

    function restoreSelection()
    {
        objectTools.restoreSelection();
    }

    onActiveGroupChanged: activeSection = ""
    onSelectionChanged:
    {
        if (activeGroup !== "object" && activeGroup !== "text" && activeGroup !== "table")
        {
            return;
        }
        if (activeGroup === "object" && activeSection === "image" && selection.isImage !== true)
        {
            activeSection = "";
            return;
        }
        if (activeGroup === "object" && (activeSection === "appearance" || activeSection === "gradient")
            && selection.isImage === true)
        {
            activeSection = "";
            return;
        }
        let found = false;
        for (let index = 0; index < sections.length; ++index)
        {
            if (sections[index].key === activeSection)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            activeSection = "";
        }
    }

    SpectrumStroke
    {
        id: focusFrame
        objectName: "presentationFocusFrame"
        theme: root.theme
        x: stack.x
        width: stack.width
        y: stack.y + (root.parametersVisible ? parameters.y - 6
            + (gallery.visible ? gallery.y + gallery.focusTarget.y : 0) : subtoolGroup.y)
        height: root.parametersVisible ? (gallery.visible ? gallery.focusTarget.height : parameters.height) + 12 : subtoolGroup.height
        visible: root.activeGroup.length > 0
        radius: root.theme.radius * 0.75
        Behavior on y { enabled: root.theme.motionEnabled; NumberAnimation { duration: root.theme.disclosureDuration; easing.type: Easing.InOutCubic } }
        Behavior on height { enabled: root.theme.motionEnabled; NumberAnimation { duration: root.theme.disclosureDuration; easing.type: Easing.InOutCubic } }
    }

    ColumnLayout
    {
        id: stack

        x: 14
        y: 10
        width: parent.width - 28
        spacing: 8

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 8

            Text
            {
                text: "创作工具"
                color: root.theme.accent
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 1
                font.weight: Font.DemiBold
                Layout.rightMargin: 8
            }

            Repeater
            {
                model: [{key: "page", label: "页面", icon: "slides"}, {key: "insert", label: "插入", icon: "plus"},
                    {key: "text", label: "文字", icon: "writer"}, {key: "object", label: "对象", icon: "grid"},
                    {key: "table", label: "表格", icon: "grid"},
                    {key: "academic", label: "学业汇报", icon: "slides"},
                    {key: "work", label: "工作汇报", icon: "folder"}]
                ActionButton
                {
                    required property var modelData

                    theme: root.theme
                    text: modelData.label
                    iconName: modelData.icon
                    compact: true
                    primary: root.activeGroup === modelData.key
                    onClicked: root.chooseGroup(modelData.key)
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton
            {
                theme: root.theme
                text: "收起"
                iconName: "close"
                compact: true
                visible: root.activeGroup.length > 0
                onClicked: root.activeGroup = ""
            }
        }

        Item
        {
            id: subtoolGroup
            objectName: "presentationSubtoolGroup"
            Layout.fillWidth: true
            implicitHeight: subtools.implicitHeight + 16
            visible: root.activeGroup.length > 0

            Flow
            {
                id: subtools

                x: 8
                y: 8
                width: parent.width - 16
                spacing: 7
                Repeater
                {
                    model: root.sections
                    ActionButton
                    {
                        required property var modelData

                        theme: root.theme
                        text: modelData.label
                        iconName: ""
                        compact: true
                        primary: root.activeSection === modelData.key
                        onClicked: root.activeSection = root.activeSection === modelData.key ? "" : modelData.key
                    }
                }
            }
        }

        ColumnLayout
        {
            id: parameters
            objectName: "presentationToolParameters"
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            visible: root.parametersVisible
            enabled: root.actionsEnabled && (root.editable || root.activeSection === "output")
            spacing: 8

            PresentationTemplateGallery
            {
                id: gallery
                objectName: "presentationTemplateGallery"
                Layout.fillWidth: true
                theme: root.theme
                group: root.activeGroup
                previews: root.templatePreviews
                visible: root.activeSection === "templates"
                enabled: root.slideCount < 200
                onInsertRequested: function(key) { root.editRequested("addSlide", root.templateOptions[key]); }
            }

            Flow
            {
                objectName: "presentationSectionActions"
                Layout.fillWidth: true
                visible: root.actions.length > 0
                enabled: (root.activeGroup !== "object" && root.activeGroup !== "table")
                    || root.hasSelection
                spacing: 7
                Repeater
                {
                    model: root.actions
                    ActionButton
                    {
                        required property var modelData

                        objectName: "presentationAction_" + modelData.action
                            + (modelData.action === "mergeTableCell" ? "_" + modelData.options.direction : "")
                        theme: root.theme
                        text: modelData.label
                        iconName: ""
                        compact: true
                        enabled: modelData.allowed !== false
                            && ((root.activeGroup !== "object" && root.activeGroup !== "table")
                                || root.supports(modelData.action))
                            && (modelData.action !== "addSlide" || root.slideCount < 200)
                            && (modelData.action !== "mergeTableCell"
                                || (modelData.options.direction === "right"
                                    ? root.selection.tableStructureOptions.mergeRight
                                    : root.selection.tableStructureOptions.mergeDown))
                        onClicked: root.editRequested(modelData.action, modelData.options)
                    }
                }
            }

            PresentationPalette
            {
                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "page" && root.activeSection === "background"
                onColorSelected: function(value) { root.editRequested("background", {color: value}); }
            }

            RowLayout
            {
                visible: root.activeGroup === "insert" && root.activeSection === "textBox"
                ActionButton
                {
                    theme: root.theme
                    text: "添加文字框"
                    compact: true
                    iconName: "plus"
                    onClicked:
                    {
                        root.editRequested("addText", {});
                        root.canvasTextRequested();
                    }
                }
                Text
                {
                    text: "拖动移动 · 控制点缩放 · 双击输入文字。"
                    color: root.theme.textSecondary
                    font.pixelSize: root.theme.fontSize - 2
                }
            }

            ActionButton
            {
                visible: root.activeGroup === "insert" && root.activeSection === "image"
                theme: root.theme
                text: "从电脑选择图片"
                iconName: "folder"
                compact: true
                onClicked: root.imageRequested()
            }

            PresentationTextTools
            {
                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "text" && (root.activeSection === "font"
                    || root.activeSection === "style" || root.activeSection === "paragraph")
                section: root.activeSection
                chineseFontFamilies: root.chineseFontFamilies
                systemFontFamilies: root.systemFontFamilies
                selection: root.selection
                paragraphInfoFunction: root.paragraphInfoFunction
                enabled: root.supports("formatText")
                onFormatRequested: function(options) { root.editRequested("formatText", options); }
                onResetRequested: function(property)
                {
                    root.editRequested("resetTextInheritance", {property: property});
                }
                onParagraphRequested: function(options) { root.editRequested("formatParagraph", options); }
            }

            RowLayout
            {
                visible: root.activeGroup === "page" && root.activeSection === "output"
                ActionButton
                {
                    theme: root.theme
                    text: "导出 PDF…"
                    compact: true
                    onClicked: root.pdfRequested()
                }
                ActionButton
                {
                    objectName: "presentationExportImagesAction"
                    theme: root.theme
                    text: "逐页导出 PNG/JPG…"
                    compact: true
                    enabled: !root.exportBusy
                    onClicked: root.imageExportRequested()
                }
            }

            PresentationFindReplaceTools
            {
                id: findReplaceTools
                objectName: "presentationFindReplaceTools"
                Layout.fillWidth: true
                theme: root.theme
                searchFunction: root.searchFunction
                syncing: root.syncing
                visible: root.activeGroup === "text" && root.activeSection === "findReplace"
                onEditRequested: function(action, options) { root.editRequested(action, options); }
                onNavigateRequested: function(slideIndex, shapeIndex)
                {
                    root.searchNavigateRequested(slideIndex, shapeIndex);
                }
            }

            PresentationThemeTools
            {
                objectName: "presentationThemeTools"
                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "page" && root.activeSection === "theme"
                available: root.themeEditable
                themeState: root.themeState
                chineseFontFamilies: root.chineseFontFamilies
                systemFontFamilies: root.systemFontFamilies
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationTransitionTools
            {
                objectName: "presentationTransitionTools"
                Layout.fillWidth: true
                theme: root.theme
                transition: root.slideTransition
                visible: root.activeGroup === "page" && root.activeSection === "transition"
                enabled: root.editable && root.actionsEnabled && root.slideTransition.editable !== false
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationGuideTools
            {
                objectName: "presentationGuideTools"
                Layout.fillWidth: true
                theme: root.theme
                settings: root.guideSettings
                slideWidth: root.slideWidth
                slideHeight: root.slideHeight
                visible: root.activeGroup === "page" && root.activeSection === "guides"
                onSettingsRequested: function(patch) { root.guideSettingsRequested(patch); }
            }

            PresentationSectionTools
            {
                objectName: "presentationSectionTools"
                Layout.fillWidth: true
                theme: root.theme
                slideSections: root.slideSections
                currentSlide: root.currentSlide
                visible: root.activeGroup === "page" && root.activeSection === "sections"
                enabled: root.sectionsEditable
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationTextBoxTools
            {
                objectName: "presentationTextBoxTools"
                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "text" && root.activeSection === "textBox"
                selection: root.selection
                enabled: root.supports("formatTextBox")
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationTextStyleTools
            {
                objectName: "presentationTextStyleTools"
                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "text" && root.activeSection === "wordArt"
                selection: root.selection
                enabled: root.supports("formatTextStyle")
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationObjectTools
            {
                id: objectTools

                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "object" && (root.activeSection === "geometry" || root.activeSection === "appearance")
                section: root.activeSection
                selection: root.selection
                enabled: root.supports(root.activeSection === "geometry" ? "transformShape" : "formatShape")
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationTableTools
            {
                objectName: "presentationTableTools"
                Layout.fillWidth: true
                theme: root.theme
                selection: root.selection
                section: root.activeSection
                visible: root.activeGroup === "table" && (root.activeSection === "tableStyle"
                    || root.activeSection === "cellFill"
                    || root.activeSection === "cellBorder")
                enabled: root.supports(root.activeSection === "tableStyle" ? "formatTableStyle"
                    : root.activeSection === "cellFill" ? "formatTableCell" : "formatTableBorder")
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationTableInsertTools
            {
                objectName: "presentationTableInsertTools"
                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "insert" && root.activeSection === "tableInsert"
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationGroupTools
            {
                objectName: "presentationGroupTools"
                Layout.fillWidth: true
                theme: root.theme
                groupId: root.selection.groupId || ""
                canUngroup: root.selection.groupUngroupable === true
                canExtend: root.selection.groupExtensible === true
                layerOptions: root.selection.groupLayerOptions || ({})
                previousIndex: root.selection.groupPrevIndex === undefined ? -1 : root.selection.groupPrevIndex
                nextIndex: root.selection.groupNextIndex === undefined ? -1 : root.selection.groupNextIndex
                visible: root.activeGroup === "object" && root.activeSection === "group"
                enabled: root.selection.groupEditable === true || root.supports("groupAdjacent")
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationFormatBrushTools
            {
                objectName: "presentationFormatBrushTools"
                Layout.fillWidth: true
                theme: root.theme
                selection: root.selection
                sourceId: root.formatBrushSourceId
                visible: root.activeGroup === "object" && root.activeSection === "formatBrush"
                onCaptureRequested: root.formatBrushArmed(root.selection.id)
                onApplyRequested: root.formatBrushApplyRequested()
                onCancelRequested: root.formatBrushCancelled()
            }

            PresentationLinkTools
            {
                objectName: "presentationLinkTools"
                Layout.fillWidth: true
                theme: root.theme
                selection: root.selection
                slideCount: root.slideCount
                currentSlide: root.currentSlide
                visible: root.activeGroup === "object" && root.activeSection === "link"
                enabled: root.supports("setClickAction")
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationGradientTools
            {
                objectName: "presentationGradientTools"
                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "object" && root.activeSection === "gradient"
                selection: root.selection
                enabled: root.supports("formatShape") && root.selection.isImage !== true
                onEditRequested: function(action, options) { root.editRequested(action, options); }
            }

            PresentationImageTools
            {
                objectName: "presentationImageTools"
                Layout.fillWidth: true
                theme: root.theme
                visible: root.activeGroup === "object" && root.activeSection === "image"
                selection: root.selection
                enabled: root.supports("formatImage") && root.selection.isImage === true
                onEditRequested: function(action, options) { root.editRequested(action, options); }
                onReplaceRequested: root.imageReplaceRequested()
            }

            Text
            {
                Layout.fillWidth: true
                visible: root.activeGroup === "academic" && root.activeSection !== "templates"
                text: root.activeSection === "references"
                    ? "插入可编辑的资料占位页；请按课程要求填写并核对引用。"
                    : "在当前页之后插入新页面，双击文字即可填写自己的内容。"
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
                wrapMode: Text.Wrap
            }

            Text
            {
                visible: !root.hasSelection && (root.activeGroup === "text" || root.activeGroup === "object")
                text: "先在画布中选择要编辑的对象。"
                color: root.theme.textSecondary
                font.pixelSize: root.theme.fontSize - 2
            }
        }
    }
}
