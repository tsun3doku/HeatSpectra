import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ParaMetal

Rectangle {
    id: root
    width: 1600
    height: 900
    readonly property QtObject theme: UiTheme
    color: theme.windowBackground

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        MenuBar {
            id: menuBar
            Layout.fillWidth: true
            Layout.preferredHeight: 25
            font.family: theme.fontFamily
            palette.window: theme.menuBarBackground
            palette.windowText: theme.text
            palette.buttonText: theme.text
            palette.text: theme.text
            palette.highlight: theme.hover
            palette.highlightedText: theme.text
            background: Rectangle { color: theme.menuBarBackground }
            delegate: MenuBarItem {
                id: menuBarItem
                implicitWidth: contentItem.implicitWidth + 20
                implicitHeight: 25
                leftPadding: 10
                rightPadding: 10
                topPadding: 0
                bottomPadding: 0

                contentItem: Text {
                    text: menuBarItem.text
                    color: menuBarItem.highlighted ? theme.headingText : theme.text
                    font.family: theme.fontFamily
                    font.pixelSize: theme.regularFontSize
                    font.weight: Font.Light
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: menuBarItem.highlighted ? theme.hover : "transparent"
                }
            }

            UiMenu {
                theme: root.theme
                title: qsTr("File")
                Action { text: qsTr("New"); onTriggered: project.newProject() }
                Action { text: qsTr("Open..."); onTriggered: openProjectDialog.open() }
                Action {
                    text: qsTr("Save")
                    onTriggered: {
                        if (project.path.length > 0) project.save()
                        else saveProjectDialog.open()
                    }
                }
                Action { text: qsTr("Save As..."); onTriggered: saveProjectDialog.open() }
                UiMenuSeparator { theme: root.theme }
                Action { text: qsTr("Exit"); onTriggered: Qt.quit() }
            }
            UiMenu {
                theme: root.theme
                title: qsTr("View")
                Action { text: qsTr("Console"); checkable: true; checked: consolePane.visible; onTriggered: consolePane.visible = checked }
                Action { text: qsTr("Node Graph"); checkable: true; checked: nodeEditor.visible; onTriggered: nodeEditor.visible = checked }
            }
        }

        SplitView {
            id: mainSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 7
                color: theme.subtleBorder
            }

            ConsolePane {
                id: consolePane
                SplitView.preferredWidth: 327
                SplitView.minimumWidth: 0
                SplitView.maximumWidth: 520
                theme: root.theme
                bridge: ui.console
            }

            ColumnLayout {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                spacing: 0

                SplitView {
                    id: workspaceSplit
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Horizontal

                    handle: Rectangle {
                        implicitWidth: 7
                        color: theme.subtleBorder
                    }

                    NodeEditorPane {
                        id: nodeEditor
                        SplitView.preferredWidth: 365
                        SplitView.minimumWidth: 365
                        SplitView.maximumWidth: 620
                        theme: root.theme
                        graphModel: ui.nodeGraph
                        runtimeNotifier: runtimeStatus
                    }

                    ViewportPane {
                        SplitView.fillWidth: true
                        SplitView.fillHeight: true
                          theme: root.theme
                          bridge: ui.viewport
                          heatPalette: ui.heatPalette
                    }
                }

                TimelineBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    theme: root.theme
                    bridge: ui.timeline
                }
            }
        }
    }

    FileDialog {
        id: openProjectDialog
        title: qsTr("Open ParaMetal Project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("ParaMetal projects (*.pm)"), qsTr("All files (*)")]
        onAccepted: project.open(selectedFile)
    }

    FileDialog {
        id: saveProjectDialog
        title: qsTr("Save ParaMetal Project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pm"
        nameFilters: [qsTr("ParaMetal projects (*.pm)"), qsTr("All files (*)")]
        onAccepted: project.saveAs(selectedFile)
    }

    MessageDialog {
        id: projectErrorDialog
        title: qsTr("Project Error")
        buttons: MessageDialog.Ok
    }

    Connections {
        target: project
        function onSaveAsRequired() { saveProjectDialog.open() }
        function onError(message) {
            projectErrorDialog.text = message
            projectErrorDialog.open()
        }
    }
}
