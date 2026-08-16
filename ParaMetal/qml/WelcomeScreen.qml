import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

Item {
    id: root

    required property QtObject theme

    readonly property int choicePreviewSize: 160
    readonly property int choiceContentSpacing: 8
    readonly property int choiceTitleHeight: 18
    readonly property int choiceTileHeight: choicePreviewSize + choiceContentSpacing + choiceTitleHeight

    signal emptyGraphRequested()
    signal defaultGraphRequested()

    Rectangle {
        anchors.fill: parent
        color: root.theme.welcomeBackdrop

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }
    }

    component ChoiceButton: Button {
        id: choice

        required property string titleText
        property bool emphasized: false

        Layout.preferredWidth: root.choicePreviewSize
        Layout.minimumWidth: root.choicePreviewSize
        Layout.maximumWidth: root.choicePreviewSize
        Layout.preferredHeight: root.choiceTileHeight
        Layout.minimumHeight: root.choiceTileHeight
        Layout.maximumHeight: root.choiceTileHeight
        padding: 0

        contentItem: ColumnLayout {
            anchors.fill: parent
            spacing: root.choiceContentSpacing

            Item {
                Layout.preferredWidth: root.choicePreviewSize
                Layout.minimumWidth: root.choicePreviewSize
                Layout.maximumWidth: root.choicePreviewSize
                Layout.preferredHeight: root.choicePreviewSize
                Layout.minimumHeight: root.choicePreviewSize
                Layout.maximumHeight: root.choicePreviewSize
                Layout.alignment: Qt.AlignHCenter
                clip: true

                Image {
                    anchors.fill: parent
                    source: "../textures/preview.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    visible: choice.emphasized
                }

                Image {
                    anchors.fill: parent
                    source: "../textures/grid.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    visible: !choice.emphasized
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.preferredHeight: root.choiceTitleHeight
                text: choice.titleText
                color: root.theme.welcomeText
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.regularFontSize
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }

        }

        background: Rectangle {
            width: parent.width
            height: root.choicePreviewSize
            radius: 6
            color: choice.hovered ? root.theme.welcomeOptionHover : root.theme.welcomeOptionBackground
            border.width: choice.hovered ? 2 : 1
            border.color: choice.hovered ? root.theme.accent : root.theme.border
        }
    }

    Rectangle {
        id: welcomeCard
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(780, parent.width - 80)
        height: Math.min(510, parent.height - 80)
        radius: 12
        color: "transparent"
        clip: true

        Image {
            id: welcomeImageSource
            anchors.fill: parent
            source: "../textures/welcome.jpg"
            fillMode: Image.PreserveAspectCrop
            visible: false
        }

        MultiEffect {
            anchors.fill: parent
            source: welcomeImageSource
            maskEnabled: true
            maskSource: welcomeImageMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
            opacity: 0.75
        }

        Item {
            id: welcomeImageMask
            anchors.fill: parent
            layer.enabled: true
            layer.smooth: true
            visible: false

            Rectangle {
                anchors.fill: parent
                radius: 12
                color: "black"
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "transparent"
            border.width: 1
            border.color: root.theme.welcomeCardBorder
        }

        Rectangle {
            id: titleBar
            x: 0
            y: 0
            width: parent.width
            height: 32
            color: root.theme.welcomeOptionBackground

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 36
                cursorShape: Qt.SizeAllCursor
                property real pressX: 0
                property real pressY: 0

                onPressed: function(mouse) {
                    pressX = mouse.x
                    pressY = mouse.y
                }

                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const maximumX = Math.max(0, welcomeCard.parent.width - welcomeCard.width)
                    const maximumY = Math.max(0, welcomeCard.parent.height - welcomeCard.height)
                    welcomeCard.x = Math.max(0, Math.min(maximumX, welcomeCard.x + mouse.x - pressX))
                    welcomeCard.y = Math.max(0, Math.min(maximumY, welcomeCard.y + mouse.y - pressY))
                }
            }

            Item {
                anchors.right: parent.right
                anchors.top: parent.top
                width: 36
                height: 32

                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.visible = false
                }

                Image {
                    anchors.centerIn: parent
                    width: 12
                    height: 12
                    source: "../textures/icons/Menu/x/128w/Artboard 1.png"
                    sourceSize.width: width
                    sourceSize.height: height
                    opacity: closeArea.containsMouse ? 1.0 : 0.7
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.theme.border
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom
            anchors.leftMargin: 48
            anchors.rightMargin: 48
            anchors.topMargin: 48
            anchors.bottomMargin: 48
            spacing: 2

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Start a Project")
                    color: root.theme.welcomeText
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.titleFontSize + 40
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignRight
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Choose a starting point for your workspace")
                    color: root.theme.welcomeSubtext
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.regularFontSize
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignRight
                }
            }

            Item { Layout.preferredHeight: 10 }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 16

                ChoiceButton {
                    titleText: qsTr("Empty Graph")
                    onClicked: root.emptyGraphRequested()
                }

                ChoiceButton {
                    titleText: qsTr("Default Graph")
                    emphasized: true
                    onClicked: root.defaultGraphRequested()
                }
            }
        }
    }
}
