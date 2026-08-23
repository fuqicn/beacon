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
import QtQuick.Dialogs
import "../components"

Item {
    id: root

    property string instanceId: ""
    property var instanceData: ({})
    property var installedMods: []
    property string gameDir: ""
    readonly property bool isWindows: (Qt.platform.os === "windows")

    function formatBytes(n) {
        if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB"
        if (n >= 1024) return (n / 1024).toFixed(1) + " KB"
        return n + " B"
    }

    function readInstanceSetting(key, def) {
        if (!instanceId) return def
        kernel.settingsManager.setInstance(instanceId)
        var v = kernel.settingsManager.value(key, def)
        kernel.settingsManager.endInstance()
        return v
    }

    function writeInstanceSetting(key, val) {
        if (!instanceId) return
        kernel.settingsManager.setInstance(instanceId)
        kernel.settingsManager.setValue(key, val)
        kernel.settingsManager.endInstance()
    }

    function reloadMods() {
        if (gameDir)
            installedMods = kernel.modManager.listInstalledMods(gameDir)
        else
            installedMods = []
    }

    function loadInstance() {
        var sel = kernel.instanceManager.getSelectedInstance()
        if (sel && sel.id) {
            instanceId = sel.id
            instanceData = sel
            gameDir = kernel.gameDirFor(sel.rootDir, sel.id)
            reloadMods()
            updateIsolationUI()
        }
    }

    function updateIsolationUI() {
        if (!instanceId) return
        var hasOverride = root.readInstanceSetting("launch/isolationOverride", false)
        var eff = kernel.instanceIsolationEffective(root.instanceData.rootDir, instanceId)
        if (hasOverride)
            isoCombo.currentIndex = root.readInstanceSetting("launch/isolation", false) ? 1 : 2
        else
            isoCombo.currentIndex = 0
        isoEffText.text = eff ? I18n.tr("instances.isolationEffOn") : I18n.tr("instances.isolationEffOff")
        isoDirText.text = eff && gameDir ? gameDir : ""
    }

    Component.onCompleted: loadInstance()
    Connections {
        target: kernel.instanceManager
        function onSelectedChanged() { loadInstance() }
    }
    Connections {
        target: kernel.modManager
        function onInstallCompleted(success, path) {
            if (success)
                root.reloadMods()
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: contentColumn.implicitHeight + 48
        clip: true
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 20

            // Back button + instance name
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    text: "\u2190  " + I18n.tr("back")
                    flat: true
                    font.weight: Font.Normal
                    onClicked: window.navigateToPage(4, I18n.tr("launch.manageInstances"))
                }

                Text {
                    text: instanceData.id || I18n.tr("instance.notSelected")
                    font.pixelSize: 20
                    font.weight: Font.Medium
                    color: palette.text
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
                opacity: 0.3
            }

            // Version isolation (per-instance override)
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: isoInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: isoHover.hovered ? Qt.alpha(Theme.primary, 0.08) : Theme.surfaceContainer
                HoverHandler { id: isoHover }

                ColumnLayout {
                    id: isoInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: I18n.tr("instances.isolation")
                                font.pixelSize: 14
                                color: palette.text
                            }
                            Text {
                                text: I18n.tr("instances.isolationDesc")
                                font.pixelSize: 11
                                color: palette.placeholderText
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                lineHeight: 1.35
                            }
                        }

                        ComboBox {
                            id: isoCombo
                            model: [
                                { text: I18n.tr("instances.isolationFollow"), value: -1 },
                                { text: I18n.tr("instances.isolationOn"), value: 1 },
                                { text: I18n.tr("instances.isolationOff"), value: 0 }
                            ]
                            textRole: "text"
                            valueRole: "value"
                            Layout.preferredWidth: 150
                            onActivated: {
                                var v = currentValue
                                var override = (v !== -1)
                                root.writeInstanceSetting("launch/isolationOverride", override)
                                root.writeInstanceSetting("launch/isolation", v === 1)
                                kernel.setInstanceIsolation(root.instanceId, override, v === 1)
                                root.gameDir = kernel.gameDirFor(root.instanceData.rootDir, root.instanceId)
                                root.reloadMods()
                                root.updateIsolationUI()
                            }
                        }
                    }

                    Text {
                        id: isoEffText
                        font.pixelSize: 11
                        color: Theme.primary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        id: isoDirText
                        font.pixelSize: 11
                        color: palette.placeholderText
                        wrapMode: Text.ElideLeft
                        Layout.fillWidth: true
                        visible: text !== ""
                    }
                }
            }

            // JVM arguments
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: jvmCol.implicitHeight + 32
                radius: Theme.shapeMedium
                color: jvmHover.hovered ? Qt.alpha(Theme.primary, 0.08) : Theme.surfaceContainer
                HoverHandler { id: jvmHover }

                ColumnLayout {
                    id: jvmCol
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            spacing: 2
                            Text {
                                text: I18n.tr("instance.jvmArgs")
                                font.pixelSize: 14
                                color: palette.text
                            }
                            Text {
                                text: I18n.tr("instance.jvmArgsDesc")
                                font.pixelSize: 11
                                color: palette.placeholderText
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Switch {
                            id: jvmSwitch
                            checked: root.readInstanceSetting("launch/jvmArgsEnabled", false)
                            onToggled: root.writeInstanceSetting("launch/jvmArgsEnabled", checked)
                        }
                    }

                    TextField {
                        id: jvmField
                        Layout.fillWidth: true
                        visible: jvmSwitch.checked
                        text: root.readInstanceSetting("launch/jvmArgs", "")
                        placeholderText: "-Xmx2G -XX:+UseG1GC"
                        font.family: "Consolas, monospace"
                        font.pixelSize: 13
                        onEditingFinished: root.writeInstanceSetting("launch/jvmArgs", text)
                    }
                }
            }

            // Memory
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: memInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: memHover.hovered ? Qt.alpha(Theme.primary, 0.08) : Theme.surfaceContainer
                HoverHandler { id: memHover }

                RowLayout {
                    id: memInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    ColumnLayout {
                        spacing: 2
                        Text {
                            text: I18n.tr("instance.memory")
                            font.pixelSize: 14
                            color: palette.text
                        }
                        Text {
                            text: I18n.tr("instance.memoryDesc")
                            font.pixelSize: 11
                            color: palette.placeholderText
                        }
                    }

                    Item { Layout.fillWidth: true }

                    SpinBox {
                        id: memSpin
                        from: 0; to: 32768; stepSize: 256
                        editable: true
                        value: root.readInstanceSetting("launch/memory", 0)
                        onValueModified: root.writeInstanceSetting("launch/memory", value)
                        textFromValue: function(v, locale) { return v === 0 ? I18n.tr("instance.default") : v + " MB" }
                        valueFromText: function(t, locale) {
                            var n = parseInt(t)
                            return isNaN(n) ? 0 : n
                        }
                    }
                }
            }

            // Resolution + fullscreen
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: resInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: resHover.hovered ? Qt.alpha(Theme.primary, 0.08) : Theme.surfaceContainer
                HoverHandler { id: resHover }

                ColumnLayout {
                    id: resInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            spacing: 2
                            Text {
                                text: I18n.tr("instance.resolution")
                                font.pixelSize: 14
                                color: palette.text
                            }
                            Text {
                                text: I18n.tr("instance.resolutionDesc")
                                font.pixelSize: 11
                                color: palette.placeholderText
                            }
                        }

                        Item { Layout.fillWidth: true }

                        SpinBox {
                            id: resWSpin
                            from: 0; to: 7680; stepSize: 16
                            editable: true
                            value: root.readInstanceSetting("launch/resolutionW", 0)
                            onValueModified: root.writeInstanceSetting("launch/resolutionW", value)
                            textFromValue: function(v, locale) { if (v === 0) return I18n.tr("instance.default"); return I18n.tr("instance.resWidthLabel") + ":" + String(v) }
                            valueFromText: function(t, locale) { var n = parseInt(t); return isNaN(n) ? 0 : n }
                        }

                        SpinBox {
                            id: resHSpin
                            from: 0; to: 4320; stepSize: 16
                            editable: true
                            value: root.readInstanceSetting("launch/resolutionH", 0)
                            onValueModified: root.writeInstanceSetting("launch/resolutionH", value)
                            textFromValue: function(v, locale) { if (v === 0) return I18n.tr("instance.default"); return I18n.tr("instance.resHeightLabel") + ":" + String(v) }
                            valueFromText: function(t, locale) { var n = parseInt(t); return isNaN(n) ? 0 : n }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: I18n.tr("instance.fullscreen")
                                font.pixelSize: 14
                                color: palette.text
                            }
                            Text {
                                text: I18n.tr("instance.fullscreenDesc")
                                font.pixelSize: 11
                                color: palette.placeholderText
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Switch {
                            id: fsSwitch
                            checked: root.readInstanceSetting("launch/fullscreen", false)
                            onToggled: root.writeInstanceSetting("launch/fullscreen", checked)
                        }
                    }
                }
            }

            // Java path
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: javaInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: javaHover.hovered ? Qt.alpha(Theme.primary, 0.08) : Theme.surfaceContainer
                HoverHandler { id: javaHover }

                ColumnLayout {
                    id: javaInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: I18n.tr("instance.customJava")
                                font.pixelSize: 14
                                color: palette.text
                            }
                            Text {
                                text: I18n.tr("instance.customJavaDesc")
                                font.pixelSize: 11
                                color: palette.placeholderText
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: javaField
                            Layout.fillWidth: true
                            text: root.readInstanceSetting("launch/javaPath", "")
                            placeholderText: root.isWindows ? I18n.tr("instance.javaPathWin") : I18n.tr("instance.javaPathUnix")
                            font.pixelSize: 12
                            onEditingFinished: root.writeInstanceSetting("launch/javaPath", text)
                        }

                        Button {
                            text: I18n.tr("instance.browse")
                            font.weight: Font.Normal
                            onClicked: javaFileDialog.open()
                        }
                    }
                }
            }

            // Version folder
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: verFolderInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: vfHover.hovered ? Qt.alpha(Theme.primary, 0.08) : Theme.surfaceContainer
                HoverHandler { id: vfHover }

                RowLayout {
                    id: verFolderInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: I18n.tr("instance.versionFolder")
                            font.pixelSize: 14
                            color: palette.text
                        }
                        Text {
                            text: instanceData.verDir || ""
                            font.pixelSize: 11
                            color: palette.placeholderText
                            elide: Text.ElideLeft
                            Layout.fillWidth: true
                        }
                    }

                    Button {
                        text: I18n.tr("instance.open")
                        font.weight: Font.Normal
                        onClicked: {
                            if (instanceData.verDir)
                                Qt.openUrlExternally("file:///" + instanceData.verDir.replace(/\\/g, "/"))
                        }
                    }
                }
            }

            // Saves folder
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: savesInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: savesHover.hovered ? Qt.alpha(Theme.primary, 0.08) : Theme.surfaceContainer
                HoverHandler { id: savesHover }

                RowLayout {
                    id: savesInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: I18n.tr("instance.savesFolder")
                            font.pixelSize: 14
                            color: palette.text
                        }
                        Text {
                            text: (gameDir || "") + "/saves"
                            font.pixelSize: 11
                            color: palette.placeholderText
                            elide: Text.ElideLeft
                            Layout.fillWidth: true
                        }
                    }

                    Button {
                        text: I18n.tr("instance.open")
                        font.weight: Font.Normal
                        onClicked: {
                            if (gameDir)
                                Qt.openUrlExternally("file:///" + gameDir.replace(/\\/g, "/") + "/saves")
                        }
                    }
                }
            }

            // Installed mods
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: modsCol.implicitHeight + 32
                radius: Theme.shapeMedium
                color: modsHover.hovered ? Qt.alpha(Theme.primary, 0.08) : Theme.surfaceContainer
                HoverHandler { id: modsHover }

                ColumnLayout {
                    id: modsCol
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: I18n.tr("instance.installedMods")
                                font.pixelSize: 14
                                color: palette.text
                            }
                            Text {
                                text: installedMods.length === 0 ? I18n.tr("instance.noMods") : I18n.tr("instance.modsCount").replace("%1", String(installedMods.length))
                                font.pixelSize: 11
                                color: palette.placeholderText
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: I18n.tr("instance.openFolder")
                            font.weight: Font.Normal
                            onClicked: {
                                if (gameDir)
                                    kernel.modManager.openModsFolder(gameDir)
                            }
                        }
                    }

                    ListView {
                        id: installedModsList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(installedMods.length, 4) * 42
                        spacing: 4
                        clip: true
                        model: installedMods
                        ScrollBar.vertical: OverlayScrollBar {
                            policy: ScrollBar.AsNeeded
                            width: 8
                        }

                        delegate: Rectangle {
                            width: installedModsList.width
                            height: 38
                            radius: Theme.shapeSmall
                            color: Theme.surfaceContainer

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 8
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.fileName || ""
                                    font.pixelSize: 12
                                    color: palette.text
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: formatBytes(modelData.size || 0)
                                    font.pixelSize: 10
                                    color: palette.placeholderText
                                }

                                Button {
                                    text: I18n.tr("instance.delete")
                                    font.weight: Font.Normal
                                    flat: true
                                    onClicked: {
                                        if (gameDir && modelData.fileName)
                                            kernel.modManager.removeMod(gameDir, modelData.fileName)
                                        reloadMods()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Delete instance
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: delInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Qt.alpha("#F44336", 0.08)
                border.color: Qt.alpha("#F44336", 0.3)
                border.width: 1

                RowLayout {
                    id: delInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    ColumnLayout {
                        spacing: 2
                        Text {
                            text: I18n.tr("instance.deleteTitle")
                            font.pixelSize: 14
                            color: "#F44336"
                        }
                        Text {
                            text: I18n.tr("instance.deleteConfirmBrief")
                            font.pixelSize: 11
                            color: Qt.alpha("#F44336", 0.7)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: I18n.tr("instance.delete")
                        highlighted: true
                        font.weight: Font.Normal
                        onClicked: {
                            deleteConfirmPopup.open()
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        ScrollBar.vertical: OverlayScrollBar { }
    }

    // Delete confirmation dialog
    Popup {
        id: deleteConfirmPopup

        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width - 64, 420)
        padding: 24

        background: Rectangle {
            radius: Theme.shapeLarge
            color: palette.window
            border.color: palette.mid
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 20

            Text {
                text: I18n.tr("instance.deleteTitle")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
            }

            Text {
                text: I18n.tr("instance.deleteConfirmFull").replace("%1", instanceData.id || "")
                font.pixelSize: 13
                color: palette.placeholderText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                lineHeight: 1.4
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Layout.alignment: Qt.AlignRight

                Button {
                    text: I18n.tr("instance.cancel")
                    font.weight: Font.Normal
                    onClicked: deleteConfirmPopup.close()
                }

                Button {
                    text: I18n.tr("instance.delete")
                    font.weight: Font.Normal
                    highlighted: true
                    onClicked: {
                        kernel.instanceManager.removeInstance(instanceId)
                        deleteConfirmPopup.close()
                        window.navigateToPage(4, I18n.tr("launch.manageInstances"))
                    }
                }
            }
        }

        ScrollBar.vertical: OverlayScrollBar {
            followAlways: true
            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            width: 8
        }
    }

    // Java executable picker
    FileDialog {
        id: javaFileDialog
        title: I18n.tr("instance.selectJava")
        nameFilters: root.isWindows
                   ? [I18n.tr("instance.javaFilterWin"), I18n.tr("instances.allFiles")]
                   : [I18n.tr("instance.javaFilterUnix"), I18n.tr("instances.allFiles")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            var p = selectedFile.toString().replace(/^file:\/\//, "")
            javaField.text = p
            root.writeInstanceSetting("launch/javaPath", p)
        }
    }
}