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
    }
    readonly property string _uiFont: Qt.platform.os === "osx" ? "PingFang SC" : Qt.platform.os === "linux" ? "Noto Sans CJK SC" : "Microsoft YaHei"
    font.family: _uiFont

    readonly property bool isCompact: window.width < 600
    property int navIndex: 0
    property string subPageTitle: ""

    readonly property var navModel: [
        { label: "启动", icon: "play" },
        { label: "下载", icon: "download" },
        { label: "设置", icon: "gear" },
        { label: "工具", icon: "wrench" }
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

            Column {
                anchors.fill: parent
                anchors.topMargin: 16
                spacing: 4

                Repeater {
                    model: window.navModel
                    delegate: ItemDelegate {
                        width: 72
                        height: 64

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
                                text: modelData.label
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
                    text: subPageTitle !== "" ? subPageTitle : window.navModel[window.navIndex].label
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
                        l.active = l.active || show
                        l.enabled = show
                        if (show) {
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
                text: modelData.label
                font.pixelSize: 12
                onClicked: window.navigateTo(index)
            }
        }
    }

    // Bottom download progress panel
    DownloadProgressButton {
        id: downloadFab
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: isCompact ? navBar.top : parent.bottom
        anchors.leftMargin: isCompact ? 8 : 80
        anchors.rightMargin: isCompact ? 8 : 24
        anchors.bottomMargin: isCompact ? 8 : 24
        z: 100
    }

    // Kill Minecraft floating button
    Rectangle {
        id: killBtn
        anchors.right: parent.right
        anchors.bottom: downloadFab.top
        anchors.rightMargin: 24
        anchors.bottomMargin: 8
        width: 40; height: 40; radius: Theme.shapeFull
        color: palette.highlight
        visible: mcRunning
        z: 101

        AppIcon {
            anchors.centerIn: parent
            iconName: "power"
            iconSize: 20
        }

        Rectangle {
            anchors.fill: parent; radius: parent.radius
            color: killMa.pressed ? Qt.rgba(0,0,0,0.2) : (killMa.hovered ? Qt.rgba(0,0,0,0.1) : "transparent")
            Behavior on color { ColorAnimation { duration: 150 } }
        }

        MouseArea {
            id: killMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: kernel.killAllMinecraft()
        }
        ToolTip {
            parent: killBtn
            visible: killMa.hovered
            delay: 500
            text: "关闭所有正在运行的 Minecraft"
        }
    }

    property bool mcRunning: false

    Timer {
        interval: 3000
        running: true
        repeat: true
        onTriggered: mcRunning = kernel.isAnyMinecraftRunning()
    }

    Connections {
        target: kernel
        function onMinecraftRunningChanged() {
            mcRunning = kernel.isAnyMinecraftRunning()
        }
    }
}