import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    height: kernel.downloadManager.busy ? panelContent.implicitHeight + 32 : 0
    radius: Theme.shapeLarge
    color: Qt.alpha(palette.window, 0.95)
    border.color: palette.mid
    border.width: 1
    clip: true

    // Absorb clicks so they don't reach items behind
    MouseArea {
        anchors.fill: parent
        propagateComposedEvents: false
        onPressed: function(mouse) { mouse.accepted = true }
        onReleased: function(mouse) { mouse.accepted = true }
        onClicked: function(mouse) { mouse.accepted = true }
    }

    property real shownY: parent ? parent.height - height - 24 : 0
    property real hiddenY: parent ? parent.height : 0
    y: kernel.downloadManager.busy ? shownY : hiddenY
    opacity: kernel.downloadManager.busy ? 1 : 0
    Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    Behavior on opacity { NumberAnimation { duration: 200 } }
    Behavior on height { NumberAnimation { duration: 250 } }

    readonly property real currentProgress: kernel.downloadManager.progress
    readonly property string mainTask: kernel.downloadManager.currentTask
    readonly property string subTaskText: kernel.downloadManager.subTask
    readonly property int total: kernel.downloadManager.totalFiles
    readonly property int completed: kernel.downloadManager.completedFiles
    readonly property double speed: kernel.downloadManager.speedBytes

    function formatSpeed(bytesPerSec) {
        if (bytesPerSec <= 0) return ""
        if (bytesPerSec > 1024 * 1024) return (bytesPerSec / (1024 * 1024)).toFixed(1) + " MB/s"
        if (bytesPerSec > 1024) return (bytesPerSec / 1024).toFixed(0) + " KB/s"
        return bytesPerSec.toFixed(0) + " B/s"
    }

    ColumnLayout {
        id: panelContent
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // Row 1: Task icon + main task + speed
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            // Animated download icon
            Rectangle {
                width: 32; height: 32; radius: Theme.shapeSmall
                color: Qt.alpha(palette.highlight, 0.15)

                AppIcon {
                    anchors.centerIn: parent
                    iconName: "download"
                    iconSize: 16
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: root.mainTask
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: palette.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: root.subTaskText
                    font.pixelSize: 11
                    color: palette.placeholderText
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    visible: text !== ""
                }
            }

            Text {
                text: formatSpeed(root.speed)
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: palette.highlight
                font.italic: true
            }
        }

        // Row 2: Progress bar
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Rectangle {
                id: progressTrack
                Layout.fillWidth: true
                height: 6
                radius: Theme.shapeExtraSmall
                color: Qt.alpha(palette.highlight, 0.12)
                clip: true

                Rectangle {
                    id: progressBar
                    width: parent.width * Math.min(root.currentProgress, 1.0)
                    height: parent.height
                    radius: Theme.shapeExtraSmall
                    color: palette.highlight
                    Behavior on width { SmoothedAnimation { duration: 300; velocity: 200 } }

                    Rectangle {
                        width: parent.width * 0.3
                        height: parent.height
                        radius: Theme.shapeNone
                        color: Qt.rgba(1, 1, 1, 0.2)
                        SequentialAnimation on x {
                            loops: Animation.Infinite
                            NumberAnimation {
                                from: -progressBar.width * 0.3
                                to: progressTrack.width + progressBar.width * 0.3
                                duration: 1500
                            }
                            PauseAnimation { duration: 2000 }
                        }
                    }
                }
            }

            // Row 3: File count + percentage
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: Math.round(root.currentProgress * 100) + "%"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: palette.text
                }

                Text {
                    text: root.total > 0
                          ? root.completed + "/" + root.total + " 文件"
                          : ""
                    font.pixelSize: 11
                    color: palette.placeholderText
                    Layout.fillWidth: true
                }

                // Close button
                Rectangle {
                    width: 24; height: 24; radius: Theme.shapeSmall
                    color: closeMa.containsMouse ? Qt.alpha(palette.placeholderText, 0.1) : "transparent"

                    AppIcon { anchors.centerIn: parent; iconName: "xmark"; iconSize: 12 }

                    MouseArea {
                        id: closeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: kernel.downloadManager.cancelAll()
                    }
                }
            }
        }
    }
}
