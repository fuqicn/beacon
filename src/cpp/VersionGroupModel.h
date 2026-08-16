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
#ifndef VERSIONGROUPMODEL_H
#define VERSIONGROUPMODEL_H

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QStringList>

// Groups a flat list of version entries into collapsible "Minecraft x.y.z"
// sections (PCL-style), mirroring the grouping/ordering/collapse logic that
// used to live in the QML detail pages. Each row is either a header row or a
// version row:
//   header: { type: "header", primary: <mc version>, count: <n> }
//   item:   { type: "item",   primary: <mc version>, ver: <version map> }
// The model performs fine-grained insert/remove so a ListView with add/remove
// transitions can animate collapsing/expanding a group.
class VersionGroupModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int rowCount READ rowCount NOTIFY rowCountChanged)
    Q_PROPERTY(int listHeight READ listHeight NOTIFY rowCountChanged)

public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        PrimaryRole,
        CountRole,
        VerRole
    };

    explicit VersionGroupModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int listHeight() const;

    // Rebuild the model from a fresh list of version maps (also clears
    // collapse state).
    Q_INVOKABLE void setVersions(const QVariantList &versions);
    // True if the group for this primary version is currently collapsed.
    Q_INVOKABLE bool isCollapsed(const QString &primary) const;
    // Toggle the group and animate the rows in/out.
    Q_INVOKABLE void toggleGroup(const QString &primary);
    // First visible version map, or an empty map if there are none.
    Q_INVOKABLE QVariantMap firstVersion() const;

signals:
    void rowCountChanged();

private:
    struct Row {
        bool header = false;
        QString primary;
        int count = 0;
        QVariantMap ver;
    };

    int indexOfHeader(const QString &primary) const;
    static QStringList primaryList(const QVariantList &versions);
    static void sortPrimaryList(QStringList &order);
    static void sortVersions(QVariantList &list);

    QList<Row> m_rows;
    QHash<QString, bool> m_collapsed;
    QHash<QString, QVariantList> m_groupVersions;
};

#endif
