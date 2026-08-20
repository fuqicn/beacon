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

    property int hoveredIndex: -1

    FolderDialog {
        id: folderDialog
        currentFolder: "file:///" + (kernel.instanceManager.currentRootDir || kernel.mcDir)
        onAccepted: {
            if (selectedFolder)
                kernel.instanceManager.addRootDir(selectedFolder.toString().replace(/^file:\/\//, ""))
        }
    }

    FileDialog {
        id: importDialog
        title: "导入整合包(.mrpack)"
        nameFilters: ["Modrinth 整合包(*.mrpack)", "所有文件(*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            var src = selectedFile.toString().replace(/^file:\/\//, "")
            kernel.modpackManager.installFromFile(
                src, kernel.instanceManager.currentRootDir)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: "\u2190  " + I18n.tr("back")
                flat: true
                font.weight: Font.Normal
                onClicked: window.navigateToPage(0, "")
            }

            Text {
                text: I18n.tr("instanceManage")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
                Layout.fillWidth: true
            }

            Rectangle {
                width: 32; height: 32; radius: Theme.shapeSmall
                color: refHover.hovered ? Qt.alpha(Theme.primary, 0.15) : "transparent"
                AppIcon { anchors.centerIn: parent; iconName: "refresh"; iconSize: 16 }
                HoverHandler { id: refHover }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: kernel.instanceManager.scanInstances()
                }
            }

            Button {
                text: I18n.tr("instances.addFolder")
                font.weight: Font.Normal
                onClicked: folderDialog.open()
            }

            Button {
                text: I18n.tr("instances.import")
                font.weight: Font.Normal
                onClicked: importDialog.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: palette.mid
            opacity: 0.3
        }

        // Root directories
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: I18n.tr("instances.scanDirs")
                font.pixelSize: 13
                color: palette.placeholderText
            }

            Repeater {
                model: kernel.instanceManager.rootDirs
                delegate: Rectangle {
                    id: dirChip
                    Layout.preferredWidth: rootDirText.implicitWidth + 24
                    Layout.preferredHeight: 28
                    radius: Theme.shapeLarge
                    color: modelData === kernel.instanceManager.currentRootDir
                           ? Qt.alpha(Theme.primary, 0.18)
                           : Qt.alpha(Theme.primary, 0.1)

                    Row {
                        anchors.centerIn: parent
                        spacing: 6

                        Text {
                            id: rootDirText
                            text: modelData.split("/").pop() || modelData
                            font.pixelSize: 12
                            font.weight: modelData === kernel.instanceManager.currentRootDir ? Font.DemiBold : Font.Normal
                            color: Theme.primary
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        AppIcon {
                            iconName: "xmark"
                            iconSize: 10

                            anchors.verticalCenter: parent.verticalCenter

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: kernel.instanceManager.removeRootDir(modelData)
                            }
                        }
                    }

                    ToolTip {
                        visible: maDir.containsMouse
                        text: modelData
                        delay: 300
                    }

                    MouseArea {
                        id: maDir
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: kernel.instanceManager.setCurrentRootDir(modelData)
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // Instance count
        Text {
            text: kernel.instanceManager.instances.length + " " + I18n.tr("instances.count")
            font.pixelSize: 13
            color: palette.placeholderText
        }

        // Instance list
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: kernel.instanceManager.instances
            spacing: 8

            ScrollBar.vertical: OverlayScrollBar {
                followAlways: true
                policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                width: 8
            }

            delegate: Rectangle {
                width: listView.width
                height: 72
                radius: Theme.shapeLarge
                color: modelData.id === kernel.instanceManager.selectedId
                       ? Qt.alpha(Theme.primary, 0.12)
                       : (mainHover.containsMouse ? Theme.surfaceContainerHigh : Theme.surfaceContainer)
                border.color: modelData.id === kernel.instanceManager.selectedId
                              ? Qt.alpha(Theme.primary, 0.3)
                              : "transparent"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    // Instance icon (real image; PCL-style block/loader icon or
                    // a custom/modpack cover saved alongside the version)
                    Rectangle {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        radius: Theme.shapeMedium
                        color: Theme.surfaceContainer
                        clip: true

                        Image {
                            id: iconImg
                            anchors.fill: parent
                            source: modelData.customIcon
                                    ? "file:///" + modelData.customIcon.replace(/\\/g, "/")
                                    : "qrc:/icons/instances/" + (modelData.iconKey || "grass") + ".png"
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 48
                            sourceSize.height: 48
                            mipmap: true
                            smooth: true
                        }
                    }

                    // Instance info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: modelData.id || "Unknown"
                            font.pixelSize: 16
                            font.weight: Font.Medium
                            color: palette.text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                text: {
                                    var t = modelData.type || "release"
                                    var k = "type." + t
                                    if (t === "release") return I18n.tr("type.release")
                                    if (t === "snapshot") return I18n.tr("type.snapshot")
                                    if (t === "old_beta") return I18n.tr("type.old_beta")
                                    if (t === "old_alpha") return I18n.tr("type.old_alpha")
                                    return t
                                }
                                font.pixelSize: 12
                                color: palette.placeholderText
                            }

                            Text {
                                text: modelData.libraryCount + " 库"
                                font.pixelSize: 12
                                color: palette.placeholderText
                            }

                            Text {
                                text: "Java " + modelData.javaMajorVersion
                                font.pixelSize: 12
                                color: palette.placeholderText
                            }

                            AppIcon {
                                iconName: modelData.hasJar ? "check" : "xmark"
                                iconSize: 12
                            }
                        }
                    }
                }

                // Click overlay (main area, not buttons)
                MouseArea {
                    id: mainClickArea
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.leftMargin: 16
                    anchors.topMargin: 16
                    anchors.bottomMargin: 16
                    anchors.rightMargin: 80
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            kernel.selectInstance(modelData.id, modelData.rootDir)
                            window.navigateToPage(5, "实例设置")
                        } else {
                            kernel.selectInstance(modelData.id, modelData.rootDir)
                            window.navigateTo(0)
                        }
                    }
                }

                // Background hover detector (main area only)
                MouseArea {
                    id: mainHover
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.rightMargin: 88
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: true
                }

                // Hover action buttons
                Row {
                    id: hoverButtons
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 16
                    spacing: 8
                    opacity: root.hoveredIndex === index ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 150 } }

                    Rectangle {
                        width: 32; height: 32; radius: Theme.shapeSmall
                        HoverHandler { id: gearHover }
                        color: gearHover.hovered ? Qt.alpha(Theme.primary, 0.15) : "transparent"

                        AppIcon {
                            anchors.centerIn: parent
                            iconName: "gear"
                            iconSize: 16
                        }

                        MouseArea {
                            id: ma1
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                kernel.selectInstance(modelData.id, modelData.rootDir)
window.navigateToPage(5, "实例设置")
                            }
                        }
                    }

                    Rectangle {
                        width: 32; height: 32; radius: Theme.shapeSmall
                        HoverHandler { id: trashHover }
                        color: trashHover.hovered ? Qt.alpha("#F44336", 0.15) : "transparent"

                        AppIcon {
                            anchors.centerIn: parent
                            iconName: "trash"
                            iconSize: 16
                        }

                        MouseArea {
                            id: ma2
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                deleteConfirmPopup.instanceId = modelData.id
                                deleteConfirmPopup.instanceName = modelData.id
                                deleteConfirmPopup.open()
                            }
                        }
                    }
                }

                // Hover overlay (full-size — keeps buttons visible)
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: true
                    z: 99
                    onEntered: root.hoveredIndex = index
                    onExited: root.hoveredIndex = -1
                }
            }
        }

        // Empty state
        Text {
            Layout.fillWidth: true
            text: I18n.tr("instances.empty")
            font.pixelSize: 14
            color: palette.placeholderText
            horizontalAlignment: Text.AlignHCenter
            visible: kernel.instanceManager.instances.length === 0
        }
    }

    // Delete confirmation dialog
    Popup {
        id: deleteConfirmPopup

        property string instanceId: ""
        property string instanceName: ""

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
                text: I18n.tr("instances.deleteTitle")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
            }

            Text {
                text: I18n.tr("instances.deleteConfirm").replace("%1", deleteConfirmPopup.instanceName)
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
                    text: I18n.tr("cancel")
                    font.weight: Font.Normal
                    onClicked: deleteConfirmPopup.close()
                }

                Button {
                    text: I18n.tr("delete")
                    font.weight: Font.Normal
                    highlighted: true
                    onClicked: {
                        kernel.instanceManager.removeInstance(deleteConfirmPopup.instanceId)
                        deleteConfirmPopup.close()
                    }
                }
            }
        }
    }

    // Name input dialog (copy / rename)
    Popup {
        id: namePopup

        property string instanceId: ""
        property string titleText: ""
        property string okText: ""
        property string action: "copy"

        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width - 64, 420)
        padding: 24

        onOpened: nameField.forceActiveFocus()

        background: Rectangle {
            radius: Theme.shapeLarge
            color: palette.window
            border.color: palette.mid
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 20

            Text {
                text: namePopup.titleText
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
            }

            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: I18n.tr("instances.newName")
                selectByMouse: true
                onAccepted: nameOkButton.clicked()
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Layout.alignment: Qt.AlignRight

                Button {
                    text: I18n.tr("cancel")
                    font.weight: Font.Normal
                    onClicked: namePopup.close()
                }

                Button {
                    id: nameOkButton
                    text: namePopup.okText
                    font.weight: Font.Normal
                    highlighted: true
                    onClicked: {
                        var newName = nameField.text.trim()
                        if (newName.length === 0) return
                        if (namePopup.action === "copy")
                            kernel.instanceManager.copyInstance(namePopup.instanceId, newName)
                        else
                            kernel.instanceManager.renameInstance(namePopup.instanceId, newName)
                        namePopup.close()
                    }
                }
            }
        }
    }

}