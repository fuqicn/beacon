import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root

    property string selectedVersion: ""
    property string selectedJavaPath: ""

    Component.onCompleted: {
        var sel = kernel.instanceManager.getSelectedInstance()
        if (sel && sel.id) {
            selectedVersion = sel.id
            kernel.selectInstance(sel.id, sel.rootDir || kernel.mcDir)
        }
    }

    Connections {
        target: kernel.instanceManager
        function onSelectedChanged() {
            var sel = kernel.instanceManager.getSelectedInstance()
            if (sel && sel.id) {
                selectedVersion = sel.id
                kernel.selectInstance(sel.id, sel.rootDir || kernel.mcDir)
            }
        }
        function onInstancesChanged() {
            var sel = kernel.instanceManager.getSelectedInstance()
            if (sel && sel.id) {
                selectedVersion = sel.id
                kernel.selectInstance(sel.id, sel.rootDir || kernel.mcDir)
            }
        }
    }

    Connections {
        target: kernel.launchManager
        function onLaunchStarted() {
            launchStatus.text = "游戏已启动"
        }
        function onLaunchCompleted(exitCode) {
            launchStatus.text = "游戏已退出 (代码: " + exitCode + ")"
        }
        function onVerifyProgressChanged() {
            if (kernel.launchManager.verifying) {
                launchStatus.text = kernel.launchManager.verifyTask +
                    " (" + Math.round(kernel.launchManager.verifyProgress * 100) + "%)"
            }
        }
        function onErrorOccurred(msg) {
            launchStatus.text = msg
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // Account cards
        Text {
            text: "账户"
            font.pixelSize: 13
            font.weight: Font.Medium
            color: palette.placeholderText
        }

        Repeater {
            model: kernel.authManager.accounts

            delegate: Rectangle {
                Layout.fillWidth: true
                implicitHeight: 56
                radius: Theme.shapeMedium
                color: {
                    if (kernel.authManager.currentAccountIndex === index)
                        return Qt.alpha(palette.highlight, 0.12)
                    return maus.containsMouse ? Qt.alpha(palette.placeholderText, 0.08) : Qt.alpha(palette.placeholderText, 0.05)
                }
                border.color: kernel.authManager.currentAccountIndex === index
                             ? Qt.alpha(palette.highlight, 0.3)
                             : "transparent"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    // Avatar
                    Rectangle {
                        id: avatarRect
                        width: 36; height: 36; radius: Theme.shapeSmall
                        color: Qt.alpha(palette.highlight, 0.1)

                        property bool skinAvailable: modelData.type === 2 && kernel.skinManager.hasCachedSkin(modelData.uuid)

                        Image {
                            anchors.fill: parent
                            source: avatarRect.skinAvailable
                                    ? "file:///" + kernel.skinManager.cachedSkin(modelData.uuid) : ""
                            visible: avatarRect.skinAvailable
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            smooth: false
                        }

                        Text {
                            anchors.centerIn: parent
                            text: modelData.username.charAt(0).toUpperCase()
                            font.pixelSize: 16; font.weight: Font.Bold
                            color: palette.highlight
                            visible: !avatarRect.skinAvailable
                        }

                        Connections {
                            target: kernel.skinManager
                            function onSkinReady(uuid) {
                                if (uuid === modelData.uuid)
                                    avatarRect.skinAvailable = true
                            }
                        }

                        Component.onCompleted: {
                            if (modelData.type === 2)
                                kernel.skinManager.fetchSkin(modelData.uuid)
                        }
                    }

                    // Info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: modelData.username
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: palette.text
                            elide: Text.ElideRight
                        }

                        Text {
                            text: modelData.type === 2 ? "Microsoft 正版" : "离线模式"
                            font.pixelSize: 11
                            color: modelData.type === 2 ? palette.highlight : palette.placeholderText
                        }
                    }

                    // Current indicator / switch button
                    Button {
                        text: kernel.authManager.currentAccountIndex === index ? "当前" : "切换"
                        font.weight: Font.Normal
                        enabled: kernel.authManager.currentAccountIndex !== index
                        onClicked: kernel.authManager.switchAccount(index)
                    }

                    // Delete account
                    Rectangle {
                        width: 28; height: 28; radius: Theme.shapeSmall
                        color: delHover.hovered ? Qt.alpha("#F44336", 0.15) : "transparent"
                        AppIcon { anchors.centerIn: parent; iconName: "trash"; iconSize: 12 }
                        HoverHandler { id: delHover }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: kernel.authManager.removeAccount(index)
                        }
                    }
                }

                MouseArea {
                    id: maus
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }
        }

        // Login progress indicator
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            radius: Theme.shapeMedium
            color: Qt.alpha(palette.highlight, 0.1)
            visible: kernel.authManager.loggingIn

            RowLayout {
                anchors.centerIn: parent
                spacing: 8

                BusyIndicator {
                    width: 18; height: 18
                    running: kernel.authManager.loggingIn
                }

                Text {
                    text: "正在登录 Microsoft... 请在浏览器中完成验证"
                    font.pixelSize: 13
                    color: palette.highlight
                }
            }
        }

        // Add accounts section
        Button {
            Layout.fillWidth: true
            text: "添加 Microsoft 账户"
            highlighted: true
            font.weight: Font.Normal
            enabled: !kernel.authManager.loggingIn
            onClicked: kernel.authManager.addMicrosoftAccount()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            TextField {
                id: offlineNameField
                Layout.fillWidth: true
                placeholderText: "输入离线用户名"
                text: "Player"
            }

            Button {
                text: "添加离线"
                font.weight: Font.Normal
                enabled: !kernel.authManager.loggingIn
                onClicked: kernel.authManager.addOfflineAccount(offlineNameField.text)
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

        // Selected instance
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Column {
                Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter
                spacing: 4
                Text {
                    text: "当前实例"
                    font.pixelSize: 13; color: palette.placeholderText
                }
                Text {
                    id: instanceName
                    text: selectedVersion || "未选择"
                    font.pixelSize: 16; font.weight: Font.Medium
                    color: palette.text
                }
            }

            Button {
                text: "选择实例"
                font.weight: Font.Normal
                onClicked: window.navigateToPage(4, "实例管理")
            }
            Button {
                text: "实例设置"
                font.weight: Font.Normal
                onClicked: {
                    var sel = kernel.instanceManager.getSelectedInstance()
                    if (sel && sel.id)
                        window.navigateToPage(5, "实例设置")
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

        Item { Layout.fillHeight: true }

        // Status
        Text {
            id: launchStatus
            Layout.fillWidth: true
            text: ""
            font.pixelSize: 13
            color: palette.placeholderText
            wrapMode: Text.Wrap
            visible: text !== ""
        }

        // Launch button
        Button {
            Layout.fillWidth: true
            text: {
                if (kernel.launchManager.running) return "运行中..."
                if (kernel.launchManager.verifying) return kernel.launchManager.verifyTask
                return "启动游戏"
            }
            highlighted: true
            font.weight: Font.Normal
            enabled: !kernel.launchManager.running && !kernel.launchManager.verifying && !kernel.javaDownloading && selectedVersion !== ""
            onClicked: {
                launchStatus.text = "正在验证文件..."
                kernel.launchGame(4096)
            }
        }
    }
}
