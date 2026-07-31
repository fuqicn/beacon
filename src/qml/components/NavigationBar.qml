import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property int currentIndex: 0
    property var model: []
    signal activated(int index)

    implicitHeight: 80
    color: Theme.surface

    Row {
        anchors.fill: parent
        Repeater {
            model: root.model
            delegate: Item {
                width: parent.width / root.model.length
                height: parent.height

                Column {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.icon || ""
                        font.family: "Material Symbols Outlined"
                        font.pixelSize: 24
                        color: index === root.currentIndex ? Theme.primary : Theme.onSurfaceVariant
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label || ""
                        font.pixelSize: 12
                        font.bold: index === root.currentIndex
                        color: index === root.currentIndex ? Theme.primary : Theme.onSurfaceVariant
                    }
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 64
                    height: 3
                    radius: Theme.shapeExtraSmall
                    color: index === root.currentIndex ? Theme.primary : "transparent"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.currentIndex = index
                        root.activated(index)
                    }
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
