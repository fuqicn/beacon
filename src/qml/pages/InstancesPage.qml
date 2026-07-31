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
        currentFolder: "file:///" + kernel.mcDir
        onAccepted: {
            if (selectedFolder)
                kernel.instanceManager.addRootDir(selectedFolder)
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
                text: "\u2190  返回"
                flat: true
                font.weight: Font.Normal
                onClicked: window.navigateToPage(0, "")
            }

            Text {
                text: "实例管理"
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
                Layout.fillWidth: true
            }

            Rectangle {
                width: 32; height: 32; radius: Theme.shapeSmall
                color: refHover.hovered ? Qt.alpha(palette.highlight, 0.15) : "transparent"
                AppIcon { anchors.centerIn: parent; iconName: "refresh"; iconSize: 16 }
                HoverHandler { id: refHover }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: kernel.instanceManager.scanInstances()
                }
            }

            Button {
                text: "添加文件夹"
                font.weight: Font.Normal
                onClicked: folderDialog.open()
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
                text: "扫描目录:"
                font.pixelSize: 13
                color: palette.placeholderText
            }

            Repeater {
                model: kernel.instanceManager.rootDirs
                delegate: Rectangle {
                    Layout.preferredWidth: rootDirText.implicitWidth + 24
                    Layout.preferredHeight: 28
                    radius: Theme.shapeLarge
                    color: Qt.alpha(palette.highlight, 0.1)

                    Row {
                        anchors.centerIn: parent
                        spacing: 6

                        Text {
                            id: rootDirText
                            text: modelData.split("/").pop() || modelData
                            font.pixelSize: 12
                            color: palette.highlight
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
                        acceptedButtons: Qt.NoButton
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // Instance count
        Text {
            text: kernel.instanceManager.instances.length + " 个实例"
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

            ScrollBar.vertical: ScrollBar {
                policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                width: 8
            }

            delegate: Rectangle {
                width: listView.width
                height: 72
                radius: Theme.shapeLarge
                color: modelData.id === kernel.instanceManager.selectedId
                       ? Qt.alpha(palette.highlight, 0.12)
                       : (mainHover.containsMouse ? Qt.alpha(palette.placeholderText, 0.08) : Qt.alpha(palette.placeholderText, 0.05))
                border.color: modelData.id === kernel.instanceManager.selectedId
                              ? Qt.alpha(palette.highlight, 0.3)
                              : "transparent"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    // Version type icon
                    Rectangle {
                        width: 48; height: 48; radius: Theme.shapeMedium
                        color: {
                            var t = modelData.type || "release"
                            if (t === "release") return Qt.alpha(palette.highlight, 0.15)
                            if (t === "snapshot") return Qt.alpha("#FFA050", 0.15)
                            return Qt.alpha(palette.placeholderText, 0.1)
                        }

                        AppIcon {
                            anchors.centerIn: parent
                            iconName: {
                                var t = modelData.type || "release"
                                if (t === "release") return "play"
                                if (t === "snapshot") return "star"
                                return "xmark"
                            }
                            iconSize: 18
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
                                    if (t === "release") return "正式版"
                                    if (t === "snapshot") return "快照"
                                    if (t === "old_beta") return "旧测试版"
                                    if (t === "old_alpha") return "旧版本"
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
                        color: gearHover.hovered ? Qt.alpha(palette.highlight, 0.15) : "transparent"

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
            text: "未找到实例\n请确保目录下存在 versions 文件夹且包含有效的版本 JSON"
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
                text: "删除实例"
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
            }

            Text {
                text: "确认删除 \"" + deleteConfirmPopup.instanceName + "\"？将永久删除版本文件夹的所有文件，此操作不可恢复。"
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
                    text: "取消"
                    font.weight: Font.Normal
                    onClicked: deleteConfirmPopup.close()
                }

                Button {
                    text: "删除"
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

}