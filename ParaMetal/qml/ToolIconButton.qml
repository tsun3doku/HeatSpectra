import QtQuick
import QtQuick.Controls

Button {
    id: control
    property url imageSource
    property int segment: 0
    property bool mirrorHorizontal: false
    property real iconSize: 0
    property color baseColor: UiTheme.toolNormal
    property color hoverColor: UiTheme.toolHover
    property color pressedColor: UiTheme.toolPressed
    property color selectedColor: UiTheme.interactiveAccent
    property color selectedPressedColor: UiTheme.toolSelectedPressed
    implicitWidth: UiTheme.toolButtonSize
    implicitHeight: UiTheme.toolButtonSize
    padding: UiTheme.toolButtonPadding
    checkable: true

    background: Rectangle {
        topLeftRadius: control.segment === 0 || control.segment === 1 ? 4 : 0
        bottomLeftRadius: control.segment === 0 || control.segment === 1 ? 4 : 0
        topRightRadius: control.segment === 0 || control.segment === 3 ? 4 : 0
        bottomRightRadius: control.segment === 0 || control.segment === 3 ? 4 : 0
        color: control.checked
            ? (control.down ? control.selectedPressedColor : control.selectedColor)
            : (control.down ? control.pressedColor : (control.hovered ? control.hoverColor : control.baseColor))
        border.width: 0
    }

    contentItem: Item {
        Image {
            anchors.centerIn: parent
            width: control.iconSize > 0 ? control.iconSize : parent.width
            height: control.iconSize > 0 ? control.iconSize : parent.height
            source: control.imageSource
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            opacity: control.enabled ? 1.0 : 0.45
            mirror: control.mirrorHorizontal
        }
    }
}
