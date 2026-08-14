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

// Unified download status card (WinUI3 style) covering Minecraft/Java version
// downloads (kernel.downloadManager), mod install tasks (kernel.modManager)
// and modpack installs (kernel.modpackManager). Every source renders through
// DownloadCard so the whole panel shares one visual language.
// Auto-closes when everything is finished.
Rectangle {
    id: root

    readonly property bool dlBusy: kernel.downloadManager.busy
    readonly property var modTasks: kernel.modManager.tasks || []
    readonly property bool packBusy: kernel.modpackManager.busy
    readonly property bool anyVisible: root.dlBusy || root.modTasks.length > 0 || root.packBusy

    height: root.anyVisible ? content.implicitHeight + 32 : 0
    radius: Theme.shapeExtraLarge
    color: Qt.alpha(palette.window, Theme.transparencyOpacity)
    border.color: Qt.alpha(palette.mid, 0.4)
    border.width: 1
    clip: true
    opacity: root.anyVisible ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }
    Behavior on height { NumberAnimation { duration: 250 } }

    // Absorb clicks so they don't reach items behind
    MouseArea {
        anchors.fill: parent
        propagateComposedEvents: false
        onPressed: function(mouse) { mouse.accepted = true }
        onReleased: function(mouse) { mouse.accepted = true }
        onClicked: function(mouse) { mouse.accepted = true }
    }

    // --- mod task helpers ---
    readonly property real modRowHeight: 58
    readonly property real modListHeight: Math.min(4, root.modTasks.length) * root.modRowHeight

    readonly property int activeModCount: {
        var n = 0
        for (var i = 0; i < root.modTasks.length; i++) {
            var s = root.modTasks[i].status
            if (s === "queued" || s === "downloading" || s === "cancelling") n++
        }
        return n
    }

    readonly property bool modsFinished: {
        for (var i = 0; i < root.modTasks.length; i++) {
            var s = root.modTasks[i].status
            if (s === "success" || s === "failed" || s === "cancelled")
                return true
        }
        return false
    }

    // Auto-close mod card shortly after all mod tasks reach a terminal state.
    Timer {
        id: autoCloseTimer
        interval: 1600
        repeat: false
        onTriggered: kernel.modManager.clearFinished()
    }

    function hasActiveTasks() {
        var ts = kernel.modManager.tasks || []
        for (var i = 0; i < ts.length; i++) {
            var s = ts[i].status
            if (s === "queued" || s === "downloading" || s === "cancelling")
                return true
        }
        return false
    }

    function maybeAutoCloseMods() {
        if (kernel.modManager.tasks && kernel.modManager.tasks.length > 0
                && !root.hasActiveTasks()) {
            root.autoCloseTimer.restart()
        } else {
            root.autoCloseTimer.stop()
        }
    }

    Connections {
        target: kernel.modManager
        function onTasksChanged() {
            if (kernel.modManager.busy) {
                root.autoCloseTimer.stop()
            } else {
                root.maybeAutoCloseMods()
            }
        }
    }

    function statusText(status, progress, received, total) {
        if (status === "queued") return "排队中"
        if (status === "downloading") {
            if (total > 0) return "下载中 " + Math.round(progress * 100) + "%"
            return "下载中..."
        }
        if (status === "cancelling") return "取消中..."
        if (status === "success") return "已完成"
        if (status === "failed") return "失败"
        if (status === "cancelled") return "已取消"
        return status
    }

    function statusColor(status) {
        if (status === "failed") return "#F44336"
        if (status === "success") return palette.highlight
        if (status === "downloading") return palette.highlight
        return palette.placeholderText
    }

    function formatSpeed(bytesPerSec) {
        if (bytesPerSec <= 0) return ""
        if (bytesPerSec > 1024 * 1024) return (bytesPerSec / (1024 * 1024)).toFixed(1) + " MB/s"
        if (bytesPerSec > 1024) return (bytesPerSec / 1024).toFixed(0) + " KB/s"
        return bytesPerSec.toFixed(0) + " B/s"
    }

    function formatBytes(bytes) {
        if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        if (bytes >= 1024) return (bytes / 1024).toFixed(0) + " KB"
        return bytes + " B"
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // ================= Minecraft / Java downloads =================
        DownloadCard {
            Layout.fillWidth: true
            visible: root.dlBusy
            title: kernel.downloadManager.currentTask
            subtitle: kernel.downloadManager.subTask
            progress: kernel.downloadManager.progress
            speedText: root.formatSpeed(kernel.downloadManager.speedBytes)
            fileCountText: kernel.downloadManager.totalFiles > 0
                           ? kernel.downloadManager.completedFiles + "/" + kernel.downloadManager.totalFiles + " 文件"
                           : ""
            statusText: Math.round(kernel.downloadManager.progress * 100) + "%"
            statusColor: palette.highlight
            onCancelRequested: kernel.downloadManager.cancelAll()
        }

        // ================= separator =================
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: palette.mid
            opacity: 0.3
            visible: (root.dlBusy && (root.modTasks.length > 0 || root.packBusy))
                     || (root.modTasks.length > 0 && root.packBusy)
        }

        // ================= Mod install tasks =================
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: root.modTasks.length > 0

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                AppIcon {
                    iconName: "download"
                    iconSize: 14
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: root.activeModCount > 0 ? "模组下载  " + root.activeModCount + " 个任务" : "模组下载"
                    font.pixelSize: 12; font.weight: Font.DemiBold
                    color: palette.text; elide: Text.ElideRight
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: kernel.modManager.busy ? "下载中..." : (root.modsFinished ? "已完成" : "")
                    font.pixelSize: 11
                    color: kernel.modManager.busy ? palette.highlight : palette.placeholderText
                    visible: text !== ""
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            ListView {
                id: taskListView
                Layout.fillWidth: true
                Layout.preferredHeight: root.modListHeight
                implicitHeight: root.modListHeight
                clip: true
                model: root.modTasks
                spacing: 2
                interactive: root.modTasks.length > 4
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: 6
                }

                delegate: DownloadCard {
                    width: taskListView.width
                    dense: true
                    title: modelData.name || modelData.fileName || ""
                    subtitle: (modelData.status === "downloading" && (modelData.total || 0) > 0)
                              ? root.formatBytes(modelData.received || 0) + " / " + root.formatBytes(modelData.total)
                              : ""
                    progress: modelData.progress || 0
                    indeterminate: false
                    speedText: root.formatSpeed(modelData.speedBytes || 0)
                    statusText: root.statusText(modelData.status, modelData.progress, modelData.received, modelData.total)
                    statusColor: root.statusColor(modelData.status)
                    showCancel: modelData.status === "queued" || modelData.status === "downloading"
                                || modelData.status === "cancelling"
                    showRetry: modelData.status === "failed" || modelData.status === "cancelled"
                    onCancelRequested: kernel.modManager.cancelTask(index)
                    onRetryRequested: kernel.modManager.retryTask(index)
                }
            }
        }

        // ================= Modpack install =================
        DownloadCard {
            Layout.fillWidth: true
            visible: root.packBusy
            title: "整合包安装"
            subtitle: kernel.modpackManager.status
            progress: kernel.modpackManager.progress
            statusText: Math.round(kernel.modpackManager.progress * 100) + "%"
            statusColor: palette.highlight
            onCancelRequested: kernel.modpackManager.cancelAll()
        }
    }
}
