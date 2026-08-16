import QtQuick

Item {
    id: root

    required property QtObject theme
    required property QtObject bridge

    width: UiTheme.toolButtonSize + UiTheme.toolButtonFrameSize
    height: UiTheme.toolButtonSize + UiTheme.toolButtonFrameSize

    Image {
        anchors.centerIn: parent
        width: 20
        height: 20
        source: "../textures/icons/Settings/gear/128w/Artboard 1.png"
        sourceSize.width: width
        sourceSize.height: height
        fillMode: Image.PreserveAspectFit
        opacity: settingsMouseArea.pressed ? 0.65 : settingsMouseArea.containsMouse ? 0.85 : 1.0
    }

    MouseArea {
        id: settingsMouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: settingsPopup.visible ? settingsPopup.close() : settingsPopup.open()
    }

    UiPanelPopup {
        id: settingsPopup
        theme: root.theme
        x: 0
        y: root.height + 4
        width: 178

        UiCheckBox {
            theme: root.theme
            text: qsTr("Background")
            checked: root.bridge.backgroundMode === 0
            onClicked: root.bridge.setBackgroundMode(checked ? 0 : 1)
        }

        UiCheckBox {
            theme: root.theme
            text: qsTr("Navigation Cube")
            checked: root.bridge.navigationCubeVisible
            onClicked: root.bridge.setNavigationCubeVisible(checked)
        }

        UiCheckBox {
            theme: root.theme
            text: qsTr("Axis Labels")
            checked: root.bridge.axisLabelsVisible
            onClicked: root.bridge.setAxisLabelsVisible(checked)
        }
    }
}
