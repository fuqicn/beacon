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
import "../components"

Item {
    id: root

    property string gameDir: kernel.launchManager.gameDir
    property string logPath: (gameDir ? gameDir + "/game_output.log" : "")
    property string cmdPath: (gameDir ? gameDir + "/mclaunch_cmdline.txt" : "")

    function currentLog() {
        return kernel.readFileTail(logPath, 400)
    }

    function currentCmdline() {
        var c = kernel.readFileTail(cmdPath, 200)
        return c
    }

    function refresh() {
        logTextArea.text = currentLog()
        if (logTextArea.text.length === 0)
            logTextArea.text = I18n.tr("log.empty")
        diagModel.clear()
        var issues = kernel.diagnoseCrash(logTextArea.text)
        for (var i = 0; i < issues.length; ++i)
            diagModel.append(issues[i])
        logScroll.contentItem.contentY = logScroll.contentItem.contentHeight
    }

    ListModel { id: diagModel }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: "\u2190  " + I18n.tr("back")
                flat: true
                font.weight: Font.Normal
                onClicked: window.navigateTo(3)
            }

            Text {
                text: I18n.tr("log.title")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
                Layout.fillWidth: true
            }

            Switch {
                id: autoSwitch
                text: I18n.tr("log.auto")
                checked: true
            }

            Button {
                text: I18n.tr("log.refresh")
                font.weight: Font.Normal
                highlighted: true
                onClicked: root.refresh()
            }
        }

        Timer {
            interval: 3000
            running: autoSwitch.checked
            repeat: true
            onTriggered: root.refresh()
        }

        Text {
            text: logPath
            font.pixelSize: 12
            color: palette.placeholderText
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }

        Text {
            text: I18n.tr("log.diagnosis")
            font.pixelSize: 16
            font.weight: Font.Medium
            color: palette.text
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(160, contentHeight)
            clip: true
            spacing: 6
            model: diagModel

            delegate: Rectangle {
                width: ListView.view.width
                height: diagTitle.implicitHeight + diagDetail.implicitHeight + 16
                radius: Theme.shapeMedium
                color: {
                    if (modelData.level === "error") return Qt.alpha("#F44336", 0.12)
                    if (modelData.level === "warning") return Qt.alpha("#FFA050", 0.12)
                    return Qt.alpha("#4CAF50", 0.12)
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 2

                    Text {
                        id: diagTitle
                        text: (modelData.level === "error" ? "\u26a0 " : modelData.level === "warning" ? "! " : "\u2713 ") + modelData.title
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: {
                            if (modelData.level === "error") return "#F44336"
                            if (modelData.level === "warning") return "#E8912D"
                            return "#4CAF50"
                        }
                    }

                    Text {
                        id: diagDetail
                        text: modelData.detail
                        font.pixelSize: 12
                        color: palette.placeholderText
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        lineHeight: 1.3
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: I18n.tr("log.path")
                font.pixelSize: 16
                font.weight: Font.Medium
                color: palette.text
            }
            Item { Layout.fillWidth: true }
            Text {
                text: I18n.tr("log.desc")
                font.pixelSize: 11
                color: palette.placeholderText
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.shapeMedium
            color: Theme.surfaceContainer
            clip: true

            ScrollView {
                id: logScroll
                anchors.fill: parent
                anchors.margins: 4
                clip: true

                TextArea {
                    id: logTextArea
                    text: "(暂无日志内容)"
                    readOnly: true
                    selectByMouse: true
                    font.family: "Consolas"
                    font.pixelSize: 12
                    color: palette.text
                    background: null
                    wrapMode: TextEdit.NoWrap
                    textFormat: TextEdit.PlainText
                }

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            }
        }

        Component.onCompleted: root.refresh()
    }
}
