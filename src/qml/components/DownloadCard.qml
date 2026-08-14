/*
 * Beacon - a cross-platform Minecraft launcher.
 *
 * Copyright (C) 2024-2026 fuqicn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Unified download task card. Every download source in the launcher renders
// through this component (Minecraft/Java version installs, mod installs,
// modpack installs) so they share one visual language and behavior.
//
// Public API:
//   title         - main line text
//   subtitle      - secondary line (hidden in dense mode)
//   progress      - 0..1 completion; ignored when indeterminate
//   indeterminate - true => shimmer bar instead of proportional fill
//   statusText    - trailing status label (排队中 / 45% / 已完成 ...)
//   statusColor   - color of the status label
//   speedText     - trailing speed label
//   fileCountText - "3/58 文件" line in the footer (hidden in dense mode)
//   iconName      - AppIcon glyph
//   showCancel / showRetry - footer actions (hidden in dense mode)
//   dense         - compact single-row variant for list delegates
//   signals: cancelRequested(), retryRequested()
Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property real progress: 0.0
    property bool indeterminate: false
    property string statusText: ""
    property color statusColor: palette.placeholderText
    property string speedText: ""
    property string fileCountText: ""
    property string iconName: "download"
    property bool showCancel: true
    property bool showRetry: false
    property bool dense: false

    signal cancelRequested()
    signal retryRequested()

    radius: Theme.shapeSmall
    color: "transparent"

    implicitHeight: root.dense ? 56 : contentCol.implicitHeight + 20

    readonly property real fill: root.indeterminate ? 1.0 : Math.min(root.progress, 1.0)

    MouseArea {
        anchors.fill: parent
        propagateComposedEvents: false
        onPressed: function(mouse) { mouse.accepted = true }
        onReleased: function(mouse) { mouse.accepted = true }
        onClicked: function(mouse) { mouse.accepted = true }
    }

    ColumnLayout {
        id: contentCol
        anchors.fill: parent
        anchors.margins: root.dense ? 0 : 10
        anchors.leftMargin: root.dense ? 4 : 10
        anchors.rightMargin: root.dense ? 4 : 10
        spacing: root.dense ? 0 : 6

        // Header: icon + title/subtitle + status/speed
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: root.dense ? 24 : 32
                height: root.dense ? 24 : 32
                radius: Theme.shapeSmall
                color: Qt.alpha(palette.highlight, 0.15)

                AppIcon {
                    anchors.centerIn: parent
                    iconName: root.iconName
                    iconSize: root.dense ? 12 : 16
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: root.title
                    font.pixelSize: root.dense ? 13 : 13
                    font.weight: Font.Medium
                    color: palette.text
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: root.subtitle
                    font.pixelSize: 11
                    color: palette.placeholderText
                    elide: Text.ElideRight
                    visible: text !== ""
                }
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: root.speedText
                font.pixelSize: root.dense ? 10 : 12
                font.weight: Font.DemiBold
                color: palette.highlight
                font.italic: true
                visible: text !== ""
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: root.statusText
                font.pixelSize: 11
                font.weight: Font.DemiBold
                color: root.statusColor
                visible: text !== ""
            }
        }

        // Progress bar
        Rectangle {
            id: progressTrack
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            height: root.dense ? 4 : 6
            radius: Theme.shapeExtraSmall
            color: Qt.alpha(palette.highlight, 0.12)
            clip: true

            Rectangle {
                id: progressBar
                width: progressTrack.width * root.fill
                height: progressTrack.height
                radius: Theme.shapeExtraSmall
                color: palette.highlight
                Behavior on width { SmoothedAnimation { duration: 300; velocity: 200 } }

                // Indeterminate shimmer
                Rectangle {
                    width: progressTrack.width * 0.3
                    height: progressTrack.height
                    radius: Theme.shapeNone
                    color: Qt.rgba(1, 1, 1, 0.2)
                    visible: root.indeterminate
                    SequentialAnimation on x {
                        running: root.indeterminate
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

        // Footer: percentage + file count + actions
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: !root.dense

            Text {
                text: Math.round(root.progress * 100) + "%"
                font.pixelSize: 11
                font.weight: Font.DemiBold
                color: palette.text
            }

            Text {
                text: root.fileCountText
                font.pixelSize: 11
                color: palette.placeholderText
                visible: text !== ""
            }

            // Always-present spacer so the retry/cancel buttons sit flush right
            // even when there is no fileCountText (e.g. the modpack card).
            Item { Layout.fillWidth: true }

            Rectangle {
                width: 24; height: 24; radius: Theme.shapeSmall
                color: retryMa.containsMouse ? Qt.alpha(palette.placeholderText, 0.1) : "transparent"
                visible: root.showRetry

                AppIcon { anchors.centerIn: parent; iconName: "refresh"; iconSize: 12 }

                MouseArea {
                    id: retryMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.retryRequested()
                }
            }

            Rectangle {
                width: 24; height: 24; radius: Theme.shapeSmall
                color: cancelMa.containsMouse ? Qt.alpha(palette.placeholderText, 0.1) : "transparent"
                visible: root.showCancel

                AppIcon { anchors.centerIn: parent; iconName: "xmark"; iconSize: 12 }

                MouseArea {
                    id: cancelMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.cancelRequested()
                }
            }
        }
    }
}
