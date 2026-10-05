import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item
{
    id: root
    property var theme
    property var agent
    property string islandStatus: ""
    property real chromeInset: 0
    property var files: []
    property int recentCount: 0
    property string category: "all"
    property string query: ""
    property string notice: ""
    property string expandedSection: ""
    property string route: "home"
    property real controlsOpacity: 1
    property bool brandVisible: true
    readonly property Item brandMark: sidebar.brandMark
    readonly property string routeTitle: route === "create" ? "开始创作"
        : (route === "recent" ? "最近文件" : (route === "ai" ? "AI 模型" : "首页"))

    signal openRequested()
    signal createRequested(string kind)
    signal queryEdited(string query)
    signal categorySelected(string category)
    signal fileRequested(string path)
    signal starRequested(string path)
    signal replayRequested()
    signal dismissNotice()
    signal openIslandRequested()

    function navigate(destination)
    {
        searchFocusTimer.stop();
        route = destination;
        expandedSection = destination === "home" ? "" : destination;
        pageScroll.contentY = 0;
    }
    function focusSearch()
    {
        navigate("recent");
        searchFocusTimer.restart();
    }
    DynamicBackground { id: homeBackdrop; anchors.fill: parent; theme: root.theme; animated: false }
    Timer
    {
        id: searchFocusTimer
        interval: 16
        onTriggered: if (root.route === "recent") recentPanel.focusSearch()
    }
    SidebarNavigation
    {
        id: sidebar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 20
        width: 224
        theme: root.theme
        currentRoute: root.route
        controlsOpacity: root.controlsOpacity
        brandVisible: root.brandVisible
        onRouteRequested: function(route) { root.navigate(route); }
        onReplayRequested: root.replayRequested()
    }
    Flickable
    {
        id: pageScroll
        opacity: root.controlsOpacity
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.chromeInset
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        contentWidth: width
        contentHeight: Math.max(height, pageContent.implicitHeight + 60)
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column
        {
            id: pageContent
            x: (pageScroll.width - width) / 2
            y: 24
            width: Math.max(1, Math.min(root.theme.workspaceMaxWidth, pageScroll.width - 40))
            spacing: root.theme.workspaceGap
            WorkspaceHeader
            {
                width: parent.width
                theme: root.theme
                ribbonCycling: root.route === "home" && root.controlsOpacity >= 0.99
                heading: root.route === "home" ? "今天"
                    : (root.route === "local" ? "打开文件" : root.routeTitle)
                subtitle: root.route === "home" ? dashboard.greeting
                    : (root.route === "create" ? "让想法成形。"
                        : (root.route === "recent" ? root.recentCount + " 份最近文件"
                            : (root.route === "ai" ? "连接你的工作伙伴。" : "从熟悉的内容继续。")))
            }
            HomeDashboard
            {
                id: dashboard
                backdrop: homeBackdrop
                backdropOrigin: Qt.point(pageScroll.x + pageContent.x + x,
                    pageScroll.y + pageContent.y + y - pageScroll.contentY)
                objectName: "homeDashboard"
                width: parent.width
                visible: root.route === "home"
                theme: root.theme
                agent: root.agent
                recentCount: root.recentCount
                onOpenRequested: root.openRequested()
                onCreateRequested: root.navigate("create")
            }
            CreateWorkspace
            {
                objectName: "createWorkspace"
                width: parent.width
                visible: root.route === "create"
                theme: root.theme
                onCreateRequested: function(kind) { root.createRequested(kind); }
            }
            RecentFilesPanel
            {
                id: recentPanel
                width: parent.width
                visible: root.route === "recent"
                theme: root.theme
                files: root.files
                query: root.query
                category: root.category
                onOpenRequested: root.openRequested()
                onQueryEdited: function(query) { root.queryEdited(query); }
                onCategorySelected: function(category) { root.categorySelected(category); }
                onFileRequested: function(path) { root.fileRequested(path); }
                onStarRequested: function(path) { root.starRequested(path); }
            }
            AiModelSettingsPage
            {
                width: parent.width
                visible: root.route === "ai"
                theme: root.theme
                agent: root.agent
                islandStatus: root.islandStatus
                onOpenIslandRequested: root.openIslandRequested()
            }
            DashboardSurface
            {
                width: parent.width
                height: 150
                visible: root.route === "local"
                theme: root.theme
                ActionButton
                {
                    anchors.centerIn: parent
                    theme: root.theme
                    text: "选择本地文件"
                    iconName: "folder"
                    primary: true
                    onClicked: root.openRequested()
                }
            }
        }
    }
    Rectangle
    {
        anchors.horizontalCenter: pageScroll.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 26
        width: Math.min(pageScroll.width - 48, 650)
        height: noticeText.implicitHeight + 28
        radius: root.theme.radius * 0.7
        color: root.theme.noticeBackground
        opacity: root.theme.surfaceOpacity * root.controlsOpacity
        visible: root.notice.length > 0

        RowLayout
        {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 16

            Text
            {
                id: noticeText

                Layout.fillWidth: true
                text: root.notice
                color: root.theme.noticeForeground
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize - 2
                wrapMode: Text.Wrap
                Accessible.role: Accessible.AlertMessage
            }

            AbstractButton
            {
                id: dismissButton

                implicitWidth: 25
                implicitHeight: 25
                hoverEnabled: true
                activeFocusOnTab: true
                padding: 2
                Accessible.name: "关闭提示"
                onClicked: root.dismissNotice()

                contentItem: UiIcon
                {
                    symbol: "close"
                    iconColor: root.theme.noticeForeground
                }

                background: Rectangle
                {
                    color: dismissButton.hovered || dismissButton.down
                        ? root.theme.navigationHover : root.theme.transparentColor
                    border.width: dismissButton.visualFocus ? 1 : 0
                    border.color: root.theme.noticeForeground
                    radius: 5

                    Behavior on color
                    {
                        ColorAnimation
                        {
                            duration: root.theme.motionEnabled ? root.theme.hoverDuration : 0
                        }
                    }
                }
            }
        }
    }
}
