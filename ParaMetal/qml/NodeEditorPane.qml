pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ParaMetal

Rectangle {
    id: root
    required property QtObject theme
    required property QtObject graphModel
    required property QtObject runtimeNotifier
    color: theme.panelBackground

    function nodeIconFolder(typeId) {
        switch (String(typeId)) {
        case "contact": return "Contact"
        case "heat_solve": return "HeatSystem"
        case "model": return "Model"
        case "heat_model": return "HeatModel"
        case "remesh": return "Remesh"
        case "transform": return "Transform"
        case "voronoi": return "VoronoiSystem"
        case "mesh_points": return "Points"
        default: return ""
        }
    }

    function nodeIconSource(typeId) {
        const folder = nodeIconFolder(typeId)
        return folder.length > 0
            ? "../textures/icons/" + folder + "/128w/Artboard 1.png"
            : ""
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: UiSplitterHandle {
            orientation: Qt.Vertical
        }

        Rectangle {
            SplitView.preferredHeight: 260
            SplitView.minimumHeight: 150
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
                            visible: source.toString().length > 0
                            source: root.nodeIconSource(graphModel.selectedNodeType)
                            sourceSize.width: 24
                            sourceSize.height: 24
                            Layout.preferredWidth: visible ? 24 : 0
                            Layout.preferredHeight: 24
                            fillMode: Image.PreserveAspectFit
                        }
                        Text {
                            text: graphModel.selectedNodeId > 0
                                ? (graphModel.selectedNodeTitle.length > 0 ? graphModel.selectedNodeTitle : graphModel.selectedNodeType)
                                : qsTr("No Node Selected")
                            color: theme.text
                            font.family: theme.fontFamily
                            font.pixelSize: theme.titleFontSize
                            font.letterSpacing: 0.75
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 12
                    Layout.bottomMargin: 10
                    text: graphModel.selectedNodeDescription
                    color: theme.secondaryText
                    font.family: theme.fontFamily
                    font.pixelSize: theme.descriptionFontSize
                    font.weight: Font.Light
                    wrapMode: Text.Wrap
                }

                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    Layout.preferredHeight: 38
                    background: Rectangle { color: theme.panelBackground }
                    Repeater {
                        model: [qsTr("Node"), qsTr("Spreadsheet"), qsTr("Dataflow")]
                        TabButton {
                            id: tabButton
                            required property string modelData
                            width: implicitContentWidth + 20
                            text: modelData
                            font.family: theme.fontFamily
                            font.pixelSize: theme.regularFontSize
                            font.weight: theme.regularFontWeight
                            contentItem: Text {
                                text: tabButton.text
                                color: tabButton.checked ? theme.activeTabText : theme.mutedText
                                font: tabButton.font
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: "transparent"
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 3
                                    color: tabButton.checked ? theme.accent : "transparent"
                                }
                            }
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabs.currentIndex

                    NodeInspectorPane {
                        theme: root.theme
                        graphModel: root.graphModel
                        runtimeNotifier: root.runtimeNotifier
                    }
                    Rectangle {
                        color: theme.panelBackground
                        Text { anchors.centerIn: parent; text: qsTr("Spreadsheet data appears here"); color: theme.mutedText; font.family: theme.fontFamily; font.pixelSize: theme.descriptionFontSize }
                    }
                    Rectangle {
                        color: theme.panelBackground
                        Text { anchors.centerIn: parent; text: qsTr("Dataflow diagnostics appear here"); color: theme.mutedText; font.family: theme.fontFamily; font.pixelSize: theme.descriptionFontSize }
                    }
                }
            }

        }

        Rectangle {
            id: graphViewport
            SplitView.fillHeight: true
            color: theme.panelBackground
            clip: true
            property int contextNodeId: 0
            property real createX: 0
            property real createY: 0

            NodeGraphCanvasItem {
                anchors.fill: parent
                model: graphModel
                focus: true
                onCreateMenuRequested: function(graphX, graphY) {
                    graphViewport.createX = graphX
                    graphViewport.createY = graphY
                    createMenu.popup()
                }
                onNodeMenuRequested: function(nodeId) {
                    graphViewport.contextNodeId = nodeId
                    nodeMenu.popup()
                }
            }

            UiMenu {
                id: nodeMenu
                theme: root.theme
                Action {
                    text: qsTr("Display")
                    onTriggered: graphModel.toggleNodeDisplay(graphViewport.contextNodeId)
                }
                Action {
                    text: qsTr("Freeze")
                    onTriggered: graphModel.toggleNodeFrozen(graphViewport.contextNodeId)
                }
                UiMenuSeparator { theme: root.theme }
                Action {
                    text: qsTr("Delete")
                    onTriggered: graphModel.removeNode(graphViewport.contextNodeId)
                }
            }

            UiMenu {
                id: createMenu
                theme: root.theme
                Instantiator {
                    model: graphModel.nodeCategories
                    delegate: UiMenu {
                        id: categoryMenu
                        required property var modelData
                        theme: root.theme
                        title: modelData.name
                        Instantiator {
                            model: modelData.types
                            delegate: Action {
                                required property var modelData
                                text: modelData.name
                                onTriggered: graphModel.addNode(modelData.typeId, graphViewport.createX, graphViewport.createY)
                            }
                            onObjectAdded: function(index, object) { categoryMenu.insertAction(index, object) }
                            onObjectRemoved: function(index, object) { categoryMenu.removeAction(object) }
                        }
                    }
                    onObjectAdded: function(index, object) { createMenu.insertMenu(index, object) }
                    onObjectRemoved: function(index, object) { createMenu.removeMenu(object) }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 12
                spacing: 18
                Repeater {
                    model: [
                        { icon: "mouse_middle", label: qsTr("Pan") },
                        { icon: "mouse_right", label: qsTr("Menu") },
                        { icon: "node_right", label: qsTr("Display") },
                        { icon: "node_left", label: qsTr("Freeze") }
                    ]
                    Row {
                        required property var modelData

                        spacing: 14
                        Image {
                            width: 20; height: 20
                            source: "../textures/icons/NodeGraph_nav/" + modelData.icon + "/128w/Artboard 1.png"
                            sourceSize.width: width
                            sourceSize.height: height
                            fillMode: Image.PreserveAspectFit
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: theme.navigationText
                            font.family: theme.fontFamily
                            font.pixelSize: theme.descriptionFontSize
                            font.weight: Font.Light
                        }
                    }
                }
            }
        }
    }
}
