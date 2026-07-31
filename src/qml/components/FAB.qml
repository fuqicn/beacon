import QtQuick

Rectangle {
    id: root

    property string iconText: ""
    property string label: ""
    property string size: "medium"
    property bool extended: false

    signal clicked()

    implicitWidth: extended ? (labelText.implicitWidth + (iconText ? 56 : 32)) : (size === "small" ? 40 : (size === "large" ? 96 : 56))
    implicitHeight: size === "small" ? 40 : (size === "large" ? 96 : 56)

    radius: size === "large" ? Theme.shapeLarge : Theme.shapeLarge
    color: Theme.primaryContainer

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: ma.pressed ? Qt.rgba(0,0,0,0.12) : (ma.hovered ? Qt.rgba(0,0,0,0.08) : "transparent")
        Behavior on color { ColorAnimation { duration: 150 } }
    }

    Row {
        anchors.centerIn: parent
        spacing: 8
        Text {
            id: iconTextItem
            text: root.iconText
            font.family: "Material Symbols Outlined"
            font.pixelSize: root.size === "small" ? 20 : 24
            color: Theme.onPrimaryContainer
            visible: root.iconText !== ""
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: labelText
            text: root.label
            font.weight: Font.DemiBold
            font.pixelSize: 14
            color: Theme.onPrimaryContainer
            visible: root.extended || root.label !== ""
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
