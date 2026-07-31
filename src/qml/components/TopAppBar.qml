import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property string title: "Title"
    property string subtitle: ""
    property string variant: "small"
    property var navAction: null

    implicitWidth: parent.width
    implicitHeight: variant === "compact" ? 48 : 64

    color: Theme.surface

    Row {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 16
        spacing: 4

        Item {
            width: childrenRect.width
            height: parent.height
            visible: root.navAction !== null
            Loader {
                anchors.centerIn: parent
                sourceComponent: root.navAction
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Text {
                text: root.title
                font.weight: variant === "medium" || variant === "large" ? Font.Normal : Font.Medium
                font.pixelSize: variant === "large" ? 28 : (variant === "medium" ? 24 : 22)
                color: Theme.onSurface
                lineHeight: 1.2
            }
            Text {
                text: root.subtitle
                font.pixelSize: 14
                color: Theme.onSurfaceVariant
                visible: root.subtitle !== ""
            }
        }
    }
}
