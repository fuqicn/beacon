import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root

    property string instanceId: ""
    property var instanceData: ({})

    function loadInstance() {
        var sel = kernel.instanceManager.getSelectedInstance()
        if (sel && sel.id) {
            instanceId = sel.id
            instanceData = sel
        }
    }

    Component.onCompleted: loadInstance()
    Connections {
        target: kernel.instanceManager
        function onSelectedChanged() { loadInstance() }
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
                    text: "\u2190  返回"
                    flat: true
                    font.weight: Font.Normal
                    onClicked: window.navigateToPage(4, "实例管理")
                }

                Text {
                    text: instanceData.id || "未选择"
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

            // Version isolation toggle
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: inner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: isoHover.hovered ? Qt.alpha(palette.highlight, 0.08) : Qt.alpha(palette.placeholderText, 0.05)
                HoverHandler { id: isoHover }

                RowLayout {
                    id: inner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    ColumnLayout {
                        spacing: 2
                        Text {
                            text: "版本隔离"
                            font.pixelSize: 14
                            color: palette.text
                        }
                        Text {
                            text: "独立运行配置，互不干扰"
                            font.pixelSize: 11
                            color: palette.placeholderText
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Switch {
                        checked: false
                    }
                }
            }

            // JVM arguments
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: jvmCol.implicitHeight + 32
                radius: Theme.shapeMedium
                color: jvmHover.hovered ? Qt.alpha(palette.highlight, 0.08) : Qt.alpha(palette.placeholderText, 0.05)
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
                                text: "单独 JVM 参数"
                                font.pixelSize: 14
                                color: palette.text
                            }
                            Text {
                                text: "为此实例设置独立的 Java 参数"
                                font.pixelSize: 11
                                color: palette.placeholderText
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Switch {
                            id: jvmSwitch
                            checked: false
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        visible: jvmSwitch.checked
                        placeholderText: "-Xmx2G -XX:+UseG1GC"
                        font.family: "Consolas, monospace"
                        font.pixelSize: 13
                    }
                }
            }

            // Version folder
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: verFolderInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: vfHover.hovered ? Qt.alpha(palette.highlight, 0.08) : Qt.alpha(palette.placeholderText, 0.05)
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
                            text: "版本文件夹"
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
                        text: "打开"
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
                color: savesHover.hovered ? Qt.alpha(palette.highlight, 0.08) : Qt.alpha(palette.placeholderText, 0.05)
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
                            text: "存档文件夹"
                            font.pixelSize: 14
                            color: palette.text
                        }
                        Text {
                            text: (instanceData.rootDir || "") + "/saves"
                            font.pixelSize: 11
                            color: palette.placeholderText
                            elide: Text.ElideLeft
                            Layout.fillWidth: true
                        }
                    }

                    Button {
                        text: "打开"
                        font.weight: Font.Normal
                        onClicked: {
                            if (instanceData.rootDir)
                                Qt.openUrlExternally("file:///" + instanceData.rootDir.replace(/\\/g, "/") + "/saves")
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
                            text: "删除实例"
                            font.pixelSize: 14
                            color: "#F44336"
                        }
                        Text {
                            text: "将永久删除此实例的所有文件"
                            font.pixelSize: 11
                            color: Qt.alpha("#F44336", 0.7)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "删除"
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

        ScrollBar.vertical: ScrollBar { }
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
                text: "删除实例"
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
            }

            Text {
                text: "确认删除 \"" + (instanceData.id || "") + "\"？将永久删除版本文件夹的所有文件，此操作不可恢复。"
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
                        kernel.instanceManager.removeInstance(instanceId)
                        deleteConfirmPopup.close()
                        window.navigateToPage(4, "实例管理")
                    }
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            width: 8
        }
    }
}