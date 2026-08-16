import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

CheckBox {
    id: control

    required property QtObject theme

    implicitHeight: 26
    Layout.fillWidth: true
    leftPadding: 24
    rightPadding: 4
    font: theme.regularFont

    indicator: Item {
        x: 4
        y: (control.height - height) / 2
        width: 14
        height: 14

        HoverHandler {
            id: indicatorHover
        }

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: control.checked ? control.theme.accent
                                   : indicatorHover.hovered ? control.theme.toolHover : control.theme.checkboxBackground
            border.width: 1
            border.color: control.checked ? control.theme.accent : control.theme.checkboxBorder
        }

        Image {
            anchors.fill: parent
            source: "../textures/icons/Settings/check/128w/Artboard 1.png"
            sourceSize.width: width
            sourceSize.height: height
            visible: control.checked
        }

        opacity: indicatorHover.hovered ? 0.7 : 1.0
    }

    contentItem: Text {
        text: control.text
        color: control.theme.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 3
        color: "transparent"
    }
}
