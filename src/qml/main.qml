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
import "components"
import "pages"

ApplicationWindow {
    id: window
    visible: true
    width: 960
    height: 640
    minimumWidth: 800
    minimumHeight: 500
    title: "Beacon"

    color: Theme.transparencyEnabled
           ? (isWin11 ? "transparent" : Qt.rgba(palette.window.r, palette.window.g, palette.window.b, Theme.transparencyOpacity))
           : palette.window
    background: Rectangle {
        color: Theme.transparencyEnabled
               ? (isWin11 ? Qt.rgba(palette.window.r, palette.window.g, palette.window.b, Theme.transparencyOpacity) : "transparent")
               : palette.window
    }

    onClosing: {
        close.accepted = true
        Qt.quit()
    }

    Component.onCompleted: {
        kernel.applyThemeMode(Theme.themeMode)
        refreshMcRunning()
    }
    readonly property string _uiFont: Qt.platform.os === "osx" ? "PingFang SC" : Qt.platform.os === "linux" ? "Noto Sans CJK SC" : "Microsoft YaHei"
    font.family: _uiFont

    readonly property bool isCompact: window.width < 600
    property int navIndex: 0
    property string subPageTitle: ""

    readonly property var navModel: [
        { labelKey: "nav.launch", icon: "play" },
        { labelKey: "nav.download", icon: "download" },
        { labelKey: "nav.settings", icon: "gear" },
        { labelKey: "nav.tools", icon: "wrench" }
    ]

    function navigateTo(index) {
        navIndex = index
        subPageTitle = ""
        pageContainer.current = index
    }

    function navigateToPage(index, title) {
        subPageTitle = title
        pageContainer.current = index
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Navigation Rail (medium+ screens)
        Rectangle {
            Layout.fillHeight: true
            width: 72
            visible: !isCompact
            color: "transparent"

            // Active-item pill that glides to the selected button with a
            // non-linear easing instead of flashing into place instantly.
            Rectangle {
                id: navIndicator
                x: 0
                y: 16 + window.navIndex * 68
                width: 72
                height: 64
                radius: Theme.shapeExtraLarge
                color: Qt.alpha(palette.highlight, 0.14)
                visible: window.navIndex >= 0
                Behavior on y {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { duration: 320; easing.type: Easing.OutBack }
                }
            }

            Column {
                anchors.fill: parent
                anchors.topMargin: 16
                spacing: 4

                Repeater {
                    model: window.navModel
                    delegate: ItemDelegate {
                        id: navBtn
                        width: 72
                        height: 64

                        background: Rectangle {
                            radius: Theme.shapeExtraLarge
                            color: navBtn.hovered ? Qt.alpha(palette.placeholderText, 0.09) : "transparent"
                            Behavior on color {
                                enabled: Theme.animationsEnabled
                                ColorAnimation { duration: 150 }
                            }
                        }

                        contentItem: Column {
                            spacing: 4
                            anchors.verticalCenter: parent.verticalCenter

                            AppIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                iconName: modelData.icon
                                iconSize: 20
                             }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: I18n.tr(modelData.labelKey)
                                font.pixelSize: 11
                                font.weight: index === window.navIndex ? Font.DemiBold : Font.Normal
                                color: index === window.navIndex
                                       ? (isWin11 ? palette.highlight : palette.highlightedText)
                                       : palette.placeholderText
                            }
                        }

                        highlighted: index === window.navIndex
                        onClicked: window.navigateTo(index)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Top App Bar
            Rectangle {
                Layout.fillWidth: true
                height: 48
                color: "transparent"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    text: subPageTitle !== "" ? subPageTitle : I18n.tr(window.navModel[window.navIndex].labelKey)
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: palette.text
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
                opacity: 0.3
            }

            // Content area (with lazy page loading + transition)
            Item {
                id: pageContainer
                Layout.fillWidth: true
                Layout.fillHeight: true

                property int current: 0
                readonly property bool scaleAnim: Qt.platform.os !== "windows" || isWin11

                function activatePage(index, animate) {
                    for (var i = 0; i < children.length; i++) {
                        var l = children[i]
                        if (!l.hasOwnProperty("pageIndex")) continue
                        var show = (l.pageIndex === index)
                        l.enabled = show
                        if (show) {
                            l.active = true
                            if (animate && l.opacity !== 1) {
                                l.scale = scaleAnim ? 0.92 : 1
                                l.opacity = 0
                            }
                            l.scale = 1
                            l.opacity = 1
                        } else {
                            l.opacity = 0
                            l.scale = scaleAnim ? 0.92 : 1
                        }
                    }
                }

                onCurrentChanged: activatePage(current, true)
                Component.onCompleted: activatePage(0, false)

                // Unload pages left for a while to reclaim QML object/binding memory
                Timer {
                    id: unloadTimer
                    interval: 60000
                    repeat: true
                    onTriggered: {
                        var unloaded = false
                        for (var i = 0; i < parent.children.length; i++) {
                            var l = parent.children[i]
                            if (!l.hasOwnProperty("pageIndex")) continue
                            // Keep DownloadPage (mods stack) alive so browsing state
                            // (search results / detail versions) survives page switches.
                            if (l.pageIndex !== 1 && !l.enabled && l.active) {
                                l.active = false
                                unloaded = true
                            }
                        }
                        if (unloaded)
                            kernel.qmlCollectGarbage()
                    }
                }

                Loader {
                    id: page0
                    property int pageIndex: 0
                    anchors.fill: parent
                    active: false
                    asynchronous: true
                    opacity: 0
                    enabled: false
                    Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    Behavior on scale { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    sourceComponent: LaunchPage { anchors.fill: parent }
                }
                Loader {
                    id: page1
                    property int pageIndex: 1
                    anchors.fill: parent
                    active: false
                    asynchronous: true
                    opacity: 0
                    enabled: false
                    Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    Behavior on scale { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    sourceComponent: DownloadPage { anchors.fill: parent }
                }
                Loader {
                    id: page2
                    property int pageIndex: 2
                    anchors.fill: parent
                    active: false
                    asynchronous: true
                    opacity: 0
                    enabled: false
                    Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    Behavior on scale { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    sourceComponent: SettingsPage { anchors.fill: parent }
                }
                Loader {
                    id: page3
                    property int pageIndex: 3
                    anchors.fill: parent
                    active: false
                    asynchronous: true
                    opacity: 0
                    enabled: false
                    Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    Behavior on scale { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    sourceComponent: ToolsPage { anchors.fill: parent }
                }
                Loader {
                    id: page4
                    property int pageIndex: 4
                    anchors.fill: parent
                    active: false
                    asynchronous: true
                    opacity: 0
                    enabled: false
                    Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    Behavior on scale { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    sourceComponent: InstancesPage { anchors.fill: parent }
                }
                Loader {
                    id: page5
                    property int pageIndex: 5
                    anchors.fill: parent
                    active: false
                    asynchronous: true
                    opacity: 0
                    enabled: false
                    Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    Behavior on scale { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    sourceComponent: InstanceSettingsPage { anchors.fill: parent }
                }
                Loader {
                    id: page6
                    property int pageIndex: 6
                    anchors.fill: parent
                    active: false
                    asynchronous: true
                    opacity: 0
                    enabled: false
                    Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    Behavior on scale { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    sourceComponent: LogViewerPage { anchors.fill: parent }
                }
                Loader {
                    id: page7
                    property int pageIndex: 7
                    anchors.fill: parent
                    active: false
                    asynchronous: true
                    opacity: 0
                    enabled: false
                    Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    Behavior on scale { enabled: Theme.animationsEnabled; NumberAnimation { duration: 200 } }
                    sourceComponent: FileManagerPage { anchors.fill: parent }
                }
            }
        }
    }

    // Navigation Bar (compact screens)
    TabBar {
        id: navBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: isCompact

        Repeater {
            model: window.navModel
            TabButton {
                text: I18n.tr(modelData.labelKey)
                font.pixelSize: 12
                onClicked: window.navigateTo(index)
            }
        }
    }

    // Unified download status card (Minecraft/Java downloads + mod install tasks)
    DownloadStatusPanel {
        id: downloadPanel
        anchors.right: parent.right
        anchors.bottom: isCompact ? navBar.top : parent.bottom
        anchors.rightMargin: 24
        anchors.bottomMargin: 24
        width: 400
        z: 100
    }

    // Kill Minecraft floating button
    Button {
        id: killBtn
        anchors.right: parent.right
        anchors.bottom: downloadPanel.top
        anchors.rightMargin: 24
        anchors.bottomMargin: 8
        width: 40; height: 40
        visible: mcRunning
        z: 101
        focusPolicy: Qt.NoFocus

        onClicked: kernel.killAllMinecraft()
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: "关闭所有正在运行的 Minecraft"

        background: Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: palette.highlight
            Rectangle {
                anchors.fill: parent; radius: parent.radius
                color: killBtn.down ? Qt.rgba(0,0,0,0.2) : (killBtn.hovered ? Qt.rgba(0,0,0,0.1) : "transparent")
                Behavior on color { ColorAnimation { duration: 150 } }
            }
        }

        contentItem: AppIcon {
            anchors.centerIn: parent
            iconName: "power"
            iconSize: 20
        }
    }

    property bool mcRunning: false

    function refreshMcRunning() {
        mcRunning = kernel.launchManager.running || kernel.isAnyMinecraftRunning()
    }

    Timer {
        interval: 3000
        running: kernel.launchManager.running || kernel.isAnyMinecraftRunning()
        repeat: true
        onTriggered: refreshMcRunning()
    }

    Connections {
        target: kernel.launchManager
        function onRunningChanged() {
            refreshMcRunning()
        }
    }

    Connections {
        target: kernel
        function onMinecraftRunningChanged() {
            refreshMcRunning()
        }
    }
}