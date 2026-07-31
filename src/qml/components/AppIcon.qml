import QtQuick
import QtQuick.Controls

Item {
    property string iconName: ""
    property int iconSize: 20

    width: iconSize
    height: iconSize

    Image {
        anchors.fill: parent
        source: {
            if (!iconName) return ""
            var c = palette.text
            return "image://tinted/" + iconName + "/"
                + Math.round(c.r * 255) + "/"
                + Math.round(c.g * 255) + "/"
                + Math.round(c.b * 255)
        }
        sourceSize.width: iconSize
        sourceSize.height: iconSize
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }
}
