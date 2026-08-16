import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property QtObject theme
    required property QtObject bridge

    width: UiTheme.toolButtonGroupWidth(2)
    height: UiTheme.toolButtonSize + UiTheme.toolButtonFrameSize

    function syncFov(force) {
        if (force || !fovField.activeFocus)
            fovField.text = Number(bridge.cameraFov).toFixed(1)
    }

    function commitFov() {
        const input = fovField.text.trim()
        const parsed = Number(input)
        if (input.length === 0 || !Number.isFinite(parsed)) {
            syncFov(true)
            return
        }
        const clamped = Math.max(1.0, Math.min(120.0, parsed))
        fovField.text = clamped.toFixed(1)
        bridge.setCameraFov(clamped)
    }

    function syncZoomSpeed(force) {
        if (force || !zoomSpeedField.activeFocus)
            zoomSpeedField.text = Number(bridge.cameraZoomSpeed).toFixed(1)
    }

    function commitZoomSpeed() {
        const input = zoomSpeedField.text.trim()
        const parsed = Number(input)
        if (input.length === 0 || !Number.isFinite(parsed)) {
            syncZoomSpeed(true)
            return
        }
        const clamped = Math.max(0.1, Math.min(5.0, parsed))
        zoomSpeedField.text = clamped.toFixed(1)
        bridge.setCameraZoomSpeed(clamped)
    }

    function syncPanSpeed(force) {
        if (force || !panSpeedField.activeFocus)
            panSpeedField.text = Number(bridge.cameraPanSpeed).toFixed(1)
    }

    function commitPanSpeed() {
        const input = panSpeedField.text.trim()
        const parsed = Number(input)
        if (input.length === 0 || !Number.isFinite(parsed)) {
            syncPanSpeed(true)
            return
        }
        const clamped = Math.max(0.1, Math.min(5.0, parsed))
        panSpeedField.text = clamped.toFixed(1)
        bridge.setCameraPanSpeed(clamped)
    }

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.theme.toolBorder

        Row {
            x: UiTheme.toolButtonFrameInset
            y: UiTheme.toolButtonFrameInset
            spacing: UiTheme.toolButtonGroupSpacing

            ToolIconButton {
                segment: 1
                checkable: false
                iconSize: 20
                imageSource: "../textures/icons/Overlays/camera/128w/Artboard 1.png"
                onClicked: root.bridge.focusWorldOrigin()
            }

            ToolIconButton {
                segment: 3
                checkable: false
                iconSize: 14
                imageSource: "../textures/icons/Overlays/arrow/128w/Artboard 1.png"
                onClicked: cameraPopup.visible ? cameraPopup.close() : cameraPopup.open()
            }
        }
    }

    UiPanelPopup {
        id: cameraPopup
        theme: root.theme
        x: 0
        y: root.height + 4
        width: 232

        ButtonGroup { id: projectionGroup }

        RowLayout {
            Layout.fillWidth: true
            spacing: 1

            Button {
                id: perspectiveButton
                Layout.fillWidth: true
                implicitHeight: 28
                checkable: true
                ButtonGroup.group: projectionGroup
                checked: root.bridge.projectionMode === 0
                text: qsTr("Perspective")
                onClicked: root.bridge.setProjectionMode(0)
                contentItem: Text {
                    text: perspectiveButton.text
                    color: root.theme.text
                    font: root.theme.regularFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    topLeftRadius: 3
                    bottomLeftRadius: 3
                    color: perspectiveButton.checked ? root.theme.interactiveAccent
                                                     : perspectiveButton.hovered ? root.theme.toolHover
                                                                                 : root.theme.toolNormal
                }
            }

            Button {
                id: orthographicButton
                Layout.fillWidth: true
                implicitHeight: 28
                checkable: true
                ButtonGroup.group: projectionGroup
                checked: root.bridge.projectionMode === 1
                text: qsTr("Orthographic")
                onClicked: root.bridge.setProjectionMode(1)
                contentItem: Text {
                    text: orthographicButton.text
                    color: root.theme.text
                    font: root.theme.regularFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    topRightRadius: 3
                    bottomRightRadius: 3
                    color: orthographicButton.checked ? root.theme.interactiveAccent
                                                      : orthographicButton.hovered ? root.theme.toolHover
                                                                                   : root.theme.toolNormal
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 5
            spacing: 8

            RowLayout {
                Layout.preferredWidth: 80
                spacing: 4
                enabled: root.bridge.projectionMode === 0
                opacity: enabled ? 1.0 : 0.45

                Text {
                    Layout.preferredWidth: 24
                    text: qsTr("FOV")
                    color: root.theme.text
                    font: root.theme.regularFont
                }

                TextField {
                    id: fovField
                    Layout.preferredWidth: 52
                    implicitHeight: 28
                    color: root.theme.text
                    selectionColor: root.theme.accent
                        selectedTextColor: root.theme.onAccentText
                    font: root.theme.regularFont
                    padding: 7
                    horizontalAlignment: TextInput.AlignRight
                    verticalAlignment: TextInput.AlignVCenter
                    validator: DoubleValidator {
                        bottom: 1.0
                        top: 120.0
                        decimals: 1
                    }
                    background: Rectangle {
                        radius: 3
                        color: root.theme.inputBackground
                        border.width: 1
                        border.color: fovField.activeFocus ? root.theme.accent : root.theme.border
                    }
                    onEditingFinished: root.commitFov()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Zoom Speed")
                    color: root.theme.text
                    font: root.theme.regularFont
                }

                TextField {
                    id: zoomSpeedField
                    Layout.preferredWidth: 44
                    implicitHeight: 28
                    color: root.theme.text
                    selectionColor: root.theme.accent
                        selectedTextColor: root.theme.onAccentText
                    font: root.theme.regularFont
                    padding: 7
                    horizontalAlignment: TextInput.AlignRight
                    verticalAlignment: TextInput.AlignVCenter
                    validator: DoubleValidator {
                        bottom: 0.1
                        top: 5.0
                        decimals: 1
                    }
                    background: Rectangle {
                        radius: 3
                        color: root.theme.inputBackground
                        border.width: 1
                        border.color: zoomSpeedField.activeFocus ? root.theme.accent : root.theme.border
                    }
                    onEditingFinished: root.commitZoomSpeed()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: qsTr("Pan Speed")
                color: root.theme.text
                font: root.theme.regularFont
            }

            TextField {
                id: panSpeedField
                Layout.preferredWidth: 44
                implicitHeight: 28
                color: root.theme.text
                selectionColor: root.theme.accent
                    selectedTextColor: root.theme.onAccentText
                font: root.theme.regularFont
                padding: 7
                horizontalAlignment: TextInput.AlignRight
                verticalAlignment: TextInput.AlignVCenter
                validator: DoubleValidator {
                    bottom: 0.1
                    top: 5.0
                    decimals: 1
                }
                background: Rectangle {
                    radius: 3
                    color: root.theme.inputBackground
                    border.width: 1
                    border.color: panSpeedField.activeFocus ? root.theme.accent : root.theme.border
                }
                onEditingFinished: root.commitPanSpeed()
            }

            Item { Layout.fillWidth: true }
        }
    }

    Component.onCompleted: {
        syncFov()
        syncZoomSpeed()
        syncPanSpeed()
    }

    Connections {
        target: root.bridge
        function onStateChanged() {
            root.syncFov()
            root.syncZoomSpeed()
            root.syncPanSpeed()
        }
    }
}
