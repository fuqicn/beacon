import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string variant: "elevated"
    property var cardContent: null

    implicitWidth: 300
    implicitHeight: contentItem ? contentItem.implicitHeight + 32 : 120

    radius: Theme.shapeMedium
    color: {
        switch (variant) {
            case "elevated": return Theme.surfaceContainerLow
            case "filled": return Theme.surfaceContainerHigh
            case "outlined": return Theme.surface
            default: return Theme.surfaceContainerLow
        }
    }

    border.width: variant === "outlined" ? 1 : 0
    border.color: variant === "outlined" ? Theme.outlineVariant : "transparent"

    layer.enabled: variant === "elevated"
    layer.effect: elevationEffect

    Behavior on color { ColorAnimation { duration: 200 } }

    property var elevationEffect: Component {
        Item {
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: root.radius + 4
                color: Qt.rgba(0,0,0,0.08)
                z: -1
            }
        }
    }

    default property alias content: contentArea.children

    ColumnLayout {
        id: contentArea
        x: 16; y: 16
        width: parent.width - 32
        height: parent.height - 32
        spacing: 8
    }
}
