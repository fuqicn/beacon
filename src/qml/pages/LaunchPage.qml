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

property string selectedVersion: ""
    property string selectedJavaPath: ""
    property string selectedIcon: ""
    readonly property bool launchActive: kernel.launchManager.running ||
                                         kernel.launchManager.verifying ||
                                         kernel.javaDownloading

    function updateSelection() {
        var sel = kernel.instanceManager.getSelectedInstance()
        if (sel && sel.id) {
            selectedVersion = sel.id
            if (sel.customIcon)
                selectedIcon = "file:///" + sel.customIcon.replace(/\\/g, "/")
            else if (sel.iconKey)
                selectedIcon = "qrc:/icons/instances/" + sel.iconKey + ".png"
            else
                selectedIcon = "qrc:/icons/instances/grass.png"
            kernel.selectInstance(sel.id, sel.rootDir || kernel.mcDir)
            return
        }
        // Selected instance no longer exists (e.g. just deleted): fall back to
        // the first instance, or clear the display when the list is empty.
        var list = kernel.instanceManager.instances || []
        if (list.length > 0) {
            var first = list[0]
            selectedVersion = first.id
            selectedIcon = first.customIcon
                    ? "file:///" + first.customIcon.replace(/\\/g, "/")
                    : "qrc:/icons/instances/" + (first.iconKey || "grass") + ".png"
            kernel.selectInstance(first.id, first.rootDir || kernel.mcDir)
        } else {
            selectedVersion = ""
            selectedIcon = ""
            kernel.selectInstance("", kernel.mcDir)
        }
    }

    function updateStatus() {
        if (kernel.launchManager.running) {
            launchStatus.text = I18n.tr("launch.active")
            return
        }
        if (kernel.launchManager.verifying) {
            launchStatus.text = kernel.launchManager.verifyTask +
                " (" + Math.round(kernel.launchManager.verifyProgress * 100) + "%)"
            return
        }
        if (kernel.javaDownloading) {
            launchStatus.text = I18n.tr("launch.downloadingJava")
            return
        }
        if (kernel.authManager.loggingIn) {
            launchStatus.text = I18n.tr("account.loggingIn")
            return
        }
    }

Component.onCompleted: {
        updateSelection()
    }

    Connections {
        target: kernel.instanceManager
        function onSelectedChanged() {
            updateSelection()
        }
        function onInstancesChanged() {
            updateSelection()
        }
    }

    Connections {
        target: kernel.launchManager
        function onLaunchStarted() {
            launchStatus.text = I18n.tr("launch.active")
        }
        function onLaunchCompleted(exitCode) {
            launchStatus.text = I18n.tr("launch.exit").replace("%1", exitCode)
        }
        function onVerifyProgressChanged() {
            if (kernel.launchManager.verifying)
                updateStatus()
        }
        function onErrorOccurred(msg) {
            launchStatus.text = msg
        }
    }

    Connections {
        target: kernel
        function onJavaDownloadingChanged() {
            updateStatus()
        }
    }

    Connections {
        target: kernel.authManager
        function onLoggingInChanged() {
            updateStatus()
        }
    }

    Flickable {
        id: pageFlickable
        anchors.fill: parent
        clip: true
        contentHeight: contentColumn.implicitHeight + 48
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 16

        // Account cards
        Text {
            text: I18n.tr("account")
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
                        return Qt.alpha(Theme.primary, 0.12)
                    return maus.containsMouse ? Theme.surfaceContainerHigh : Theme.surfaceContainer
                }
                border.color: kernel.authManager.currentAccountIndex === index
                             ? Qt.alpha(Theme.primary, 0.3)
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
                        color: Qt.alpha(Theme.primary, 0.1)

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
                            color: Theme.primary
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
                            text: modelData.type === 2 ? I18n.tr("account.microsoft") : I18n.tr("account.offline")
                            font.pixelSize: 11
                            color: modelData.type === 2 ? Theme.primary : palette.placeholderText
                        }
                    }

                    // Current indicator / switch button
                    Button {
                        text: kernel.authManager.currentAccountIndex === index ? I18n.tr("account.current") : I18n.tr("account.switch")
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
            color: Qt.alpha(Theme.primary, 0.1)
            visible: kernel.authManager.loggingIn

            RowLayout {
                anchors.centerIn: parent
                spacing: 8

                BusyIndicator {
                    width: 18; height: 18
                    running: kernel.authManager.loggingIn
                }

                Text {
                    text: I18n.tr("account.loggingIn")
                    font.pixelSize: 13
                    color: Theme.primary
                }
            }
        }

        // Add accounts section
        Button {
            Layout.fillWidth: true
            text: I18n.tr("account.addMicrosoft")
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
                placeholderText: I18n.tr("account.offlinePlaceholder")
                text: "Player"
            }

            Button {
                text: I18n.tr("account.addOffline")
                font.weight: Font.Normal
                enabled: !kernel.authManager.loggingIn
                onClicked: kernel.authManager.addOfflineAccount(offlineNameField.text)
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

        // Selected instance
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            // Instance icon
            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                clip: true

Image {
                    id: launchIconImg
                    anchors.fill: parent
                    source: selectedIcon
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 44
                    sourceSize.height: 44
                    mipmap: true
                    smooth: true
                }
            }

            Column {
                Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter
                spacing: 4
                Text {
                    text: I18n.tr("currentInstance")
                    font.pixelSize: 13; color: palette.placeholderText
                }
Text {
                    id: instanceName
                    text: selectedVersion
                          || (kernel.instanceManager.instances.length === 0
                              ? I18n.tr("noInstances")
                              : I18n.tr("noInstance"))
                    font.pixelSize: 16; font.weight: Font.Medium
                    color: palette.text
                }
            }

            Button {
                text: I18n.tr("selectInstance")
                font.weight: Font.Normal
                onClicked: window.navigateToPage(4, I18n.tr("launch.manageInstances"))
            }
            Button {
                text: I18n.tr("instanceSettings")
                font.weight: Font.Normal
                onClicked: {
                    var sel = kernel.instanceManager.getSelectedInstance()
                    if (sel && sel.id)
                        window.navigateToPage(5, I18n.tr("launch.instanceSettings"))
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
            text: root.launchActive ? I18n.tr("cancel") : I18n.tr("launchGame")
            highlighted: true
            font.weight: Font.Normal
            enabled: root.launchActive || (selectedVersion !== "")
            onClicked: {
                if (root.launchActive) {
                    kernel.cancelLaunch()
                } else {
                    launchStatus.text = I18n.tr("launch.verifying")
                    kernel.launchGame(4096)
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
}
