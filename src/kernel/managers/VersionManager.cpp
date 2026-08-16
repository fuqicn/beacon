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
#include "VersionManager.h"
#include <mc_log.h>
#include <mc_path.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QThread>
#include <memory>

VersionManager::VersionManager(QObject *parent) : QObject(parent)
{
    memset(&m_manifest, 0, sizeof(m_manifest));
    m_delayTimer.setSingleShot(true);
    connect(&m_delayTimer, &QTimer::timeout, this, [this]() {
        doFetchManifest(m_pendingMirror);
    });
}

VersionManager::~VersionManager()
{
    if (m_fetchThread && m_fetchThread->isRunning())
        m_fetchThread->wait(5000);
}

static QVariantMap versionEntryToMap(const McVersionEntry &e)
{
    QVariantMap m;
    m["id"] = QString::fromUtf8(e.id);
    m["type"] = QString::fromUtf8(e.type);
    m["releaseTime"] = QDateTime::fromSecsSinceEpoch(e.release_time).toString(Qt::ISODate);
    m["modifiedTime"] = QDateTime::fromSecsSinceEpoch(e.modified_time).toString(Qt::ISODate);
    m["releaseTimestamp"] = e.release_time;
    return m;
}

bool VersionManager::isAprilFools(const char *id, qint64 releaseTime) const
{
    // Hardcoded April Fools IDs
    static const char *foolsIds[] = {
        "2point0_blue", "2point0_red", "2point0_purple",
        "2.0_blue", "2.0_red", "2.0_purple", "2.0",
        "15w14a", "1.rv-pre1",
        "3d shareware v1.34",
        "20w14infinite", "20w14∞",
        "22w13oneblockatatime",
        "23w13a_or_b",
        "24w14potato",
        "25w14craftmine",
        "26w14a",
        nullptr
    };

    for (int i = 0; foolsIds[i]; ++i) {
        if (qstrcmp(id, foolsIds[i]) == 0)
            return true;
    }

    // Date-based detection: April 1
    if (releaseTime > 0) {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(releaseTime);
        if (dt.date().month() == 4 && dt.date().day() == 1)
            return true;
    }

    return false;
}

void VersionManager::fetchManifest(const QString &mirror)
{
    if (m_loading) return;
    m_pendingMirror = mirror;
    m_delayTimer.start(100);
}

void VersionManager::doFetchManifest(const QString &mirror)
{
    m_loading = true;
    emit loadingChanged();

    QThread *thread = QThread::create([this, mirror]() {
        int ret = 0;
        if (mirror.isEmpty())
            ret = mc_manifest_fetch(&m_manifest, 1);
        else
            ret = mc_manifest_fetch_mirror(&m_manifest, 1, mirror.toUtf8().constData());
        // mc_manifest_fetch_mirror already persists the fresh manifest to disk;
        // fall back to the on-disk cache when the network fetch fails.
        if (!ret)
            mc_manifest_load_cache(&m_manifest);
        QVariantList categories = buildCategories();
        QMetaObject::invokeMethod(this, [this, categories]() {
            m_cachedCategories = categories;
            m_categoriesDirty = false;
            m_loading = false;
            emit loadingChanged();
            emit manifestReady();
        }, Qt::QueuedConnection);
    });
    thread->setObjectName("manifestFetch");
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_fetchThread = thread;
    thread->start();
}

QVariantList VersionManager::listVersions(const QString &type) const
{
    QVariantList result;
    for (int i = 0; i < m_manifest.count; ++i) {
        const auto &e = m_manifest.entries[i];
        if (type != "all" && type != QString::fromUtf8(e.type))
            continue;
        result.append(versionEntryToMap(e));
    }
    return result;
}

QVariantList VersionManager::searchVersions(const QString &query) const
{
    McVersionEntry results[64];
    int count = mc_manifest_search(&m_manifest, query.toUtf8().constData(), results, 64);
    QVariantList list;
    for (int i = 0; i < count; ++i)
        list.append(versionEntryToMap(results[i]));
    return list;
}

QVariantList VersionManager::classifyVersions()
{
    if (!m_categoriesDirty && !m_cachedCategories.isEmpty())
        return m_cachedCategories;
    m_cachedCategories = buildCategories();
    m_categoriesDirty = false;
    return m_cachedCategories;
}

