import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    required property QtObject theme

    implicitHeight: 28
    font: theme.regularFont

    contentItem: Text {
        leftPadding: 7
        rightPadding: 20
        text: control.displayText
        color: control.theme.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 8
        y: (control.height - height) / 2
        text: "▾"
        color: control.theme.mutedText
        font: control.theme.regularFont
    }

    background: Rectangle {
        radius: 3
        color: control.theme.inputBackground
        border.width: 1
        border.color: control.activeFocus ? control.theme.accent : control.theme.border
    }

    popup: Popup {
        y: control.height
        width: control.width
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            delegate: ItemDelegate {
                id: delegateItem
                required property int index
                width: control.width - 2
                height: 24
                padding: 0
                highlighted: control.highlightedIndex === index
                contentItem: Text {
                    leftPadding: 10
                    text: control.textAt(index)
                    color: control.theme.text
                    font: control.theme.regularFont
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: delegateItem.highlighted ? control.theme.hover : "transparent"
                }
            }
        }
        background: Rectangle {
            color: control.theme.cardBackground
            border.width: 1
            border.color: control.theme.border
            radius: 3
        }
    }
}
