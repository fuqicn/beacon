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

    // Solid window background. The translucent-window + Mica path is disabled:
    // on Windows 11 it leaves the content area a persistent gray block that
    // does not follow window resizes. Theme.surface follows the applied scheme
    // (via the application palette) so dark/light always render correctly.
    color: Theme.surface

    // Force the window palette's accent to the system accent so native Qt
    // Quick Controls (Button/ComboBox/TabButton hover & selection, Fusion and
    // Windows styles) follow Theme.primary on platforms whose style palette
    // defaults to a wrong/white Highlight (Windows 10, GNOME/Linux).
    // Note: DO NOT bind palette.highlightedText to kernel.windowColor() here —
    // that would call QGuiApplication::palette() every frame on the main thread,
    // causing periodic stutter even when idle. Instead compute once and store.
    palette.highlight: Theme.primary

    // Cache the text color for tinted icons so AppIcon.qml doesn't trigger
    // palette access on every binding evaluation cycle. Update only on theme change.
    property color _cachedTextColor: kernel.windowColor().hslLightness < 0.5 ? Qt.white : Qt.black
    property color _cachedPaletteText: Qt.rgba(0.8, 0.8, 0.8, 1)  // light-gray fallback

    onClosing: function(close) {
        // Cancel every in-flight download and sweep temp files before quitting.
        kernel.prepareShutdown()
        close.accepted = true
        Qt.quit()
    }

    Component.onCompleted: {
        // Theme.themeMode has already been set by main.cpp, so no need to call applyThemeMode again
        // Initialize cached palette colors once (avoids per-frame palette access)
        _cachedTextColor = kernel.windowColor().hslLightness < 0.5 ? Qt.white : Qt.black
        console.log("[main] Component.onCompleted: Theme.themeMode =", Theme.themeMode)
        console.log("[main] Component.onCompleted: Theme.darkMode =", Theme.darkMode)
        console.log("[main] Component.onCompleted: kernel.windowColor() =", kernel.windowColor())
        console.log("[main] Component.onCompleted: _cachedTextColor =", _cachedTextColor)
        refreshMcRunning()
        // Auto update check honours the user's setting (default: on).
        if (kernel.settingsManager.value("update/auto", true))
            kernel.checkForUpdate()
        // First launch after an update: show the release notes once the window
        // has been laid out so the popup can centre itself correctly.
        if (kernel.showChangelog)
            Qt.callLater(function() { changelogDialog.open() })
    }
    readonly property string _uiFont: Qt.platform.os === "osx" ? "PingFang SC" : Qt.platform.os === "linux" ? "Noto Sans CJK SC" : "Microsoft YaHei"
    font.family: _uiFont

    readonly property bool isCompact: window.width < 600
    property int navIndex: 0
    property bool settingsActive: false
    property string subPageTitle: ""

    readonly property var navModel: [
        { labelKey: "nav.launch", icon: "nav-play" },
        { labelKey: "nav.download", icon: "nav-download" },
        { labelKey: "nav.tools", icon: "nav-wrench" }
    ]

    function navigateTo(index) {
        // navModel indices: launch=0, download=1, tools=2
        // Page indices: launch=0, download=1, settings=2, tools=3
        if (index === 2)       pageContainer.current = 3   // tools → ToolsPage
        else                  pageContainer.current = index
        navIndex = index
        settingsActive = false
        subPageTitle = ""
    }

    function navigateToSettings() {
        settingsActive = true
        navIndex = -1
        subPageTitle = ""
        pageContainer.current = 2
    }

    function navigateToPage(index, title) {
        subPageTitle = title
        pageContainer.current = index
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Navigation rail (medium+ screens). Collapsed = 56 px, icons only;
        // expanded = 200 px, icons + labels. Width goes through the layout
        // attached properties so the page area reflows when it toggles.
        Rectangle {
            id: navRail
            Layout.fillHeight: true
            // preferredWidth is what Qt's layout engine uses for size negotiation;
            // binding it directly plus adding a Behaviour ensures the parent RowLayout
            // reflows its children during the animation — otherwise the right panel
            // would stay at the expanded width until the animation completes.
            Layout.preferredWidth: collapsed ? 56 : 200
            visible: !window.isCompact
            color: "transparent"
            clip: true

            property bool collapsed: false

            Behavior on Layout.preferredWidth {
                enabled: Theme.animationsEnabled
                NumberAnimation { duration: 220; easing.type: Easing.InOutCubic }
            }
            // Keep actual width in sync with preferred so internal anchors line up.
            Behavior on width {
                enabled: Theme.animationsEnabled
                NumberAnimation { duration: 220; easing.type: Easing.InOutCubic }
            }

            // ---- Header: brand mark + collapse/expand toggle --------------
            Item {
                id: navHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 56

                // Beacon logo — uses a plain Image so the SVG's native colours
                // (gradient / multi-fill) render unchanged, bypassing the tint
                // provider which would otherwise force a single solid colour.
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 20
                    width: 22; height: 22
                    source: "qrc:/icons/beacon.svg"
                    sourceSize.width: 22
                    sourceSize.height: 22
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    asynchronous: true
                    visible: !navRail.collapsed
                }

                Button {
                    id: railToggle
                    // Always anchored to the right edge — no x-calculation that
                    // would shift during the width animation.
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 40
                    padding: 0
                    focusPolicy: Qt.NoFocus
                    onClicked: navRail.collapsed = !navRail.collapsed

                    contentItem: AppIcon {
                        iconName: "nav-columns"
                        iconSize: 20
                    }

                    background: Rectangle {
                        radius: Math.max(Theme.shapeSmall, 6)
                        color: railToggle.hovered || railToggle.pressed
                               ? Theme.hoverOverlay : "transparent"
                        Behavior on color {
                            enabled: Theme.animationsEnabled
                            ColorAnimation { duration: 120 }
                        }
                    }

                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: I18n.tr(navRail.collapsed ? "nav.expandRail"
                                                           : "nav.collapseRail")
                }
            }

            // ---- Nav items ------------------------------------------------
            ListView {
                id: navList
                anchors.top: navHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: settingsRow.top
                anchors.topMargin: 4
                model: window.navModel
                spacing: 2

                delegate: Item {
                    id: navRow
                    width: navList.width
                    height: uiStyleName === "windows" ? 36 : 44

                    readonly property bool active: index === window.navIndex
                    readonly property bool hovered: rowHover.hovered

                    HoverHandler { id: rowHover }

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        radius: Theme.shapeMedium
                        color: {
                            if (navRow.hovered) return Theme.hoverOverlay
                            if (navRow.active)  return Qt.rgba(0.5, 0.5, 0.5, 0.07)
                            return "transparent"
                        }
                        Behavior on color {
                            enabled: Theme.animationsEnabled
                            ColorAnimation { duration: 120 }
                        }
                    }

                    Rectangle {
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 20
                        radius: 1.5
                        color: Theme.primary
                        visible: navRow.active
                    }

                    AppIcon {
                        x: uiStyleName === "windows" ? 16 : 20
                        anchors.verticalCenter: parent.verticalCenter
                        iconSize: uiStyleName === "windows" ? 16 : 20
                        iconName: modelData.icon
                    }

                    Text {
                        x: uiStyleName === "windows" ? 40 : 52
                        anchors.verticalCenter: parent.verticalCenter
                        width: navRow.width - x - (uiStyleName === "windows" ? 8 : 12)
                        visible: !navRail.collapsed
                        text: I18n.tr(modelData.labelKey)
                        font.pixelSize: uiStyleName === "windows" ? 12 : 13
                        font.weight: navRow.active ? Font.DemiBold : Font.Normal
                        color: palette.text
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.navigateTo(index)
                    }
                }
            }

            // Settings — fixed at bottom of rail, same layout as nav items
            Item {
                id: settingsRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: uiStyleName === "windows" ? 36 : 44

                readonly property bool active: settingsActive
                readonly property bool hovered: settingsHover.hovered

                HoverHandler { id: settingsHover }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    radius: Theme.shapeMedium
                    color: {
                        if (settingsRow.hovered) return Theme.hoverOverlay
                        if (settingsRow.active)  return Qt.rgba(0.5, 0.5, 0.5, 0.07)
                        return "transparent"
                    }
                    Behavior on color {
                        enabled: Theme.animationsEnabled
                        ColorAnimation { duration: 120 }
                    }
                }

                Rectangle {
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: 20
                    radius: 1.5
                    color: Theme.primary
                    visible: settingsRow.active
                }

                AppIcon {
                    x: uiStyleName === "windows" ? 16 : 20
                    anchors.verticalCenter: parent.verticalCenter
                    iconSize: uiStyleName === "windows" ? 16 : 20
                    iconName: "nav-gear"
                }

                Text {
                    x: uiStyleName === "windows" ? 40 : 52
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, settingsRow.width - x - (uiStyleName === "windows" ? 8 : 12))
                    visible: !navRail.collapsed
                    text: I18n.tr("nav.settings")
                    font.pixelSize: uiStyleName === "windows" ? 12 : 13
                    font.weight: settingsRow.active ? Font.DemiBold : Font.Normal
                    color: palette.text
                    elide: Text.ElideRight
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: navigateToSettings()
                }
            }

        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Let the content pane shrink below the natural width of its
            // children. Without this a wide filter row (search pages) forces
            // the whole right pane wider than the window and the right edge
            // gets clipped.
            Layout.minimumWidth: 0
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
                    text: subPageTitle !== "" ? subPageTitle : (settingsActive ? I18n.tr("nav.settings") : I18n.tr(window.navModel[window.navIndex].labelKey))
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
                Layout.minimumWidth: 0

                property int current: 0

                function activatePage(index, animate) {
                    for (var i = 0; i < children.length; i++) {
                        var l = children[i]
                        if (!l.hasOwnProperty("pageIndex")) continue
                        var show = (l.pageIndex === index)
                        l.enabled = show
                        if (show) {
                            l.active = true
                            if (animate && l.opacity !== 1) {
                                l.scale = Theme.scaleAnim ? 0.92 : 1
                                l.opacity = 0
                            }
                            l.scale = 1
                            l.opacity = 1
                        } else {
                            l.opacity = 0
                            l.scale = Theme.scaleAnim ? 0.92 : 1
                        }
                    }
                }

                onCurrentChanged: activatePage(current, true)
                Component.onCompleted: activatePage(0, false)

                // Unload pages left for a while to reclaim QML object/binding memory.
                // IMPORTANT: qmlCollectGarbage() is a synchronous GC pass that blocks
                // the GUI event loop; calling it directly here would freeze the UI
                // every 20 seconds. Schedule it via callLater so the current event
                // (the timer callback) finishes first, then GC runs on the idle path.
                Timer {
                    id: unloadTimer
                    // 20 s was short enough that most page switches had to
                    // re-create the whole page (visible hitch on every
                    // navigation). Two minutes still reclaims the memory while
                    // keeping page reloads off the common path.
                    interval: 120000
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
                        // Do NOT call qmlCollectGarbage() here: trimComponentCache()
                        // + collectGarbage() blocks the main event loop for tens of
                        // milliseconds every 20 s, causing a perceptible periodic
                        // stutter. QML's own reference counting reclaims memory
                        // once unload sets all active=false bindings to inactive.
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
        width: Math.min(400, window.width - 48)
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
        ToolTip.text: I18n.tr("tooltip.killMinecraft")

        background: Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.primary
            Rectangle {
                anchors.fill: parent; radius: parent.radius
                color: killBtn.down ? Theme.pressedOverlay : (killBtn.hovered ? Theme.hoverOverlay : "transparent")
                Behavior on color { ColorAnimation { duration: 150 } }
            }
        }

        contentItem: AppIcon {
            anchors.centerIn: parent
            iconName: "power"
            iconSize: 20
            // Always white: the button has a solid primary-colour background in
            // both light and dark themes, so a white icon is legible everywhere.
            tint: "#ffffff"
        }
    }

    // Auto-update prompt (bottom-right, non-blocking; progress shows in the
    // unified download status panel while downloading).
    UpdatePopup {
        id: updatePopup
        // Stack above whatever occupies the bottom-right corner.
        bottomGap: 24
                              + (downloadPanel.opacity > 0 ? downloadPanel.height + 12 : 0)
                              + (isCompact ? navBar.height + 8 : 0)
    }

    // Automatic crash dialog: opens with a parsed diagnosis when a launched
    // game exits abnormally (see kernel.gameCrashed).
    CrashDialog {
        id: crashDialog
        onViewLogRequested: window.navigateToPage(6, I18n.tr("tools.logSub"))
    }

    // First launch after an update: summarize what changed in this release.
    // Closing persists the version, so it is shown once per update.
    ChangelogDialog {
        id: changelogDialog
        version: kernel.changelogVersion
    }

    property bool mcRunning: false

    function refreshMcRunning() {
        mcRunning = kernel.launchManager.running || kernel.minecraftRunning
    }

    // Poll only when a launcher-tracked run is active; the background poller
    // (mc_mcPollThread) already updates kernel.minecraftRunning every 3 s on
    // a worker thread, so this Timer stays off most of the time.
    Timer {
        interval: 3000
        running: kernel.launchManager.running || kernel.minecraftRunning
        repeat: true
        onTriggered: refreshMcRunning()
    }

    Connections {
        target: kernel.launchManager
        function onRunningChanged() {
            refreshMcRunning()
        }
        function onProcessFinished() {
            // Clear immediately so the kill button disappears right away,
            // rather than waiting for the next 15 s background poll cycle.
            kernel.setMinecraftRunning(false)
            refreshMcRunning()
        }
    }

    Connections {
        target: kernel
        function onMinecraftRunningChanged() {
            refreshMcRunning()
        }
        function onGameCrashed(info) {
            crashDialog.openWith(info)
        }
        function onPaletteChanged() {
            // Palette changed (theme switch): update the cached text color
            // so AppIcon.qml can use the cached value without calling
            // palette.text every binding cycle.
            _cachedTextColor = kernel.windowColor().hslLightness < 0.5 ? Qt.white : Qt.black
        }
    }
}
