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

    property bool restartNeeded: false
    // Set once a user-triggered check finishes so result text can show.
    property bool manualChecked: false

    // Children (combos/inputs) are created before this runs, so every saved
    // value can be restored in one place. Without this the page always shows
    // the combo defaults no matter what settings.ini contains.
    Component.onCompleted: _initSettings()

    function styleOptions() {
        var opts = []
        var os = Qt.platform.os
        if (os === "windows") {
            opts.push({ text: I18n.tr("settings.styleAuto"), key: "auto" })
            opts.push({ text: I18n.tr("settings.styleWinUI3"), key: "fluentwinui3" })
            opts.push({ text: I18n.tr("settings.styleWindows"), key: "windows" })
        } else if (os === "osx") {
            opts.push({ text: I18n.tr("settings.styleAuto"), key: "auto" })
            opts.push({ text: I18n.tr("settings.styleWinUI3"), key: "fluentwinui3" })
            opts.push({ text: I18n.tr("settings.styleMacOS"), key: "macos" })
        } else {
            // Linux: Windows style is not available; use Fusion and Imagine
            opts.push({ text: I18n.tr("settings.styleAuto"), key: "auto" })
            opts.push({ text: I18n.tr("settings.styleWinUI3"), key: "fluentwinui3" })
            opts.push({ text: I18n.tr("settings.styleFusion"), key: "fusion" })
            opts.push({ text: I18n.tr("settings.styleImagine"), key: "imagine" })
        }
        return opts
    }

    function _initSettings() {
        // Ensure we're reading from global launcher settings (not instance settings)
        kernel.settingsManager.endInstance()
        // Language first, and logged: it is the field users notice being wrong,
        // so the resolved index must be visible in qtdebug.log.
        //
        // Read `count` BEFORE assigning currentIndex. The `model` binding below
        // is lazy: it only evaluates the first time something reads it, and
        // when that happened after this assignment ComboBox discarded the
        // currentIndex we had just set - leaving the collapsed field blank
        // until the user picked an entry (and blank again after every
        // restart). The other combos avoid this by looping over `model.length`
        // first; this one assigned directly.
        var langIndex = parseInt(kernel.settingsManager.value("language/index", -1), 10)
        if (isNaN(langIndex)) langIndex = -1
        var langCurrent = langIndex + 1
        if (langCurrent < 0 || langCurrent >= langCombo.count) langCurrent = 0
        langCombo.currentIndex = langCurrent
        console.log("_initSettings: language/index=" + langIndex
                    + " -> langCombo.currentIndex=" + langCombo.currentIndex
                    + " count=" + langCombo.count
                    + " text=" + langCombo.currentText)

        javaPathInput.text = kernel.settingsManager.value("java/path", "")
        var memModeVal = kernel.settingsManager.value("java/memoryMode", "auto")
        for (var _mm = 0; _mm < memMode.model.length; ++_mm)
            if (memMode.model[_mm].key === memModeVal) { memMode.currentIndex = _mm; break }
        if (memModeVal === "auto")
            applyAutoMemory()
        else
            memCustom.text = String(kernel.settingsManager.value("java/memory", 4096))
        dlThreadsSetting.value = kernel.settingsManager.value("download/threads", 64)

        var dlSource = kernel.settingsManager.value("download/source", "auto")
        for (var i = 0; i < dlSourceCombo.model.length; ++i) {
            if (dlSourceCombo.model[i].key === dlSource) {
                dlSourceCombo.currentIndex = i
                break
            }
        }
        var isoPolicy = kernel.settingsManager.value("launch/isolationPolicy", "off")
        for (var j = 0; j < isoPolicyCombo.model.length; ++j) {
            if (isoPolicyCombo.model[j].key === isoPolicy) {
                isoPolicyCombo.currentIndex = j
                break
            }
        }
        var uiStyle = kernel.settingsManager.value("ui/style", "auto")
        for (var s = 0; s < styleCombo.model.length; ++s) {
            if (styleCombo.model[s].key === uiStyle) {
                styleCombo.currentIndex = s
                break
            }
        }
    }

    function applyAutoMemory() {
        // Use the same multi-tier allocator that launchGame() uses, so the
        // preview value matches what will actually be allocated at launch.
        var recommended = kernel.computeAutoMemoryMB(1024)
        kernel.settingsManager.setValue("java/memory", recommended)
        memCustom.text = String(recommended)
    }

    Flickable {
        id: flickable
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
            spacing: 16

            // Java settings
            Text {
                text: I18n.tr("settings.javaRuntime")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: javaInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                ColumnLayout {
                    id: javaInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text { text: I18n.tr("settings.javaPath"); color: palette.placeholderText; font.pixelSize: 14 }
                        TextField {
                            id: javaPathInput
                            Layout.fillWidth: true
                            placeholderText: I18n.tr("settings.javaPlaceholder")
                            font.pixelSize: 13
                            onTextChanged: {
                                kernel.settingsManager.endInstance()
                                kernel.settingsManager.setValue("java/path", text)
                            }
                        }
                        Button {
                            text: I18n.tr("settings.scan")
                            font.weight: Font.Normal
                            onClicked: { kernel.javaManager.findJava() }
                        }
                    }

                    Text {
                        text: I18n.tr("settings.javaDetected").replace("%1", String(kernel.javaManager.runtimes.length))
                        font.pixelSize: 12; color: palette.placeholderText
                    }
                }
            }

            // Memory settings
            Text {
                text: I18n.tr("settings.memorySettings")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: memInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                    RowLayout {
                        id: memInner
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        ColumnLayout {
                            spacing: 2
                            Text { text: I18n.tr("settings.memory") + " (MB)"; color: palette.placeholderText; font.pixelSize: 14 }
                            Text {
                                text: I18n.tr("settings.memoryAutoDesc")
                                font.pixelSize: 11
                                color: palette.placeholderText
                                visible: memMode.currentIndex === 0
                            }
                        }
                        Item { Layout.fillWidth: true }
                        ComboBox {
                            id: memMode
                            model: [
                                { text: I18n.tr("settings.memoryAuto"), key: "auto" },
                                { text: I18n.tr("settings.memoryManual"), key: "manual" }
                            ]
                            textRole: "text"
                            valueRole: "key"
                            currentIndex: kernel.settingsManager.value("java/memoryMode", "auto") === "manual" ? 1 : 0
                            onActivated: {
                                kernel.settingsManager.endInstance()
                                kernel.settingsManager.setValue("java/memoryMode", currentValue)
                                if (currentValue === "auto") applyAutoMemory()
                                else memCustom.text = String(kernel.settingsManager.value("java/memory", 4096))
                            }
                        }
                        TextField {
                            id: memCustom
                            Layout.preferredWidth: 120
                            text: String(kernel.settingsManager.value("java/memory", 4096))
                            validator: IntValidator { bottom: 512; top: 65536 }
                            enabled: memMode.currentIndex === 1
                            visible: memMode.currentIndex === 1
                            onEditingFinished: {
                                var v = parseInt(text)
                                if (isNaN(v) || v < 512) v = 512
                                if (v > 65536) v = 65536
                                kernel.settingsManager.endInstance()
                                kernel.settingsManager.setValue("java/memory", v)
                            }
                        }
                    }
            }

            // Download settings
            Text {
                text: I18n.tr("download.mainTitle")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: dlInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                RowLayout {
                    id: dlInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Text { text: I18n.tr("download.threads"); color: palette.placeholderText; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    SpinBox {
                        id: dlThreadsSetting
                        from: 1; to: 64; stepSize: 1
                        value: 64
                        onValueChanged: {
                            kernel.settingsManager.endInstance()
                            kernel.settingsManager.setValue("download/threads", value)
                            kernel.setDownloadThreads(value)
                        }
                        HoverHandler { id: dlThreadsHover }
                        ToolTip.visible: dlThreadsHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: I18n.tr("download.parallelHint")
                    }
                }
            }

            // Download source
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: dlSourceInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                RowLayout {
                    id: dlSourceInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Text { text: I18n.tr("download.source"); color: palette.placeholderText; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: dlSourceCombo
                        model: [
                            { text: I18n.tr("download.source.auto"), key: "auto" },
                            { text: I18n.tr("download.source.official"), key: "mojang" },
                            { text: I18n.tr("download.source.mirror"), key: "mirror" }
                        ]
                        textRole: "text"
                        valueRole: "key"
                        onActivated: {
                            kernel.settingsManager.endInstance()
                            kernel.settingsManager.setValue("download/source", currentValue)
                            kernel.setDownloadSource(currentValue)
                        }
                        HoverHandler { id: dlSourceHover }
                        ToolTip.visible: dlSourceHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: I18n.tr("download.source.hint")
                    }
                }
            }

            // Mod source / MCIMirror / CF API key have been moved to the search pages.

            // Version isolation (global default)
            Text {
                text: I18n.tr("settings.isolation")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: isoInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer

                RowLayout {
                    id: isoInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: I18n.tr("settings.isolation")
                            font.pixelSize: 14
                            color: palette.text
                        }
                        Text {
                            text: I18n.tr("settings.isolationDesc")
                            font.pixelSize: 11
                            color: palette.placeholderText
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            lineHeight: 1.35
                        }
                        Text {
                            text: I18n.tr("settings.isolationHint")
                            font.pixelSize: 11
                            color: palette.placeholderText
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            lineHeight: 1.35
                        }
                    }

                    ComboBox {
                        id: isoPolicyCombo
                        model: [
                            { text: I18n.tr("settings.isolationOff"), key: "off" },
                            { text: I18n.tr("settings.isolationLoader"), key: "loader" },
                            { text: I18n.tr("settings.isolationAll"), key: "all" }
                        ]
                        textRole: "text"
                        valueRole: "key"
                        Layout.preferredWidth: 170
                        onActivated: {
                            kernel.settingsManager.endInstance()
                            kernel.settingsManager.setValue("launch/isolationPolicy", currentValue)
                        }
                        HoverHandler { id: isoPolicyHover }
                        ToolTip.visible: isoPolicyHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: I18n.tr("settings.isolationPolicyHint")
                    }
                }
            }

            // Theme settings
            Text {
                text: I18n.tr("settings.appearance")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: themeInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                RowLayout {
                    id: themeInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Text {
                        text: Theme.themeMode === "system" ? I18n.tr("theme.system")
                              : (Theme.darkMode ? I18n.tr("theme.dark") : I18n.tr("theme.light"))
                        color: palette.placeholderText
                        font.pixelSize: 14
                    }
                    Item { Layout.fillWidth: true }
                    Switch {
                        id: themeFollowSwitch
                        checked: Theme.themeMode === "system"
                        onToggled: {
                            if (checked)
                                Theme.setThemeMode("system")
                            else
                                Theme.setThemeMode(Theme.darkMode ? "dark" : "light")
                        }
                        HoverHandler { id: themeFollowHover }
                        ToolTip.visible: themeFollowHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: I18n.tr("theme.followSystem")
                    }
                    Rectangle {
                        width: 48; height: 28; radius: Theme.shapeLarge
                        color: Theme.darkMode ? "#555" : "#ccc"
                        Behavior on color { ColorAnimation { duration: 200 } }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Theme.toggleTheme()
                        }

                        Rectangle {
                            x: Theme.darkMode ? 22 : 2
                            y: 2; width: 24; height: 24; radius: Theme.shapeLarge
                            color: Theme.darkMode ? "#333" : "#fff"
                            Behavior on x { NumberAnimation { duration: 200 } }

                            Text {
                                anchors.centerIn: parent
                                text: Theme.darkMode ? "\u263E" : "\u2600"
                                font.pixelSize: 14
                                color: palette.text
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: styleInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                ColumnLayout {
                    id: styleInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            text: I18n.tr("settings.style")
                            color: palette.placeholderText
                            font.pixelSize: 14
                        }
                        Item { Layout.fillWidth: true }
                        ComboBox {
                            id: styleCombo
                            Layout.preferredWidth: 180
                            model: styleOptions()
                            textRole: "text"
                            valueRole: "key"
                            // Explicitly bind to saved style on completion
                            Component.onCompleted: {
                                var saved = kernel.settingsManager.value("ui/style", "auto")
                                for (var i = 0; i < model.length; ++i)
                                    if (model[i].key === saved) { currentIndex = i; break }
                            }
                            onActivated: {
                                kernel.settingsManager.endInstance()
                                kernel.settingsManager.setValue("ui/style", currentValue)
                                restartNeeded = true
                            }
                            HoverHandler { id: styleHover }
                            ToolTip.visible: styleHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: I18n.tr("settings.styleHint")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            text: I18n.tr("settings.styleHint")
                            color: palette.placeholderText
                            font.pixelSize: 12
                            visible: restartNeeded
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // System appearance settings
            Text {
                text: I18n.tr("settings.system")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: sysInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                ColumnLayout {
                    id: sysInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: I18n.tr("settings.scrollbar"); color: palette.placeholderText; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                        Switch {
                            checked: Theme.alwaysScrollbars
                            onToggled: Theme.setAlwaysScrollbars(checked)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: I18n.tr("settings.animation"); color: palette.placeholderText; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                        Switch {
                            checked: Theme.animationsEnabled
                            onToggled: {
                                Theme.setAnimationsEnabled(checked)
                                restartNeeded = true
                            }
                        }
                    }
                }
            }

            // Updates
            Text {
                text: I18n.tr("settings.updates")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: updInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                ColumnLayout {
                    id: updInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            text: I18n.tr("settings.autoUpdate")
                            color: palette.placeholderText; font.pixelSize: 14
                        }
                        Item { Layout.fillWidth: true }
                        Switch {
                            checked: kernel.settingsManager.value("update/auto", true)
                            onToggled: {
                                kernel.settingsManager.endInstance()
                                kernel.settingsManager.setValue("update/auto", checked)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text {
                            id: updateStatusText
                            Layout.fillWidth: true
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            color: kernel.updateAvailable ? Theme.primary : palette.placeholderText
                            text: {
                                if (kernel.checkingUpdate)
                                    return I18n.tr("settings.checkUpdate.checking")
                                if (root.manualChecked) {
                                    if (kernel.updateAvailable)
                                        return I18n.tr("settings.checkUpdate.new")
                                                .replace("%1", kernel.latestVersion)
                                    return I18n.tr("settings.checkUpdate.latest")
                                }
                                return ""
                            }
                        }

                        Button {
                            text: I18n.tr("settings.checkUpdate")
                            enabled: !kernel.checkingUpdate && !kernel.updateDownloading
                            onClicked: root.manualChecked = false
                                       , kernel.checkForUpdate()
                        }
                    }
                }
            }

            Connections {
                target: kernel
                function onCheckingUpdateChanged() {
                    if (!kernel.checkingUpdate)
                        root.manualChecked = true
                }
            }

            // Language
            Text {
                text: I18n.tr("settings.languageLabel")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: langInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                RowLayout {
                    id: langInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Text { text: I18n.tr("settings.language"); color: palette.placeholderText; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: langCombo
                        model: [I18n.tr("settings.language.followSystem"), I18n.tr("settings.chinese"), "English", I18n.tr("settings.japanese"), "Français"]
                        Layout.preferredWidth: 130
                        // NOTE: onActivated, not onCurrentIndexChanged. The
                        // page gets unloaded/recreated by the idle timer, and a
                        // currentIndex reset during construction would otherwise
                        // write -1 (follow system) and wipe the saved language.
                        onActivated: {
                            kernel.settingsManager.endInstance()
                            kernel.settingsManager.setValue("language/index", currentIndex - 1)
                            restartNeeded = true
                        }
                        HoverHandler { id: langHover }
                        ToolTip.visible: langHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: I18n.tr("settings.languageNote")
                    }
                }
            }

            // About
            Text {
                text: I18n.tr("settings.about")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: aboutInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                ColumnLayout {
                    id: aboutInner
                    anchors.fill: parent
                    anchors.margins: 16
                    Text {
                        text: I18n.tr("settings.version")
                        font.pixelSize: 13; color: palette.placeholderText
                        lineHeight: 1.4
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        ScrollBar.vertical: OverlayScrollBar {
            followAlways: true
            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            width: 8
        }
    }

    // Restart-required dialog: replaces the old inline button so the user is
    // reminded (not forced) to restart after changing language or style.
    Connections {
        target: root
        function onRestartNeededChanged() {
            if (root.restartNeeded) restartDialog.open()
        }
    }

    Popup {
        id: restartDialog
        modal: false
        focus: true
        closePolicy: Popup.NoAutoClose
        // Match DownloadDialog's sizing pattern: explicit width + height from content
        width: Math.min(window.width - 64, 420)
        height: restartContentColumn.implicitHeight + 64
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)

        background: Rectangle {
            radius: Theme.shapeLarge
            color: palette.window
            border.color: palette.mid
            border.width: 1
        }

        ColumnLayout {
            id: restartContentColumn
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16
            Text {
                text: I18n.tr("settings.restartMessage")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 14
                color: palette.text
                lineHeight: 1.4
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Layout.alignment: Qt.AlignRight
                Button {
                    text: I18n.tr("settings.restartLater")
                    font.weight: Font.Normal
                    onClicked: { root.restartNeeded = false; close() }
                }
                Button {
                    text: I18n.tr("settings.restartNow")
                    highlighted: true
                    font.weight: Font.Normal
                    onClicked: kernel.restartApp()
                }
            }
        }
    }
}
