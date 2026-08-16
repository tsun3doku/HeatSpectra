import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property QtObject theme
    required property QtObject bridge
    color: theme.panelBackground

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: theme.panelBackground
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 7
                Image {
                    source: "../textures/icons/Terminal/128w/Artboard 1.png"
                    sourceSize.width: 18
                    sourceSize.height: 18
                }
                Text {
                    text: qsTr("Console")
                    color: theme.text
                    font.family: theme.fontFamily
                    font.pixelSize: theme.titleFontSize
                    font.letterSpacing: 0.75
                }
                Item { Layout.fillWidth: true }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                text: bridge.output
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: theme.text
                font.family: theme.monoFamily
                font.pixelSize: theme.consoleFontSize
                leftPadding: 10
                rightPadding: 10
                topPadding: 6
                bottomPadding: 6
                background: Rectangle { color: theme.panelBackground }
                onTextChanged: cursorPosition = length
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.subtleBorder }

        TextField {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            placeholderText: ">>>"
            color: theme.text
            placeholderTextColor: theme.mutedText
            font.family: theme.monoFamily
            font.pixelSize: theme.consoleFontSize
            leftPadding: 10
            rightPadding: 10
            background: Rectangle { color: theme.panelBackground }
            onAccepted: {
                bridge.execute(text)
                text = ""
            }
        }
    }
}
