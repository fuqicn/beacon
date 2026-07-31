import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root

    property var categories: []
    property var filteredVersions: []
    property int currentCategory: 0
    property bool loading: false

    function selectCategory(index) {
        currentCategory = index
        if (index >= 0 && index < root.categories.length) {
            filteredVersions = root.categories[index].versions || []
        } else {
            filteredVersions = []
        }
    }

    Connections {
        target: kernel.versionManager
        function onManifestReady() {
            loading = false
            root.categories = kernel.versionManager.classifyVersions()
            categoryTabs.model = root.categories
            selectCategory(0)
        }
    }

    Component.onCompleted: {
        loading = true
        if (kernel.versionManager.versionCount > 0) {
            root.categories = kernel.versionManager.classifyVersions()
            categoryTabs.model = root.categories
            selectCategory(0)
            loading = false
        } else {
            kernel.versionManager.fetchManifest()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: "下载"
            font.pixelSize: 22
            font.weight: Font.Bold
            color: palette.text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                id: categoryTabs
                delegate: Rectangle {
                    id: tabBg
                    width: implicitLabel.implicitWidth + 32
                    height: 36
                    radius: Theme.shapeExtraLarge
                    color: index === currentCategory
                           ? Qt.alpha(palette.highlight, 0.15)
                           : "transparent"

                    Text {
                        id: implicitLabel
                        anchors.centerIn: parent
                        text: modelData.label + " (" + (modelData.versions ? modelData.versions.length : 0) + ")"
                        font.pixelSize: 13
                        font.weight: index === currentCategory ? Font.Medium : Font.Normal
                        color: index === currentCategory ? palette.highlight : palette.placeholderText
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: selectCategory(index)
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

        Rectangle {
            id: loadingContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            visible: root.loading

            BusyIndicator {
                anchors.centerIn: parent
                running: parent.visible
                implicitWidth: 40
                implicitHeight: 40
            }
        }

        Rectangle {
            id: listContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.shapeMedium
            color: Qt.alpha(palette.placeholderText, 0.05)
            clip: true
            visible: !loading

            opacity: root.loading ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: 150 } }

            ListView {
                id: versionListView
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                model: filteredVersions
                clip: true

                ScrollBar.vertical: ScrollBar {
                    policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                    width: 8
                }

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                    NumberAnimation { property: "scale"; from: isWin11 ? 0.92 : 1; to: 1; duration: 200 }
                }
                populate: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                }

                delegate: Rectangle {
                    id: listDelegate
                    width: versionListView.width
                    height: 48
                    radius: Theme.shapeSmall
                    color: mouseArea.containsMouse
                           ? Qt.alpha(palette.highlight, 0.08)
                           : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12

                        Column {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 2

                            Text {
                                text: modelData.id || ""
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: palette.text
                                elide: Text.ElideRight
                            }

                            Text {
                                text: modelData.type || ""
                                font.pixelSize: 11
                                color: palette.placeholderText
                            }
                        }

                        Text {
                            text: {
                                if (modelData.id === kernel.versionManager.latestRelease) return "最新正式版"
                                if (modelData.id === kernel.versionManager.latestSnapshot) return "最新快照版"
                                return ""
                            }
                            font.pixelSize: 11
                            color: palette.highlight
                            visible: text !== ""
                        }
                    }

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            downloadDialog.versionId = modelData.id
                            downloadDialog.versionType = modelData.type
                            downloadDialog.open()
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: "该分类暂无版本"
                font.pixelSize: 14
                color: palette.placeholderText
                visible: !loading && filteredVersions.length === 0
            }
        }
    }

    DownloadDialog {
        id: downloadDialog
    }
}
