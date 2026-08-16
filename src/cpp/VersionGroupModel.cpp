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
#include "VersionGroupModel.h"

#include <QSet>
#include <algorithm>

namespace {

// Compare "1.20.1" vs "1.19.2" numerically (used to order groups newest first).
int versionCompare(const QString &a, const QString &b)
{
    if (a == b)
        return 0;
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < pa.size() ? pa.at(i).toInt() : 0;
        const int y = i < pb.size() ? pb.at(i).toInt() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

// Primary group key for a version: its first non-empty gameVersion, else "其他".
QString primaryOf(const QVariantMap &version)
{
    const QStringList raw = version.value(QStringLiteral("gameVersions"))
                                .toString()
                                .split(QLatin1Char(','));
    for (const QString &p : raw) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            return t;
    }
    return QStringLiteral("其他");
}

} // namespace

VersionGroupModel::VersionGroupModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int VersionGroupModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant VersionGroupModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case TypeRole:
        return row.header ? QStringLiteral("header") : QStringLiteral("item");
    case PrimaryRole:
        return row.primary;
    case CountRole:
        return row.count;
    case VerRole:
        return row.ver;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> VersionGroupModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[TypeRole] = "type";
    roles[PrimaryRole] = "primary";
    roles[CountRole] = "count";
    roles[VerRole] = "ver";
    return roles;
}

int VersionGroupModel::listHeight() const
{
    int h = 12;
    for (const Row &row : m_rows)
        h += row.header ? 28 : 44;
    return qBound(52, h, 280);
}

void VersionGroupModel::setVersions(const QVariantList &versions)
{
    beginResetModel();
    m_rows.clear();
    m_collapsed.clear();
    m_groupVersions.clear();

    // Order group keys newest first (numeric compare).
    QStringList order;
    QSet<QString> seen;
    for (const QVariant &v : versions) {
        const QString primary = primaryOf(v.toMap());
        if (!seen.contains(primary)) {
            seen.insert(primary);
            order << primary;
        }
    }
    std::sort(order.begin(), order.end(),
              [](const QString &a, const QString &b) { return versionCompare(b, a) < 0; });

    for (const QString &primary : order) {
        QVariantList groupVersions;
        for (const QVariant &v : versions) {
            if (primaryOf(v.toMap()) == primary)
                groupVersions << v;
        }
        // Newest release date first.
        std::stable_sort(groupVersions.begin(), groupVersions.end(),
                         [](const QVariant &a, const QVariant &b) {
                             const QString da = a.toMap()
                                                    .value(QStringLiteral("releaseDate"))
                                                    .toString();
                             const QString db = b.toMap()
                                                    .value(QStringLiteral("releaseDate"))
                                                    .toString();
                             return da.localeAwareCompare(db) > 0;
                         });
        m_groupVersions.insert(primary, groupVersions);

        Row header;
        header.header = true;
        header.primary = primary;
        header.count = groupVersions.size();
        m_rows.append(header);

        for (const QVariant &v : groupVersions) {
            Row item;
            item.header = false;
            item.primary = primary;
            item.ver = v.toMap();
            m_rows.append(item);
        }
    }

    endResetModel();
    emit rowCountChanged();
}

bool VersionGroupModel::isCollapsed(const QString &primary) const
{
    return m_collapsed.value(primary, false);
}

int VersionGroupModel::indexOfHeader(const QString &primary) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).header && m_rows.at(i).primary == primary)
            return i;
    }
    return -1;
}

void VersionGroupModel::toggleGroup(const QString &primary)
{
    const int headerIdx = indexOfHeader(primary);
    if (headerIdx < 0)
        return;

    const bool nowCollapsed = !m_collapsed.value(primary, false);
    m_collapsed.insert(primary, nowCollapsed);

    if (nowCollapsed) {
        // Remove this group's version rows one at a time (header stays) so the
        // ListView's remove transition animates each row out.
        for (int i = m_rows.size() - 1; i >= headerIdx + 1; --i) {
            if (!m_rows.at(i).header && m_rows.at(i).primary == primary) {
                beginRemoveRows(QModelIndex(), i, i);
                m_rows.removeAt(i);
                endRemoveRows();
            }
        }
    } else {
        // Re-insert the group's rows right after its header so they animate in.
        const QVariantList groupVersions = m_groupVersions.value(primary);
        for (int n = 0; n < groupVersions.size(); ++n) {
            const int pos = headerIdx + 1 + n;
            Row item;
            item.header = false;
            item.primary = primary;
            item.ver = groupVersions.at(n).toMap();
            beginInsertRows(QModelIndex(), pos, pos);
            m_rows.insert(pos, item);
            endInsertRows();
        }
    }

    emit rowCountChanged();
}

QVariantMap VersionGroupModel::firstVersion() const
{
    for (const Row &row : m_rows) {
        if (!row.header && !row.ver.isEmpty())
            return row.ver;
    }
    return QVariantMap();
}