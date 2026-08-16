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
#ifndef VERSIONMANAGER_H
#define VERSIONMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <QTimer>

#include <mc_manifest.h>
#include <mc_version.h>

class QThread;

class VersionManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString latestRelease READ latestRelease NOTIFY manifestReady)
    Q_PROPERTY(QString latestSnapshot READ latestSnapshot NOTIFY manifestReady)
    Q_PROPERTY(int versionCount READ versionCount NOTIFY manifestReady)

public:
    explicit VersionManager(QObject *parent = nullptr);
    ~VersionManager() override;

    bool loading() const { return m_loading; }
    QString latestRelease() const { return QString::fromUtf8(m_manifest.latest_release); }
    QString latestSnapshot() const { return QString::fromUtf8(m_manifest.latest_snapshot); }
    int versionCount() const { return m_manifest.count; }

    Q_INVOKABLE void fetchManifest(const QString &mirror = QString());
    Q_INVOKABLE QVariantList listVersions(const QString &type = "all") const;
    Q_INVOKABLE QVariantList searchVersions(const QString &query) const;
    Q_INVOKABLE void fetchVersionInfo(const QString &versionId, const QString &mirror = QString());
    Q_INVOKABLE QVariantList classifyVersions();

signals:
    void manifestReady();
    void versionInfoReady(const QString &versionId, const QVariantMap &info);
    void loadingChanged();
    void errorOccurred(const QString &message);

private:
    void doFetchManifest(const QString &mirror);
    bool isAprilFools(const char *id, qint64 releaseTime) const;
    QVariantList buildCategories();
    McManifest m_manifest;
    bool m_loading = false;
    QTimer m_delayTimer;
    QString m_pendingMirror;
    QVariantList m_cachedCategories;
    bool m_categoriesDirty = true;
    QThread *m_fetchThread = nullptr;
};

#endif
