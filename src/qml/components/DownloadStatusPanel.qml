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
    readonly property bool installBusy: kernel.installManager.busy
    // Loader installs that need a JVM (Forge) resolve/download Java before the
    // worker starts. During that phase the install progress is still 0, so show
    // the Java download's own progress instead of a frozen indeterminate bar.
    readonly property bool javaPrepBusy: root.installBusy
                                         && kernel.installManager.status === "preparingJava"
                                         && kernel.javaManager.downloadBusy
    // Last loader-install error, kept visible for a while after the install
    // worker stops so a failure is not silently swallowed (the install dialog
    // closes as soon as the install starts).
    property string installError: ""
    readonly property bool updBusy: kernel.updateDownloading
    // 搜索任务（资源包/光影包/数据包）
    readonly property var searchTasks: kernel.searchManager.tasks || []
    readonly property bool searchBusy: {
        for (var i = 0; i < searchTasks.length; i++) {
            var s = searchTasks[i].status
            if (s === "queued" || s === "downloading" || s === "cancelling") return true
        }
        return false
    }
    readonly property bool searchFinished: {
        for (var i = 0; i < searchTasks.length; i++) {
            var s = searchTasks[i].status
            if (s === "success" || s === "failed" || s === "cancelled")
                return true
        }
        return false
    }
    readonly property bool anyVisible: root.dlBusy || root.modTasks.length > 0 || root.packBusy || root.installBusy || root.updBusy || root.searchBusy || root.installError !== ""

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

    // --- task list helpers ---
    // Cards render in their full (non-dense) form so the footer actions — the
    // ✕ cancel / ↻ retry icons — stay visible. The lists therefore size
    // themselves to the delegates' real height (the old fixed 58/36 row
    // heights clipped the footer, which is why the ✕ never showed up).
    readonly property real maxListHeight: 300
    readonly property real modListHeight: Math.min(taskListView.contentHeight, root.maxListHeight)
    readonly property real searchListHeight: Math.min(searchTaskListView.contentHeight, root.maxListHeight)

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
            if (!kernel.modManager.busy && kernel.modManager.tasks && kernel.modManager.tasks.length > 0) {
                // 下载完成时立即清除已完成的任务
                kernel.modManager.clearFinished()
            }
        }
    }

    // Auto-close search card shortly after all search tasks reach a terminal state.
    Timer {
        id: searchAutoCloseTimer
        interval: 1600
        repeat: false
        onTriggered: kernel.searchManager.clearFinished()
    }

    function hasActiveSearchTasks() {
        var ts = kernel.searchManager.tasks || []
        for (var i = 0; i < ts.length; i++) {
            var s = ts[i].status
            if (s === "queued" || s === "downloading" || s === "cancelling")
                return true
        }
        return false
    }

    function maybeAutoCloseSearch() {
        if (kernel.searchManager.tasks && kernel.searchManager.tasks.length > 0
                && !root.hasActiveSearchTasks()) {
            root.searchAutoCloseTimer.restart()
        } else {
            root.searchAutoCloseTimer.stop()
        }
    }

    Connections {
        target: kernel.searchManager
        function onTasksChanged() {
            if (kernel.searchManager.busy) {
                root.searchAutoCloseTimer.stop()
            } else {
                // 下载完成时立即清除已完成的任务
                kernel.searchManager.clearFinished()
            }
        }
    }

    // Loader install failures: hide the error card automatically after a while.
    Timer {
        id: installErrorTimer
        interval: 12000
        repeat: false
        onTriggered: root.installError = ""
    }

    Connections {
        target: kernel.installManager
        function onBusyChanged() {
            if (kernel.installManager.busy) {
                root.installError = ""
                installErrorTimer.stop()
            }
        }
        function onErrorOccurred(message) {
            root.installError = message
            installErrorTimer.restart()
        }
    }

    function statusText(status, progress, received, total) {
        if (status === "queued") return I18n.tr("status.queued")
        if (status === "downloading") {
            if (total > 0) return I18n.tr("status.downloading").replace("%1", String(Math.round(progress * 100)))
            return I18n.tr("status.downloadingNoCount")
        }
        if (status === "cancelling") return I18n.tr("status.cancelling")
        if (status === "success") return I18n.tr("status.success")
        if (status === "failed") return I18n.tr("status.failed")
        if (status === "cancelled") return I18n.tr("status.cancelled")
        return status
    }

    function statusColor(status) {
        if (status === "failed") return "#F44336"
        if (status === "success") return Theme.primary
        if (status === "downloading") return Theme.primary
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
                           ? I18n.tr("downloadCard.fileCount").replace("%1", String(kernel.downloadManager.completedFiles)).replace("%2", String(kernel.downloadManager.totalFiles))
                           : ""
            statusText: Math.round(kernel.downloadManager.progress * 100) + "%"
            statusColor: Theme.primary
            onCancelRequested: kernel.downloadManager.cancelAll()
        }

        // ================= Update download =================
        DownloadCard {
            Layout.fillWidth: true
            visible: root.updBusy
            title: I18n.tr("status.updateDownload").replace("%1", kernel.latestVersion)
            subtitle: kernel.updateTotal > 0
                      ? root.formatBytes(kernel.updateReceived) + " / " + root.formatBytes(kernel.updateTotal)
                      : ""
            progress: kernel.updateProgress
            indeterminate: kernel.updateTotal <= 0
            speedText: root.formatSpeed(kernel.updateSpeedBytes)
            statusText: kernel.updateTotal > 0 ? Math.round(kernel.updateProgress * 100) + "%" : ""
            statusColor: Theme.primary
            onCancelRequested: kernel.cancelUpdateDownload()
        }

        // ================= separator =================
        Rectangle {
            id: sectionSeparator
            Layout.fillWidth: true
            height: 1
            color: palette.mid
            opacity: 0.3
            readonly property int lowerCount: (root.searchTasks.length > 0 ? 1 : 0)
                                              + (root.modTasks.length > 0 ? 1 : 0)
                                              + (root.packBusy ? 1 : 0)
                                              + (root.installBusy ? 1 : 0)
            visible: ((root.dlBusy || root.updBusy) && sectionSeparator.lowerCount > 0)
                     || sectionSeparator.lowerCount > 1
        }

        // ================= Search tasks (resource/shader/datapack) =================
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: root.searchTasks.length > 0

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
                    text: I18n.tr("searchResource.installTitle")
                    font.pixelSize: 12; font.weight: Font.DemiBold
                    color: palette.text; elide: Text.ElideRight
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            ListView {
                id: searchTaskListView
                Layout.fillWidth: true
                Layout.preferredHeight: root.searchListHeight
                implicitHeight: root.searchListHeight
                clip: true
                model: root.searchTasks
                spacing: 2
                interactive: root.searchTasks.length > 4
                ScrollBar.vertical: OverlayScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: 6
                }

                delegate: DownloadCard {
                    width: searchTaskListView.width
                    dense: false
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
                    onCancelRequested: kernel.searchManager.cancelTask(index)
                    onRetryRequested: kernel.searchManager.retryTask(index)
                }
            }
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
                    text: root.activeModCount > 0 ? I18n.tr("status.modDownloadCount").replace("%1", String(root.activeModCount)) : I18n.tr("status.modDownload")
                    font.pixelSize: 12; font.weight: Font.DemiBold
                    color: palette.text; elide: Text.ElideRight
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: kernel.modManager.busy ? I18n.tr("status.modDownloading") : (root.modsFinished ? I18n.tr("status.modDone") : "")
                    font.pixelSize: 11
                    color: kernel.modManager.busy ? Theme.primary : palette.placeholderText
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
                ScrollBar.vertical: OverlayScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: 6
                }

                delegate: DownloadCard {
                    width: taskListView.width
                    dense: false
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
            title: I18n.tr("status.modpackInstall")
            subtitle: kernel.modpackManager.status
            progress: kernel.modpackManager.progress
            statusText: Math.round(kernel.modpackManager.progress * 100) + "%"
            statusColor: Theme.primary
            onCancelRequested: kernel.modpackManager.cancelAll()
        }

        // ================= Loader install =================
        DownloadCard {
            Layout.fillWidth: true
            visible: root.installBusy || root.installError !== ""
            title: I18n.tr("status.loaderInstall")
            subtitle: !root.installBusy
                      ? root.installError
                      : (kernel.installManager.status === "preparingJava"
                         ? I18n.tr("install.preparingJava")
                         : kernel.installManager.status)
            progress: !root.installBusy
                      ? 0
                      : (root.javaPrepBusy
                         ? kernel.javaManager.downloadProgress
                         : kernel.installManager.progress)
            indeterminate: root.installBusy && !root.javaPrepBusy
                           && kernel.installManager.progress <= 0.01
            statusText: !root.installBusy
                        ? I18n.tr("status.failed")
                        : Math.round((root.javaPrepBusy
                                      ? kernel.javaManager.downloadProgress
                                      : kernel.installManager.progress) * 100) + "%"
            statusColor: root.installBusy ? Theme.primary : "#F44336"
            onCancelRequested: {
                if (root.installBusy) {
                    kernel.installManager.cancelAll()
                } else {
                    installErrorTimer.stop()
                    root.installError = ""
                }
            }
        }
    }
}
