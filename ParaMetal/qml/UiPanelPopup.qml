import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: control

    required property QtObject theme
    default property alias panelContent: contentColumn.data

    popupType: Popup.Item
    padding: 5
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 5
    }

    background: Rectangle {
        color: control.theme.panelBackground
        border.width: 1
        border.color: control.theme.toolBorder
        radius: 3
    }
}
