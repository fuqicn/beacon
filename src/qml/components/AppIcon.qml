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
        smooth: true
        mipmap: true
        asynchronous: true
    }
}
