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

    function styleOptions() {
        var opts = [{ text: I18n.tr("settings.styleAuto"), key: "auto" }]
        var os = Qt.platform.os
        if (os === "windows") {
            opts.push({ text: I18n.tr("settings.styleWinUI3"), key: "fluentwinui3" })
            opts.push({ text: I18n.tr("settings.styleWindows"), key: "windows" })
        } else if (os === "osx") {
            opts.push({ text: I18n.tr("settings.styleWinUI3"), key: "fluentwinui3" })
            opts.push({ text: I18n.tr("settings.styleMacOS"), key: "macos" })
        } else {
            opts.push({ text: I18n.tr("settings.styleWinUI3"), key: "fluentwinui3" })
            opts.push({ text: I18n.tr("settings.styleWindows"), key: "windows" })
            opts.push({ text: I18n.tr("settings.styleFusion"), key: "fusion" })
            opts.push({ text: I18n.tr("settings.styleImagine"), key: "imagine" })
        }
        return opts
    }

    Component.onCompleted: {
        javaPathInput.text = kernel.settingsManager.value("java/path", "")
        memorySetting.value = kernel.settingsManager.value("java/memory", 4096)
        dlThreadsSetting.value = kernel.settingsManager.value("download/threads", 64)
        langCombo.currentIndex = kernel.settingsManager.value("language/index", -1) + 1
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
                text: "Java 运行时"
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

                        Text { text: "Java 路径"; color: palette.placeholderText; font.pixelSize: 14 }
                        TextField {
                            id: javaPathInput
                            Layout.fillWidth: true
                            placeholderText: "留空使用内置 Java"
                            font.pixelSize: 13
                            onTextChanged: kernel.settingsManager.setValue("java/path", text)
                        }
                        Button {
                            text: "扫描"
                            font.weight: Font.Normal
                            onClicked: { kernel.javaManager.findJava() }
                        }
                    }

                    Text {
                        text: "已检测到 " + kernel.javaManager.runtimes.length + " 个 Java 运行时"
                        font.pixelSize: 12; color: palette.placeholderText
                    }
                }
            }

            // Memory settings
            Text {
                text: "内存设置"
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
                        Text { text: I18n.tr("settings.memory") + " (MB)"; color: palette.placeholderText; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                        SpinBox {
                            id: memorySetting
                            from: 512; to: 65536; stepSize: 512
                            value: 4096
                            onValueChanged: kernel.settingsManager.setValue("java/memory", value)
                            HoverHandler { id: memHintHover }
                            ToolTip.visible: memHintHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: I18n.tr("settings.memoryHint")
                        }
                    }
            }

            // Download settings
            Text {
                text: "下载"
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
                            { text: I18n.tr("download.source.bmclapi"), key: "bmclapi" },
                            { text: I18n.tr("download.source.mcimirror"), key: "mcimirror" },
                            { text: I18n.tr("download.source.mcbbs"), key: "mcbbs" }
                        ]
                        textRole: "text"
                        valueRole: "key"
                        onActivated: {
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
                color: isoHover.hovered ? Qt.alpha(palette.highlight, 0.08) : Theme.surfaceContainer
                HoverHandler { id: isoHover }

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
                        onActivated: kernel.settingsManager.setValue("launch/isolationPolicy", currentValue)
                        HoverHandler { id: isoPolicyHover }
                        ToolTip.visible: isoPolicyHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: I18n.tr("settings.isolationPolicyHint")
                    }
                }
            }

            // Theme settings
            Text {
                text: "外观"
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
                            onActivated: {
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
                        Item { Layout.fillWidth: true }
                        Button {
                            text: I18n.tr("settings.restartNow")
                            enabled: restartNeeded
                            onClicked: kernel.restartApp()
                            HoverHandler { id: restartHover }
                            ToolTip.visible: restartHover.hovered && restartNeeded
                            ToolTip.delay: 500
                            ToolTip.text: I18n.tr("settings.restartHint")
                        }
                    }
                }
            }

            // System appearance settings
            Text {
                text: "系统"
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
                        Text { text: "始终显示滚动条"; color: palette.placeholderText; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                        Switch {
                            checked: Theme.alwaysScrollbars
                            onToggled: Theme.setAlwaysScrollbars(checked)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "透明效果"; color: palette.placeholderText; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                        Switch {
                            checked: Theme.transparencyEnabled
                            onToggled: Theme.setTransparencyEnabled(checked)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "动画效果"; color: palette.placeholderText; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                        Switch {
                            checked: Theme.animationsEnabled
                            onToggled: Theme.setAnimationsEnabled(checked)
                        }
                    }
                }
            }

            // Language
            Text {
                text: "语言"
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
                        model: [I18n.tr("settings.language.followSystem"), "中文", "English", "日本語", "Français"]
                        Layout.preferredWidth: 130
                        onCurrentIndexChanged: {
                            kernel.settingsManager.setValue("language/index", currentIndex - 1)
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
                        text: "Launcher v1.0.0\n基于 Qt 6 的 Minecraft 启动器"
                        font.pixelSize: 13; color: palette.placeholderText
                        lineHeight: 1.4
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        ScrollBar.vertical: ScrollBar {
            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            width: 8
        }
    }
}
