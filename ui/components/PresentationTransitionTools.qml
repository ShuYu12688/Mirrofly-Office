pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout
{
    id: root

    property var theme
    property var transition: ({})
    property string pendingType: "cut"
    property string pendingDirection: "l"
    property real pendingDuration: 0.5
    property bool pendingClick: true
    property bool pendingTimer: false
    property real pendingSeconds: 5
    signal editRequested(string action, var options)

    function refresh()
    {
        pendingType = ["cut", "fade", "push"].includes(transition.type)
            ? transition.type : "cut";
        pendingDirection = ["l", "r", "u", "d"].includes(transition.direction)
            ? transition.direction : "l";
        pendingDuration = [0.3, 0.5, 1.0].includes(transition.durationSeconds)
            ? transition.durationSeconds : 0.5;
        pendingClick = transition.advanceOnClick !== false;
        pendingTimer = transition.advanceAfterSeconds >= 0;
        pendingSeconds = pendingTimer ? Number(transition.advanceAfterSeconds) : 5;
    }

    onTransitionChanged: refresh()
    Component.onCompleted: refresh()
    spacing: 8

    Label
    {
        Layout.fillWidth: true
        text: root.transition.editable === false
            ? "此页切换包含受保护内容，暂不能编辑。"
            : "设置进入当前页时的切换方式。"
        color: root.theme.textSecondary
        font.family: root.theme.fontFamily
        font.pixelSize: root.theme.fontSize - 2
        wrapMode: Text.Wrap
    }

    RowLayout
    {
        Label { text: "效果"; color: root.theme.textSecondary }
        ComboBox
        {
            objectName: "presentationTransitionType"
            Layout.preferredWidth: 150
            textRole: "label"
            valueRole: "value"
            model: [{label: "直接切换", value: "cut"}, {label: "淡入", value: "fade"},
                {label: "推入", value: "push"}]
            currentIndex: root.pendingType === "fade" ? 1 : root.pendingType === "push" ? 2 : 0
            onActivated: root.pendingType = currentValue
            Accessible.name: "页面切换效果"
        }
        Label { text: "速度"; color: root.theme.textSecondary }
        ComboBox
        {
            objectName: "presentationTransitionSpeed"
            Layout.preferredWidth: 110
            textRole: "label"
            valueRole: "value"
            model: [{label: "快", value: 0.3}, {label: "标准", value: 0.5},
                {label: "慢", value: 1.0}]
            currentIndex: root.pendingDuration === 0.3 ? 0 : root.pendingDuration === 1.0 ? 2 : 1
            onActivated: root.pendingDuration = currentValue
            Accessible.name: "页面切换速度"
        }
    }

    RowLayout
    {
        visible: root.pendingType === "push"
        Label { text: "方向"; color: root.theme.textSecondary }
        ComboBox
        {
            objectName: "presentationTransitionDirection"
            Layout.preferredWidth: 150
            textRole: "label"
            valueRole: "value"
            model: [{label: "向左", value: "l"}, {label: "向右", value: "r"},
                {label: "向上", value: "u"}, {label: "向下", value: "d"}]
            currentIndex: root.pendingDirection === "r" ? 1
                : root.pendingDirection === "u" ? 2 : root.pendingDirection === "d" ? 3 : 0
            onActivated: root.pendingDirection = currentValue
            Accessible.name: "页面切换方向"
        }
    }

    RowLayout
    {
        CheckBox
        {
            objectName: "presentationTransitionClick"
            text: "单击后换页"
            checked: root.pendingClick
            onClicked: root.pendingClick = checked
        }
        CheckBox
        {
            objectName: "presentationTransitionTimer"
            text: "自动换页"
            checked: root.pendingTimer
            onClicked: root.pendingTimer = checked
        }
        SpinBox
        {
            objectName: "presentationTransitionSeconds"
            enabled: root.pendingTimer
            from: 0
            to: 86400
            value: Math.round(root.pendingSeconds)
            editable: true
            onValueModified: root.pendingSeconds = value
            Accessible.name: "自动换页等待秒数"
        }
        Label { text: "秒"; color: root.theme.textSecondary }
    }

    ActionButton
    {
        objectName: "presentationApplyTransition"
        theme: root.theme
        text: "应用页面切换"
        compact: true
        onClicked: root.editRequested("setSlideTransition", {
            type: root.pendingType,
            direction: root.pendingType === "push" ? root.pendingDirection : "",
            durationSeconds: root.pendingDuration,
            advanceOnClick: root.pendingClick,
            advanceAfterSeconds: root.pendingTimer ? root.pendingSeconds : -1
        })
    }
}
