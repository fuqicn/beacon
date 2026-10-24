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

// "What's new" dialog, shown on the first launch after an update. The pending
// state comes from KernelBridge::showChangelog (version comparison in
// initialize()); closing the dialog persists the acknowledged version so it
// never shows twice for the same release.
Popup {
    id: root

    // Version that triggered this changelog (shown in the subtitle).
    property string version: ""

    // Height budget for the scrollable notes. Derived from the window (the
    // popup's parent), never from the popup itself: a binding that depends on
    // the popup height would form a layout loop and let the dialog resize (and
    // reposition) itself while scrolling.
    readonly property real bodyMaxHeight: {
        var h = (root.parent && root.parent.height > 0) ? root.parent.height : 640
        return Math.max(140, h - 250)
    }

    // Release notes, grouped into themed sections. Keep the keys in sync with
    // changelog.section.* in the i18n files.
    readonly property var sections: [
        {
            title: I18n.tr("changelog.section.resources.title"),
            items: [
                I18n.tr("changelog.section.resources.item1"),
                I18n.tr("changelog.section.resources.item2"),
                I18n.tr("changelog.section.resources.item3")
            ]
        },
        {
            title: I18n.tr("changelog.section.download.title"),
            items: [
                I18n.tr("changelog.section.download.item1"),
                I18n.tr("changelog.section.download.item2"),
                I18n.tr("changelog.section.download.item3")
            ]
        },
        {
            title: I18n.tr("changelog.section.crash.title"),
            items: [
                I18n.tr("changelog.section.crash.item1"),
                I18n.tr("changelog.section.crash.item2"),
                I18n.tr("changelog.section.crash.item3")
            ]
        },
        {
            title: I18n.tr("changelog.section.ui.title"),
            items: [
                I18n.tr("changelog.section.ui.item1"),
                I18n.tr("changelog.section.ui.item2"),
                I18n.tr("changelog.section.ui.item3")
            ]
        }
    ]

    // The notes must be acknowledged with the "Got it" button; clicks outside
    // and the Escape key do not close the dialog (and do not mark it as read).
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    x: Math.max(16, Math.round((parent.width - width) / 2))
    y: Math.max(16, Math.round((parent.height - height) / 2))
    width: Math.min(parent.width - 64, 560)
    height: Math.min(contentColumn.implicitHeight + padding * 2,
                     Math.max(260, parent.height - 32))
    padding: 24

    background: Rectangle {
        radius: Theme.shapeLarge
        color: palette.window
        border.color: palette.mid
        border.width: 1
    }

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 16

        // Header: icon badge, title and version line.
        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Rectangle {
                width: 42; height: 42; radius: 21
                color: Qt.alpha(Theme.primary, 0.14)

                AppIcon {
                    anchors.centerIn: parent
                    iconName: "file-lines"
                    iconSize: 22
                    tint: Theme.primary
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: I18n.tr("changelog.title")
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    color: palette.text
                    Layout.fillWidth: true
                }
                Text {
                    text: I18n.tr("changelog.subtitle").arg(root.version)
                    font.pixelSize: 12
                    color: palette.placeholderText
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        // Scrollable release notes. The preferred height is capped by the
        // window, so long notes scroll instead of pushing the dialog offscreen.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(bodyColumn.implicitHeight, root.bodyMaxHeight)
            color: "transparent"
            clip: true

            Flickable {
                id: bodyFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: bodyColumn.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: OverlayScrollBar { width: 8 }

                ColumnLayout {
                    id: bodyColumn
                    width: bodyFlick.width
                    spacing: 16

                    Repeater {
                        model: root.sections
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 7

                            Text {
                                text: modelData.title
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: Theme.primary
                                Layout.fillWidth: true
                            }

                            Repeater {
                                model: modelData.items
                                delegate: RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Rectangle {
                                        width: 5; height: 5; radius: 2.5
                                        color: Theme.primary
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 6
                                    }

                                    Text {
                                        text: modelData
                                        font.pixelSize: 12
                                        color: palette.text
                                        wrapMode: Text.Wrap
                                        lineHeight: 1.3
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Footer: single acknowledgement button.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: I18n.tr("changelog.gotIt")
                font.weight: Font.Normal
                highlighted: true
                onClicked: {
                    // Persist first, then close: window shutdown never marks
                    // the changelog as read on its own.
                    kernel.dismissChangelog()
                    root.close()
                }
            }
        }
    }
}
