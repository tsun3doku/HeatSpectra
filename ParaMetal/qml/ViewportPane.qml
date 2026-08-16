import QtQuick
import ParaMetal

Rectangle {
    id: root
    required property QtObject theme
    required property QtObject bridge
    required property QtObject heatPalette
    color: theme.windowBackground

    ViewportItem {
        objectName: "viewportItem"
        anchors.fill: parent
        focus: true
    }

    TemperaturePalette {
        theme: root.theme
        heatPalette: root.heatPalette
        z: 10
    }

    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 10
        anchors.topMargin: 10
        spacing: 5
        z: 20

        ViewportSettingsControl {
            theme: root.theme
            bridge: root.bridge
        }

        CameraControl {
            theme: root.theme
            bridge: root.bridge
        }
    }

    Row {
        anchors.top: parent.top
        anchors.topMargin: 10
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 5

        Rectangle {
            width: UiTheme.toolButtonGroupWidth(2)
            height: UiTheme.toolButtonSize + UiTheme.toolButtonFrameSize
            radius: 5
            color: theme.toolBorder
            Row {
                x: UiTheme.toolButtonFrameInset
                y: UiTheme.toolButtonFrameInset
                spacing: UiTheme.toolButtonGroupSpacing
                ToolIconButton {
                    segment: 1
                    iconSize: 20
                    imageSource: "../textures/icons/Overlays/wireframe/128w/Artboard 1.png"
                    checked: bridge.wireframeMode === 1
                    onClicked: bridge.setWireframeMode(checked ? 1 : 0)
                }
                ToolIconButton {
                    segment: 3
                    iconSize: 20
                    imageSource: "../textures/icons/Overlays/wireframe_shaded/128w/Artboard 1.png"
                    checked: bridge.wireframeMode === 2
                    onClicked: bridge.setWireframeMode(checked ? 2 : 0)
                }
            }
        }

        Rectangle {
            width: UiTheme.toolButtonSize + UiTheme.toolButtonFrameSize
            height: UiTheme.toolButtonSize + UiTheme.toolButtonFrameSize
            radius: 5
            color: theme.toolBorder
            ToolIconButton {
                x: UiTheme.toolButtonFrameInset
                y: UiTheme.toolButtonFrameInset
                iconSize: 20
                imageSource: "../textures/icons/Overlays/grid/128w/Artboard 1.png"
                checked: bridge.gridEnabled
                onClicked: bridge.setGridEnabled(checked)
            }
        }
    }
}
