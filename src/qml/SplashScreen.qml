import QtQuick
import QtQuick.Controls

Rectangle {
    id: splashRoot
    width: 420
    height: 280
    color: sysPalette.window

    SystemPalette { id: sysPalette }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Qt.alpha(sysPalette.placeholderText, 0.15)
        border.width: 1

        Column {
            anchors.centerIn: parent
            spacing: 20

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Beacon"
                font.pixelSize: 36
                font.weight: Font.Bold
                color: sysPalette.highlight
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: true
                implicitWidth: 40
                implicitHeight: 40
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "正在启动"
                font.pixelSize: 14
                color: sysPalette.placeholderText
            }
        }
    }
}
