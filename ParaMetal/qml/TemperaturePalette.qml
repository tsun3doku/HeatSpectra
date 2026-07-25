pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property QtObject theme
    required property QtObject heatPalette

    visible: heatPalette.visible
    width: 156
    height: 286
    radius: 4
    color: theme.panelBackground
    border.color: theme.border
    Binding {
        target: root
        property: "x"
        value: heatPalette.x >= 0 ? heatPalette.x : 16
        when: !dragArea.drag.active
    }
    Binding {
        target: root
        property: "y"
        value: heatPalette.y >= 0 ? heatPalette.y : root.parent.height - root.height - 16
        when: !dragArea.drag.active
    }

    function suffix() { return heatPalette.units === 1 ? "K" : heatPalette.units === 2 ? "°F" : "°C" }
    function paletteSource() {
        if (heatPalette.paletteId === 0) return "../textures/palettes/inferno2.png"
        if (heatPalette.paletteId === 1) return "../textures/palettes/parula.png"
        if (heatPalette.paletteId === 2) return "../textures/palettes/viridis.png"
        return "../textures/palettes/inferno.png"
    }
    function toDisplay(c) {
        if (heatPalette.units === 1) return c + 273.15
        if (heatPalette.units === 2) return c * 9 / 5 + 32
        return c
    }
    function toCelsius(value) {
        if (heatPalette.units === 1) return value - 273.15
        if (heatPalette.units === 2) return (value - 32) * 5 / 9
        return value
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 5

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Temperature"); color: theme.headingText; font: theme.regularFont }
            MouseArea {
                id: dragArea
                anchors.fill: parent
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                drag.target: root
                drag.axis: Drag.XAndYAxis
                drag.minimumX: 0
                drag.maximumX: Math.max(0, root.parent.width - root.width)
                drag.minimumY: 0
                drag.maximumY: Math.max(0, root.parent.height - root.height)
                onReleased: root.heatPalette.setPosition(root.x, root.y)
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.border }

        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Text { text: qsTr("Palette"); color: theme.text; font: theme.regularFont; Layout.preferredWidth: 48 }
            UiComboBox {
                id: paletteBox; theme: root.theme; Layout.fillWidth: true; Layout.preferredHeight: 28
                model: [
                    { text: qsTr("Inferno 2"), value: 0 },
                    { text: qsTr("Parula"), value: 1 },
                    { text: qsTr("Viridis"), value: 2 },
                    { text: qsTr("Inferno"), value: 3 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: {
                    for (let i = 0; i < model.length; ++i) {
                        if (model[i].value === heatPalette.paletteId) return i
                    }
                    return 0
                }
                onActivated: heatPalette.setPaletteId(model[currentIndex].value)
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Text { text: qsTr("Units"); color: theme.text; font: theme.regularFont; Layout.preferredWidth: 48 }
            UiComboBox {
                id: unitsBox; theme: root.theme; Layout.fillWidth: true; Layout.preferredHeight: 28
                model: [qsTr("°C"), qsTr("K"), qsTr("°F")]
                currentIndex: heatPalette.units
                onActivated: heatPalette.setUnits(currentIndex)
            }
        }

        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height
                spacing: 7
                Image {
                    width: 42; height: parent.height
                    source: root.paletteSource()
                    fillMode: Image.Stretch
                    smooth: false
                }
                Column {
                    width: 85; height: parent.height
                    Row {
                        width: parent.width; height: 28; spacing: 4
                        TextField {
                            id: maxField; width: 68; height: 28
                            text: root.toDisplay(heatPalette.maximumC).toFixed(1)
                            color: theme.text; font: theme.regularFont; padding: 4
                            validator: DoubleValidator {}
                            background: Rectangle { radius: 3; color: theme.cardBackground; border.color: maxField.activeFocus ? theme.accent : theme.border }
                            onEditingFinished: { const v = Number(text); if (isFinite(v)) heatPalette.setRange(heatPalette.minimumC, root.toCelsius(v)) }
                        }
                        Text {
                            width: parent.width - maxField.width - 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.suffix()
                            color: theme.text
                            font: theme.regularFont
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    Item { width: 1; height: Math.max(1, parent.height - 56) }
                    Row {
                        width: parent.width; height: 28; spacing: 4
                        TextField {
                            id: minField; width: 68; height: 28
                            text: root.toDisplay(heatPalette.minimumC).toFixed(1)
                            color: theme.text; font: theme.regularFont; padding: 4
                            validator: DoubleValidator {}
                            background: Rectangle { radius: 3; color: theme.cardBackground; border.color: minField.activeFocus ? theme.accent : theme.border }
                            onEditingFinished: { const v = Number(text); if (isFinite(v)) heatPalette.setRange(root.toCelsius(v), heatPalette.maximumC) }
                        }
                        Text {
                            width: parent.width - minField.width - 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.suffix()
                            color: theme.text
                            font: theme.regularFont
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }
        }
    }
}
