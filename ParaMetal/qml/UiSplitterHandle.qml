import QtQuick

Rectangle {
    id: control

    property int orientation: Qt.Horizontal

    implicitWidth: orientation === Qt.Horizontal ? UiTheme.splitterWidth : 0
    implicitHeight: orientation === Qt.Vertical ? UiTheme.splitterWidth : 0
    color: UiTheme.splitterCenter

    Rectangle {
        visible: control.orientation === Qt.Horizontal
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: UiTheme.splitterBorderWidth
        color: UiTheme.splitterBorder
    }

    Rectangle {
        visible: control.orientation === Qt.Horizontal
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: UiTheme.splitterBorderWidth
        color: UiTheme.splitterBorder
    }

    Rectangle {
        visible: control.orientation === Qt.Vertical
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: UiTheme.splitterBorderWidth
        color: UiTheme.splitterBorder
    }

    Rectangle {
        visible: control.orientation === Qt.Vertical
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: UiTheme.splitterBorderWidth
        color: UiTheme.splitterBorder
    }
}
