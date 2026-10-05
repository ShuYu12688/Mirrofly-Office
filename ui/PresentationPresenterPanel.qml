pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Mirrorfly.Native 1.0
import "components"

Rectangle
{
    id: root
    objectName: "presentationPresenterPanel"

    required property var theme
    required property var document
    required property string speakerNotes
    required property int nextSlide
    required property int currentSlide
    required property int slideCount
    required property bool canPrevious
    required property bool canNext
    property bool dualScreen: false
    property double startedAt: Date.now()
    property double elapsedSeconds: 0

    function clockText(seconds)
    {
        const minutes = Math.floor(seconds / 60);
        const rest = Math.floor(seconds % 60);
        return Math.floor(minutes / 60).toString().padStart(2, "0") + ":"
            + (minutes % 60).toString().padStart(2, "0") + ":"
            + rest.toString().padStart(2, "0");
    }

    signal previousRequested()
    signal nextRequested()
    signal closeRequested()

    radius: theme.radius
    color: theme.surfaceColor
    border.width: 1
    border.color: theme.borderColor

    Timer
    {
        interval: 1000
        repeat: true
        running: root.visible
        onTriggered: root.elapsedSeconds = Math.floor((Date.now() - root.startedAt) / 1000)
    }

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Text
        {
            Layout.fillWidth: true
            text: (root.dualScreen ? "演讲者视图" : "演讲排练") + " · "
                + (root.currentSlide + 1) + " / " + root.slideCount
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize + 3
            font.weight: Font.DemiBold
        }

        Text
        {
            objectName: "presentationPresenterClock"
            Layout.fillWidth: true
            text: root.clockText(root.elapsedSeconds)
            color: root.theme.slidesAccent
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize + 17
            font.weight: Font.DemiBold
        }

        Text
        {
            text: "下一页"
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize
        }

        Rectangle
        {
            Layout.fillWidth: true
            Layout.preferredHeight: width * nextPreview.implicitHeight
                / Math.max(1, nextPreview.implicitWidth)
            visible: root.nextSlide >= 0
            color: root.theme.slidesPaper
            border.width: 1
            border.color: root.theme.borderColor

            SlideThumbnail
            {
                id: nextPreview
                objectName: "presentationPresenterNextSlide"
                anchors.fill: parent
                anchors.margins: 1
                document: root.document
                theme: root.theme
                slideIndex: root.nextSlide
            }
        }

        Text
        {
            visible: root.nextSlide < 0
            text: "已到最后一页"
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize
        }

        Text
        {
            text: "演讲备注"
            color: root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontSize
        }

        ScrollView
        {
            objectName: "presentationPresenterNotes"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea
            {
                text: root.speakerNotes.length > 0 ? root.speakerNotes : "本页没有备注"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSize + 2
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: true
                background: null
            }
        }

        RowLayout
        {
            Layout.fillWidth: true

            ActionButton
            {
                theme: root.theme
                text: "上一页"
                compact: true
                enabled: root.canPrevious
                onClicked: root.previousRequested()
            }

            ActionButton
            {
                theme: root.theme
                text: "下一页"
                compact: true
                enabled: root.canNext
                onClicked: root.nextRequested()
            }
        }

        ActionButton
        {
            Layout.fillWidth: true
            theme: root.theme
            text: root.dualScreen ? "结束放映" : "结束排练"
            compact: true
            onClicked: root.closeRequested()
        }
    }
}
