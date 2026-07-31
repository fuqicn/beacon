import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property int currentIndex: 0
    property var model: []
    signal activated(int index)

    implicitWidth: 80
    color: Theme.surface

    Column {
        anchors.fill: parent
        anchors.topMargin: 12
        spacing: 8

        Repeater {
            model: root.model
            delegate: Item {
                width: root.width
                height: 72

                Column {
                    anchors.centerIn: parent
                    spacing: 4

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 56
                        height: 32
                        radius: Theme.shapeExtraLarge
                        color: index === root.currentIndex ? Theme.secondaryContainer : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData.icon || ""
                            font.pixelSize: 24
                            color: index === root.currentIndex ? Theme.onSecondaryContainer : Theme.onSurfaceVariant
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label || ""
                        font.pixelSize: 12
                        font.bold: index === root.currentIndex
                        color: index === root.currentIndex ? Theme.onSecondaryContainer : Theme.onSurfaceVariant
                    }
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
