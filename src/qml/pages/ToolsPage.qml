import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root

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
            spacing: 16

            Text {
                text: "工具"
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Flow {
                Layout.fillWidth: true
                spacing: 16

                Repeater {
                model: [
                    { name: "Mod 搜索", desc: "在 Modrinth 上搜索 Mod", icon: "magnifying-glass" },
                    { name: "Java 管理", desc: "扫描并下载 Java 运行时", icon: "coffee" },
                    { name: "文件管理", desc: "浏览 .minecraft 目录", icon: "folder" },
                    { name: "日志查看", desc: "查看启动日志", icon: "file-lines" }
                ]

                Frame {
                    width: 220; height: 120

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter

                        AppIcon {
                            iconName: modelData.icon
                            iconSize: 24
                        }
                            Text {
                                text: modelData.name
                                font.pixelSize: 15; font.weight: Font.Medium
                                color: palette.text
                                width: parent.width
                                elide: Text.ElideRight
                            }
                            Text {
                                text: modelData.desc
                                font.pixelSize: 12
                                color: palette.placeholderText
                                wrapMode: Text.WordWrap
                                width: parent.width
                            }
                        }
                    }
                }
            }

            // Download Java section
            Text {
                text: "下载 Java"
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
                Layout.topMargin: 24
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: javaDlInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Qt.alpha(palette.placeholderText, 0.05)
                RowLayout {
                    id: javaDlInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Text { text: "Java 版本"; color: palette.placeholderText; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: javaVerCombo
                        model: ["8", "11", "16", "17", "21", "25"]
                        currentIndex: 2
                    }
                    Button {
                        text: kernel.downloadManager.busy ? "下载中..." : "下载"
                        enabled: !kernel.downloadManager.busy
                        highlighted: true
                        font.weight: Font.Normal
                        onClicked: {
                            var ver = parseInt(javaVerCombo.currentText)
                            kernel.downloadManager.downloadJava(ver, kernel.launchManager.dir || "")
                        }
                    }
                }
            }

            Text {
                text: kernel.downloadManager.currentTask
                font.pixelSize: 12; color: palette.placeholderText
                visible: kernel.downloadManager.busy
            }

            ProgressBar {
                visible: kernel.downloadManager.busy
                from: 0; to: kernel.downloadManager.totalFiles
                value: kernel.downloadManager.completedFiles
                Layout.fillWidth: true
            }

            Text {
                id: javaStatusText
                font.pixelSize: 12; color: palette.placeholderText
                visible: text.length > 0
            }

            Item { Layout.fillHeight: true }
        }

        ScrollBar.vertical: ScrollBar {
            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            width: 8
        }
    }

    Connections {
        target: kernel.downloadManager
        function onJavaDownloaded(majorVersion, javaPath) {
            kernel.javaManager.addBundledJava();
            javaStatusText.text = "Java " + majorVersion + " 下载完成"
        }
        function onErrorOccurred(message) {
            if (message.indexOf("Java") >= 0)
                javaStatusText.text = message
        }
    }
}
