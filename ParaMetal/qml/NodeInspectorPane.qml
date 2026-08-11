pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    required property QtObject theme
    required property QtObject graphModel
    required property QtObject runtimeNotifier
    property bool heatSolveActive: false
    property bool heatSolvePaused: false
    property string serialConnectionText: "Not used by an active Heat Solve"
    property string serialTemperatureText: "--"
    property string serialPollingRateText: "--"

    Connections {
        target: root.runtimeNotifier
        function onHeatSolveStatusChanged(active, paused) {
            root.heatSolveActive = active
            root.heatSolvePaused = paused
        }
        function onSerialStatusChanged(connection, temperature, pollingRate) {
            root.serialConnectionText = connection
            root.serialTemperatureText = temperature
            root.serialPollingRateText = pollingRate
        }
    }

    Component.onCompleted: {
        root.heatSolveActive = root.runtimeNotifier.heatSolveActive()
        root.heatSolvePaused = root.runtimeNotifier.heatSolvePaused()
        root.serialConnectionText = root.runtimeNotifier.serialConnectionText()
        root.serialTemperatureText = root.runtimeNotifier.serialTemperatureText()
        root.serialPollingRateText = root.runtimeNotifier.serialPollingRateText()
    }

    function parameter(parameterId) {
        const parameters = graphModel.selectedNodeParameters
        for (let index = 0; index < parameters.length; ++index) {
            if (parameters[index].id === parameterId) return parameters[index]
        }
        return null
    }
    function setValue(parameterId, value) { graphModel.setParameterValue(parameterId, value) }
    function panelForType(typeId) {
        switch (String(typeId)) {
        case "model": return modelPanel
        case "transform": return transformPanel
        case "group": return groupPanel
        case "remesh": return remeshPanel
        case "voronoi": return voronoiPanel
        case "contact": return contactPanel
        case "heat_model": return heatModelPanel
        case "heat_solve": return heatSolvePanel
        case "serial_temperature": return serialTemperaturePanel
        case "points": return pointsPanel
        default: return genericPanel
        }
    }

    Flickable {
        id: inspectorScroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height,
                                panelLoader.item ? panelLoader.item.implicitHeight + 40 : height)
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 4000
        maximumFlickVelocity: 900
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }

        Rectangle {
            x: 12
            width: Math.max(1, root.width - 24)
            height: inspectorScroll.contentHeight
            color: graphModel.selectedNodeId > 0 ? root.theme.cardBackground : root.theme.panelBackground
            border.width: graphModel.selectedNodeId > 0 ? 1 : 0
            border.color: root.theme.border
            radius: graphModel.selectedNodeId > 0 ? 10 : 0

            Text {
                visible: graphModel.selectedNodeId === 0
                x: 10; y: 2
                text: qsTr("This node has no editable actions yet")
                color: root.theme.text
                font: root.theme.regularFont
            }

            Loader {
                id: panelLoader
                visible: graphModel.selectedNodeId > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                sourceComponent: root.panelForType(graphModel.selectedNodeType)
            }
        }
    }

    Component {
        id: modelPanel
        Column {
            width: panelLoader.width
            spacing: 7
            Text { text: qsTr("Model File:"); color: root.theme.text; font: root.theme.regularFont }
            RowLayout {
                width: parent.width
                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 29
                    readOnly: true
                    text: root.parameter(1) ? root.parameter(1).value : ""
                    placeholderText: "models/teapot.obj"
                    color: root.theme.text
                    font: root.theme.regularFont
                    background: Rectangle { radius: 3; color: "#242429"; border.width: 1; border.color: root.theme.border }
                }
                Button {
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 29
                    text: qsTr("Browse...")
                    font: root.theme.regularFont
                    padding: 0
                    contentItem: Text {
                        text: parent.text
                        color: root.theme.text
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? "#474664" : "#3a3950"
                        border.width: 1
                        border.color: "#585670"
                    }
                    onClicked: modelDialog.open()
                }
            }
            FileDialog {
                id: modelDialog
                title: qsTr("Select Model File")
                nameFilters: [qsTr("3D Models (*.obj *.stl)"), qsTr("OBJ files (*.obj)"), qsTr("STL files (*.stl)"), qsTr("All files (*)")]
                onAccepted: {
                    let path = selectedFile.toString()
                    if (path.indexOf("file:///") === 0) path = path.substring(8)
                    root.setValue(1, decodeURIComponent(path))
                }
            }
            InspectorField { width: parent.width; theme: root.theme; editor: "combo"; label: qsTr("Import Units"); parameter: root.parameter(2); onValueEdited: value => root.setValue(2, value) }
        }
    }

    Component {
        id: transformPanel
        Column {
            width: panelLoader.width
            spacing: 11
            RowLayout {
                width: parent.width; spacing: 7
                Text { Layout.preferredWidth: 66; text: qsTr("Translate:"); color: root.theme.text; font: root.theme.regularFont }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "X"; parameter: root.parameter(1); decimals: 4; onValueEdited: value => root.setValue(1, value) }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "Y"; parameter: root.parameter(2); decimals: 4; onValueEdited: value => root.setValue(2, value) }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "Z"; parameter: root.parameter(3); decimals: 4; onValueEdited: value => root.setValue(3, value) }
            }
            RowLayout {
                width: parent.width; spacing: 7
                Text { Layout.preferredWidth: 66; text: qsTr("Rotate:"); color: root.theme.text; font: root.theme.regularFont }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "X"; parameter: root.parameter(4); decimals: 3; onValueEdited: value => root.setValue(4, value) }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "Y"; parameter: root.parameter(5); decimals: 3; onValueEdited: value => root.setValue(5, value) }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "Z"; parameter: root.parameter(6); decimals: 3; onValueEdited: value => root.setValue(6, value) }
            }
            RowLayout {
                width: parent.width; spacing: 7
                Text { Layout.preferredWidth: 66; text: qsTr("Scale:"); color: root.theme.text; font: root.theme.regularFont }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "X"; parameter: root.parameter(7); decimals: 4; onValueEdited: value => root.setValue(7, value) }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "Y"; parameter: root.parameter(8); decimals: 4; onValueEdited: value => root.setValue(8, value) }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "Z"; parameter: root.parameter(9); decimals: 4; onValueEdited: value => root.setValue(9, value) }
            }
        }
    }

    Component {
        id: groupPanel
        Column {
            width: panelLoader.width; spacing: 9
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Enable Assignment"); editor: "bool"; parameter: root.parameter(1); onValueEdited: value => root.setValue(1, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Source Type"); editor: "combo"; choices: [{text:"Vertex",value:0},{text:"Object",value:1},{text:"Material",value:2},{text:"Smooth",value:3}]; parameter: root.parameter(4); onValueEdited: value => root.setValue(4, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Source Name"); editor: "text"; parameter: root.parameter(2); onValueEdited: value => root.setValue(2, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Target Group Name"); editor: "text"; parameter: root.parameter(3); onValueEdited: value => root.setValue(3, value) }
            Text { width: parent.width; wrapMode: Text.Wrap; text: qsTr("Select a source type and source name from incoming mesh data, then assign triangles to a target group name."); color: root.theme.mutedText; font: root.theme.descriptionFont }
        }
    }

    Component {
        id: remeshPanel
        Column {
            width: panelLoader.width; spacing: 8
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Iterations"); parameter: root.parameter(1); minimum: 1; maximum: 1000; decimals: 0; onValueEdited: value => root.setValue(1, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Min Angle"); parameter: root.parameter(2); minimum: 0; maximum: 60; decimals: 1; onValueEdited: value => root.setValue(2, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Max Edge Length"); parameter: root.parameter(3); minimum: .001; maximum: 1; decimals: 4; onValueEdited: value => root.setValue(3, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Step Size"); parameter: root.parameter(4); minimum: .01; maximum: 1; decimals: 2; onValueEdited: value => root.setValue(4, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Remesh Overlay"); parameter: root.parameter(6); onValueEdited: value => root.setValue(6, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Face Normals"); parameter: root.parameter(7); onValueEdited: value => root.setValue(7, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Vertex Normals"); parameter: root.parameter(8); onValueEdited: value => root.setValue(8, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Normal Length"); parameter: root.parameter(9); minimum: .001; maximum: 1; decimals: 3; onValueEdited: value => root.setValue(9, value) }
        }
    }

    Component {
        id: voronoiPanel
        Column {
            width: panelLoader.width; spacing: 8
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("SDF Size"); parameter: root.parameter(1); minimum: .0001; maximum: .1; decimals: 6; onValueEdited: value => root.setValue(1, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Voxel Resolution"); parameter: root.parameter(2); minimum: 1; maximum: 1024; decimals: 0; onValueEdited: value => root.setValue(2, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Show Voronoi"); parameter: root.parameter(3); onValueEdited: value => root.setValue(3, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Show Points"); parameter: root.parameter(4); onValueEdited: value => root.setValue(4, value) }
        }
    }

    Component {
        id: contactPanel
        Column {
            width: panelLoader.width; spacing: 8
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Min Normal Dot"); parameter: root.parameter(1); minimum: -1; maximum: 1; decimals: 3; onValueEdited: value => root.setValue(1, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Contact Radius"); parameter: root.parameter(2); minimum: .0001; maximum: .1; decimals: 4; onValueEdited: value => root.setValue(2, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Show Contact Lines"); parameter: root.parameter(3); onValueEdited: value => root.setValue(3, value) }
        }
    }

    Component {
        id: heatModelPanel
        Column {
            width: panelLoader.width; spacing: 8
            InspectorField { width: parent.width; theme: root.theme; editor: "combo"; label: qsTr("Material Preset"); parameter: root.parameter(7); onValueEdited: value => graphModel.setHeatMaterialPreset(value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "combo"; label: qsTr("Boundary Condition"); parameter: root.parameter(5); onValueEdited: value => root.setValue(5, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Initial Temperature (C)"); parameter: root.parameter(4); decimals: 2; onValueEdited: value => root.setValue(4, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Boundary Temperature (C)"); parameter: root.parameter(6); decimals: 2; onValueEdited: value => root.setValue(6, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Inward Heat Flux (W/m²)"); parameter: root.parameter(8); onValueEdited: value => root.setValue(8, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("HTC (W/(m²·K))"); parameter: root.parameter(9); onValueEdited: value => root.setValue(9, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Power Density (W/m³)"); parameter: root.parameter(10); onValueEdited: value => root.setValue(10, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Density (kg/m³)"); parameter: root.parameter(1); decimals: 1; onValueEdited: value => root.setValue(1, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Specific Heat (J/(kg·K))"); parameter: root.parameter(2); decimals: 1; onValueEdited: value => root.setValue(2, value) }
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Conductivity (W/(m·K))"); parameter: root.parameter(3); decimals: 2; onValueEdited: value => root.setValue(3, value) }
        }
    }

    Component {
        id: heatSolvePanel
        Column {
            width: panelLoader.width; spacing: 8
            RowLayout {
                width: parent.width
                Text { text: qsTr("Status:"); color: root.theme.text; font: root.theme.regularFont }
                Text {
                    Layout.fillWidth: true
                    text: root.heatSolveActive
                        ? (root.heatSolvePaused ? qsTr("Paused") : qsTr("Running"))
                        : (root.parameter(1) && root.parameter(1).value
                            ? (root.parameter(2) && root.parameter(2).value ? qsTr("Pending Pause") : qsTr("Pending Start"))
                            : qsTr("Stopped"))
                    color: root.theme.text
                    font: root.theme.regularFont
                }
            }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Contact Thermal Conductance"); parameter: root.parameter(6); minimum: 0; maximum: 100000; decimals: 0; onValueEdited: value => root.setValue(6, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Simulation Duration (s)"); parameter: root.parameter(11); minimum: .1; maximum: 60; decimals: 1; onValueEdited: value => root.setValue(11, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Heat Overlay"); parameter: root.parameter(5); onValueEdited: value => root.setValue(5, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Flux Vectors"); parameter: root.parameter(7); onValueEdited: value => root.setValue(7, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Heat Palette"); parameter: root.parameter(9); onValueEdited: value => root.setValue(9, value) }
            InspectorField { width: parent.width; theme: root.theme; editor: "slider"; label: qsTr("Flux Vector Scale"); parameter: root.parameter(8); minimum: 0; maximum: 10; decimals: 2; onValueEdited: value => root.setValue(8, value) }
        }
    }

    Component {
        id: serialTemperaturePanel
        Column {
            width: panelLoader.width; spacing: 8
            InspectorField { width: parent.width; theme: root.theme; editor: "bool"; label: qsTr("Enabled"); parameter: root.parameter(1); onValueEdited: value => root.setValue(1, value) }
            RowLayout {
                width: parent.width
                InspectorField { Layout.fillWidth: true; theme: root.theme; editor: "combo"; label: qsTr("Port"); parameter: root.parameter(2); choices: graphModel.serialPorts; onValueEdited: value => root.setValue(2, value) }
                Button { text: qsTr("Refresh"); onClicked: graphModel.refreshSerialPorts() }
            }
            InspectorField { width: parent.width; theme: root.theme; editor: "combo"; label: qsTr("Baud Rate"); parameter: root.parameter(3); choices: [9600,19200,38400,57600,115200,230400]; onValueEdited: value => root.setValue(3, value) }
            RowLayout {
                width: parent.width
                Text { text: qsTr("Status:"); color: root.theme.text; font: root.theme.regularFont }
                Text { Layout.fillWidth: true; text: root.serialConnectionText; color: root.theme.text; font: root.theme.regularFont; wrapMode: Text.Wrap }
            }
            RowLayout {
                width: parent.width
                Text { text: qsTr("Latest Temperature:"); color: root.theme.text; font: root.theme.regularFont }
                Text { Layout.fillWidth: true; text: root.serialTemperatureText; color: root.theme.text; font: root.theme.regularFont }
            }
            RowLayout {
                width: parent.width
                Text { text: qsTr("Polling Rate:"); color: root.theme.text; font: root.theme.regularFont }
                Text { Layout.fillWidth: true; text: root.serialPollingRateText; color: root.theme.text; font: root.theme.regularFont }
            }
        }
    }

    Component {
        id: pointsPanel
        Column {
            width: panelLoader.width; spacing: 11
            InspectorField { width: parent.width; theme: root.theme; label: qsTr("Point Count"); parameter: root.parameter(1); decimals: 0; onValueEdited: value => root.setValue(1, value) }
            RowLayout {
                width: parent.width; spacing: 7
            Text { Layout.preferredWidth: 66; text: qsTr("Dimensions:"); color: root.theme.text; font: root.theme.regularFont }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "X"; parameter: root.parameter(2); decimals: 6; onValueEdited: value => root.setValue(2, value) }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "Y"; parameter: root.parameter(3); decimals: 6; onValueEdited: value => root.setValue(3, value) }
                InspectorField { Layout.fillWidth: true; theme: root.theme; compact: true; label: "Z"; parameter: root.parameter(4); decimals: 6; onValueEdited: value => root.setValue(4, value) }
            }
        }
    }

    Component {
        id: genericPanel
        Column {
            width: panelLoader.width; spacing: 12
            Text { visible: graphModel.selectedNodeParameters.length === 0; text: qsTr("This node has no editable actions yet"); color: root.theme.text; font: root.theme.regularFont }
            Repeater {
                model: graphModel.selectedNodeParameters
                InspectorField {
                    required property var modelData
                    width: parent.width
                    theme: root.theme
                    parameter: modelData
                    editor: modelData.type === 2 ? "bool" : (modelData.type === 4 ? "combo" : (modelData.type === 3 ? "text" : "number"))
                    onValueEdited: value => root.setValue(modelData.id, value)
                }
            }
        }
    }
}
