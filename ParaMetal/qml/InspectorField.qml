import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property QtObject theme
    property var parameter: null
    property string label: parameter ? parameter.name : ""
    property string editor: "number"
    property real minimum: 0
    property real maximum: 1
    property int decimals: 3
    property var choices: []
    property bool compact: false
    signal valueEdited(var value)

    function formattedValue() {
        if (!root.parameter) return ""
        if (root.editor === "text") return String(root.parameter.value)
        const value = Number(root.parameter.value)
        if (!isFinite(value)) return ""
        if (root.decimals === 0) return String(Math.round(value))
        let formatted = value.toFixed(root.decimals)
        formatted = formatted.replace(/0+$/, "")
        if (formatted.endsWith(".")) formatted += "0"
        return formatted
    }

    function selectedChoiceText() {
        if (!root.parameter) return ""
        const value = String(root.parameter.value)
        const items = root.choices.length > 0 ? root.choices : root.parameter.options
        for (let index = 0; index < items.length; ++index) {
            const item = items[index]
            const itemValue = item.value === undefined ? item : item.value
            if (String(itemValue) === value)
                return String(item.text === undefined ? item : item.text)
        }
        return ""
    }

    implicitHeight: editor === "slider" ? 29 : 28
    visible: parameter !== null && parameter !== undefined

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            Layout.preferredWidth: root.compact ? 14 : 145
            text: root.label
            color: root.theme.text
            font: root.theme.regularFont
            elide: Text.ElideRight
        }

        Slider {
            id: slider
            visible: root.editor === "slider"
            Layout.fillWidth: visible
            Layout.preferredWidth: visible ? 100 : 0
            from: root.minimum
            to: root.maximum
            value: root.parameter ? Number(root.parameter.value) : 0
            onMoved: root.valueEdited(root.decimals === 0 ? Math.round(value) : value)
            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - 2
                width: slider.availableWidth
                height: 4
                radius: 2
                color: "#696875"
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: parent.radius
                    color: root.theme.accent
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: 16; height: 16; radius: 8
                color: "#2f2e35"
                border.width: 2
                border.color: "#e6e4ef"
            }
        }

        TextField {
            id: numberField
            visible: root.editor === "number" || root.editor === "slider" || root.editor === "text"
            Layout.fillWidth: root.editor !== "slider"
            Layout.preferredWidth: root.editor === "slider" ? 68 : -1
            text: root.formattedValue()
            color: root.theme.text
            selectionColor: root.theme.accent
            selectedTextColor: "white"
            font: root.theme.regularFont
            padding: 7
            verticalAlignment: TextInput.AlignVCenter
            background: Rectangle {
                radius: 3
                color: "#242429"
                border.width: 1
                border.color: numberField.activeFocus ? root.theme.accent : root.theme.border
            }
            onEditingFinished: {
                if (root.editor === "text") root.valueEdited(text)
                else {
                    const parsed = Number(text)
                    if (!isNaN(parsed)) root.valueEdited(root.decimals === 0 ? Math.round(parsed) : parsed)
                }
            }
        }

        CheckBox {
            id: checkBox
            visible: root.editor === "bool"
            Layout.fillWidth: visible
            checked: root.parameter ? Boolean(root.parameter.value) : false
            text: ""
            onToggled: root.valueEdited(checked)
            indicator: Rectangle {
                x: checkBox.leftPadding
                y: (checkBox.height - height) / 2
                implicitWidth: 14
                implicitHeight: 14
                radius: 4
                color: checkBox.checked ? root.theme.accent : "#3a3946"
                border.width: 1
                border.color: checkBox.checked ? root.theme.accent : "#77758b"
                Rectangle {
                    anchors.centerIn: parent
                    width: 6
                    height: 6
                    radius: 2
                    visible: checkBox.checked
                    color: "#ffffff"
                }
            }
        }

        UiComboBox {
            id: combo
            theme: root.theme
            visible: root.editor === "combo"
            Layout.fillWidth: visible
            Layout.preferredHeight: 28
            model: root.choices.length > 0 ? root.choices : (root.parameter ? root.parameter.options : [])
            textRole: model && model.length > 0 && model[0].text !== undefined ? "text" : ""
            valueRole: model && model.length > 0 && model[0].value !== undefined ? "value" : ""
            currentIndex: {
                if (!root.parameter) return -1
                const value = String(root.parameter.value)
                for (let index = 0; index < model.length; ++index) {
                    const item = model[index]
                    if (String(item.value === undefined ? item : item.value) === value) return index
                }
                return -1
            }
            onActivated: {
                const item = model[currentIndex]
                root.valueEdited(item.value === undefined ? item : item.value)
            }
            contentItem: Text {
                leftPadding: 7
                text: root.selectedChoiceText()
                color: root.theme.text
                font: combo.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }
}