QVariantList VersionManager::buildCategories()
{
    QVariantList categories;

    QVariantList latest, releases, snapshots, old, fools;

    for (int i = 0; i < m_manifest.count; ++i) {
        const auto &e = m_manifest.entries[i];
        QVariantMap entry = versionEntryToMap(e);

        if (isAprilFools(e.id, e.release_time)) {
            fools.append(entry);
        } else if (qstrcmp(e.type, "release") == 0) {
            releases.append(entry);
        } else if (qstrcmp(e.type, "snapshot") == 0) {
            snapshots.append(entry);
        } else {
            old.append(entry);
        }
    }

    // Extract latest release/snapshot
    QString latestRel = latestRelease();
    QString latestSnap = latestSnapshot();
    for (int i = 0; i < releases.size(); ++i) {
        if (releases[i].toMap()["id"].toString() == latestRel) {
            latest.append(releases[i]);
            break;
        }
    }
    for (int i = 0; i < snapshots.size(); ++i) {
        if (snapshots[i].toMap()["id"].toString() == latestSnap) {
            latest.append(snapshots[i]);
            break;
        }
    }

    categories.append(QVariantMap{{"label", "最新版"}, {"versions", latest}});
    categories.append(QVariantMap{{"label", "正式版"}, {"versions", releases}});
    categories.append(QVariantMap{{"label", "快照版"}, {"versions", snapshots}});
    categories.append(QVariantMap{{"label", "远古版"}, {"versions", old}});
    categories.append(QVariantMap{{"label", "愚人节"}, {"versions", fools}});

    return categories;
}

void VersionManager::fetchVersionInfo(const QString &versionId, const QString &mirror)
{
    if (m_loading) return;
    m_loading = true;
    emit loadingChanged();

    McVersion *ver = new McVersion;
    mc_version_init(ver);

    QThread *thread = QThread::create([this, ver, versionId, mirror]() {
        int ret = 0;
        if (mirror.isEmpty())
            ret = mc_version_fetch_by_id(ver, versionId.toUtf8().constData());
        else
            ret = mc_version_fetch_by_id_mirror(ver, versionId.toUtf8().constData(),
                                                mirror.toUtf8().constData());
        QMetaObject::invokeMethod(this, [this, ver, ret, versionId]() {
            if (!ret) {
                emit errorOccurred(QString("Failed to fetch version info for %1").arg(versionId));
                mc_version_free(ver);
                delete ver;
                m_loading = false;
                emit loadingChanged();
                return;
            }

            QVariantMap info;
            info["id"] = QString::fromUtf8(ver->id);
            info["type"] = QString::fromUtf8(ver->type);
            info["mainClass"] = QString::fromUtf8(ver->main_class);
            info["inheritsFrom"] = QString::fromUtf8(ver->inherits_from);
            info["assets"] = QString::fromUtf8(ver->assets);
            info["javaMajorVersion"] = ver->java_major_version;
            info["clientSha1"] = QString::fromUtf8(ver->client_sha1);
            info["clientSize"] = static_cast<qlonglong>(ver->client_size);
            info["libraryCount"] = ver->library_count;

            QVariantList libs;
            for (int i = 0; i < ver->library_count; ++i) {
                QVariantMap lib;
                lib["name"] = QString::fromUtf8(ver->libraries[i].name);
                lib["path"] = QString::fromUtf8(ver->libraries[i].path);
                lib["url"] = QString::fromUtf8(ver->libraries[i].url);
                lib["sha1"] = QString::fromUtf8(ver->libraries[i].sha1);
                lib["size"] = static_cast<qlonglong>(ver->libraries[i].size);
                lib["isRequired"] = ver->libraries[i].is_required;
                libs.append(lib);
            }
            info["libraries"] = libs;

            info["assetIndexUrl"] = QString::fromUtf8(ver->asset_index.url);
            info["clientUrl"] = QString::fromUtf8(ver->client_url);

            mc_version_free(ver);
            delete ver;

            m_loading = false;
            emit loadingChanged();
            emit versionInfoReady(versionId, info);
        }, Qt::QueuedConnection);
    });
    thread->setObjectName("versionInfoFetch");
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_fetchThread = thread;
    thread->start();
}
