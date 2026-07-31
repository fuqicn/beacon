import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root

    Component.onCompleted: {
        javaPathInput.text = kernel.settingsManager.value("java/path", "")
        memorySetting.value = kernel.settingsManager.value("java/memory", 4096)
        langCombo.currentIndex = kernel.settingsManager.value("language/index", 0)
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
                color: Qt.alpha(palette.placeholderText, 0.05)
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
                color: Qt.alpha(palette.placeholderText, 0.05)
                    RowLayout {
                        id: memInner
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        Text { text: "最大内存"; color: palette.placeholderText; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                        SpinBox {
                            id: memorySetting
                            from: 512; to: 65536; stepSize: 512
                            value: 4096
                            onValueChanged: kernel.settingsManager.setValue("java/memory", value)
                        }
                        Text { text: "MB"; color: palette.placeholderText; font.pixelSize: 14 }
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
                color: Qt.alpha(palette.placeholderText, 0.05)
                RowLayout {
                    id: themeInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Text {
                        text: Theme.themeMode === "system" ? "主题（跟随系统）"
                              : (Theme.darkMode ? "深色模式" : "浅色模式")
                        color: palette.placeholderText
                        font.pixelSize: 14
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "跟随系统"
                        color: palette.placeholderText
                        font.pixelSize: 13
                    }
                    Switch {
                        id: themeFollowSwitch
                        checked: Theme.themeMode === "system"
                        onToggled: {
                            if (checked)
                                Theme.setThemeMode("system")
                            else
                                Theme.setThemeMode(Theme.darkMode ? "dark" : "light")
                        }
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
                color: Qt.alpha(palette.placeholderText, 0.05)
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
                color: Qt.alpha(palette.placeholderText, 0.05)
                RowLayout {
                    id: langInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Text { text: "界面语言"; color: palette.placeholderText; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: langCombo
                        model: ["中文", "English", "日本語", "Français"]
                        Layout.preferredWidth: 120
                        onCurrentIndexChanged: {
                            kernel.settingsManager.setValue("language/index", currentIndex)
                        }
                    }
                }
            }

            // About
            Text {
                text: "关于"
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: aboutInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Qt.alpha(palette.placeholderText, 0.05)
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
