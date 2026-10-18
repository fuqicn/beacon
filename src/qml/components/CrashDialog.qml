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

// Automatic crash dialog. When a launched game exits abnormally,
// KernelBridge::analyzeGameResult() emits `gameCrashed` with a structured,
// parsed diagnosis; main.qml forwards it here via openWith().
Popup {
    id: root

    // Structured diagnosis from the C++ crash analyzer.
    property var info: ({})

    // Ask the host to switch to the full log viewer.
    signal viewLogRequested()

    function openWith(i) {
        root.info = i || ({})
        root.copied = false
        root.open()
    }

    function catKey() {
        var c = (root.info && root.info.category) ? root.info.category : "unknown"
        switch (c) {
        case "out_of_memory": return "outOfMemory"
        case "java_version":  return "javaVersion"
        case "main_class":    return "mainClass"
        case "graphics":      return "graphics"
        case "native":        return "native"
        case "mod_issue":     return "modIssue"
        case "network":       return "network"
        case "launch_failed": return "launchFailed"
        default:              return "unknown"
        }
    }
    function catTitle()  { return I18n.tr("crash.cat." + catKey() + ".title") }
    function catDetail() { return I18n.tr("crash.cat." + catKey() + ".detail") }

    function metaChips() {
        var i = root.info || ({})
        var c = []
        if (i.minecraftVersion) c.push(String(i.minecraftVersion))
        if (i.modLoader) c.push(String(i.modLoader))
        if (i.exitCode !== undefined && i.exitCode !== null)
            c.push(I18n.tr("crash.chip.exit") + " " + i.exitCode)
        return c
    }

    function detailRows() {
        var i = root.info || ({})
        var rows = []
        if (i.exception)
            rows.push({ label: I18n.tr("crash.row.exception"), value: String(i.exception) })
        if (i.causedBy)
            rows.push({ label: I18n.tr("crash.row.causedBy"), value: String(i.causedBy) })
        if (i.suspectedMod)
            rows.push({ label: I18n.tr("crash.row.suspectedMod"), value: String(i.suspectedMod) })
        if (i.description)
            rows.push({ label: I18n.tr("crash.row.description"), value: String(i.description) })
        if (i.minecraftVersion)
            rows.push({ label: I18n.tr("crash.row.version"), value: String(i.minecraftVersion) })
        if (i.modLoader)
            rows.push({ label: I18n.tr("crash.row.loader"), value: String(i.modLoader) })
        if (i.reportPath)
            rows.push({ label: I18n.tr("crash.row.report"), value: String(i.reportPath) })
        return rows
    }

    function buildText() {
        var lines = [I18n.tr("crash.title"), root.catTitle(), "", root.catDetail(), ""]
        var rows = root.detailRows()
        for (var k = 0; k < rows.length; ++k)
            lines.push(rows[k].label + ": " + rows[k].value)
        return lines.join("\n")
    }

    property bool copied: false

    function copyAll() {
        clip.text = root.buildText()
        clip.selectAll()
        clip.copy()
        root.copied = true
        copiedTimer.restart()
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width - 64, 540)
    height: contentColumn.implicitHeight + padding * 2
    padding: 24

    // Offscreen clipboard helper (TextEdit is the only QML type that can
    // push text to the system clipboard directly).
    TextEdit { id: clip; visible: false }

    Timer { id: copiedTimer; interval: 1600; onTriggered: root.copied = false }

    background: Rectangle {
        radius: Theme.shapeLarge
        color: palette.window
        border.color: palette.mid
        border.width: 1
    }

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Rectangle {
                width: 42; height: 42; radius: 21
                color: Qt.alpha("#F44336", 0.14)
                Text {
                    anchors.centerIn: parent
                    text: "\u26A0"
                    font.pixelSize: 22
                    color: "#F44336"
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: I18n.tr("crash.title")
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    color: palette.text
                    Layout.fillWidth: true
                }
                Text {
                    text: root.catTitle()
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#F44336"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        Text {
            text: root.catDetail()
            font.pixelSize: 12
            color: palette.text
            wrapMode: Text.Wrap
            lineHeight: 1.35
            Layout.fillWidth: true
        }

        // Version / loader / exit-code chips
        Flow {
            Layout.fillWidth: true
            spacing: 8
            visible: root.metaChips().length > 0

            Repeater {
                model: root.metaChips()
                delegate: Rectangle {
                    height: 22
                    width: chipLabel.implicitWidth + 16
                    radius: 11
                    color: Theme.surfaceContainer
                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 11
                        color: palette.placeholderText
                    }
                }
            }
        }

        // Technical details (scrollable when long)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(detailColumn.implicitHeight + 20, 190)
            visible: detailColumn.implicitHeight > 0
            radius: Theme.shapeMedium
            color: Theme.surfaceContainer
            clip: true

            Flickable {
                anchors.fill: parent
                anchors.margins: 10
                contentWidth: width
                contentHeight: detailColumn.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: detailColumn
                    width: parent.width
                    spacing: 8

                    Repeater {
                        model: root.detailRows()
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: modelData.label
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                color: palette.placeholderText
                            }
                            Text {
                                text: modelData.value
                                font.pixelSize: 11
                                font.family: "Consolas"
                                color: palette.text
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: I18n.tr("crash.viewLog")
                font.weight: Font.Normal
                onClicked: {
                    root.close()
                    root.viewLogRequested()
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: root.copied ? I18n.tr("crash.copied") : I18n.tr("crash.copy")
                font.weight: Font.Normal
                onClicked: root.copyAll()
            }

            Button {
                text: I18n.tr("crash.report")
                font.weight: Font.Normal
                visible: {
                    var i = root.info || ({})
                    return i.reportPath !== undefined && String(i.reportPath).length > 0
                }
                onClicked: {
                    var i = root.info || ({})
                    if (i.reportPath)
                        kernel.revealInExplorer(String(i.reportPath))
                }
            }

            Button {
                text: I18n.tr("crash.close")
                font.weight: Font.Normal
                highlighted: true
                onClicked: root.close()
            }
        }
    }
}
