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
#ifndef KERNELBRIDGE_H
#define KERNELBRIDGE_H

#include <QObject>
#include <QString>
#include <QDir>

#include "VersionManager.h"
#include "DownloadManager.h"
#include "AuthManager.h"
#include "JavaManager.h"
#include "ModManager.h"
#include "InstallManager.h"
#include "ModpackManager.h"
#include "LaunchManager.h"
#include "InstanceManager.h"
#include "SettingsManager.h"
#include "SkinManager.h"

class QQuickWindow;
class QQmlEngine;

class KernelBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(VersionManager *versionManager READ versionManager CONSTANT)
    Q_PROPERTY(DownloadManager *downloadManager READ downloadManager CONSTANT)
    Q_PROPERTY(AuthManager *authManager READ authManager CONSTANT)
    Q_PROPERTY(JavaManager *javaManager READ javaManager CONSTANT)
    Q_PROPERTY(ModManager *modManager READ modManager CONSTANT)
    Q_PROPERTY(InstallManager *installManager READ installManager CONSTANT)
    Q_PROPERTY(ModpackManager *modpackManager READ modpackManager CONSTANT)
    Q_PROPERTY(LaunchManager *launchManager READ launchManager CONSTANT)
    Q_PROPERTY(InstanceManager *instanceManager READ instanceManager CONSTANT)
    Q_PROPERTY(SettingsManager *settingsManager READ settingsManager CONSTANT)
    Q_PROPERTY(SkinManager *skinManager READ skinManager CONSTANT)
    Q_PROPERTY(QString mcDir READ mcDir CONSTANT)
    Q_PROPERTY(bool javaDownloading READ javaDownloading NOTIFY javaDownloadingChanged)

public:
    static KernelBridge *instance();
    static void initialize(const QString &lang = "zh", const QString &mcDir = QString());
    static void shutdown();

    VersionManager *versionManager() const { return m_versionManager; }
    DownloadManager *downloadManager() const { return m_downloadManager; }
    AuthManager *authManager() const { return m_authManager; }
    JavaManager *javaManager() const { return m_javaManager; }
    ModManager *modManager() const { return m_modManager; }
    InstallManager *installManager() const { return m_installManager; }
    ModpackManager *modpackManager() const { return m_modpackManager; }
    LaunchManager *launchManager() const { return m_launchManager; }
    InstanceManager *instanceManager() const { return m_instanceManager; }
    SettingsManager *settingsManager() const { return m_settingsManager; }
    SkinManager *skinManager() const { return m_skinManager; }

    QString mcDir() const { return m_mcDir; }
    void setMcDir(const QString &dir) { m_mcDir = dir; }
    void setMainWindow(QQuickWindow *window) { m_mainWindow = window; }
    void setEngine(QQmlEngine *engine) { m_engine = engine; }

    Q_INVOKABLE void setLanguage(const QString &lang);
    Q_INVOKABLE void setColorScheme(bool dark);
    Q_INVOKABLE void applyThemeMode(const QString &mode);
    Q_INVOKABLE void applyWindowTransparency(bool enabled);
    Q_INVOKABLE void launchGame(int memory = 4096);
    Q_INVOKABLE void cancelLaunch();
    Q_INVOKABLE void selectInstance(const QString &versionId, const QString &rootDir = QString());
    Q_INVOKABLE void setDownloadThreads(int n);
    Q_INVOKABLE void setDownloadSource(const QString &source);

    // Game directory for an instance: rootDir when version isolation is off,
    // otherwise rootDir/versions/<versionId> (PCL-style isolation). Old data
    // under rootDir/instances/<versionId> is migrated on first access.
    Q_INVOKABLE QString gameDirFor(const QString &rootDir, const QString &versionId) const;
    Q_INVOKABLE void setInstanceIsolation(const QString &versionId, bool override, bool enabled);
    // Effective isolation: instance override if set, otherwise the global policy
    // ("off" | "loader" | "all").
    Q_INVOKABLE bool instanceIsolationEffective(const QString &rootDir, const QString &versionId) const;
    // True when the version id names a mod loader (Fabric/Forge/NeoForge/Quilt/LiteLoader).
    Q_INVOKABLE bool isLoaderVersion(const QString &versionId) const;

    Q_INVOKABLE int getRequiredJavaVersion() const;
    Q_INVOKABLE bool isAnyMinecraftRunning() const;
    Q_INVOKABLE void killAllMinecraft();
    Q_INVOKABLE void qmlCollectGarbage();

    // Log viewer helpers
    Q_INVOKABLE QString readFileTail(const QString &path, int maxLines = 400) const;
    Q_INVOKABLE QVariantList diagnoseCrash(const QString &content) const;

    // File manager helpers
    Q_INVOKABLE QVariantList listDir(const QString &path) const;
    Q_INVOKABLE void revealInExplorer(const QString &path);

    bool javaDownloading() const { return m_javaDownloading; }
    void setJavaDownloading(bool v);

signals:
    void javaDownloadingChanged();
    void minecraftRunningChanged();

private:
    void doAuthAndLaunch();
    void onJavaReady();
    void onJavaDownloaded(const QString &path, int ver);

    int readRequiredJavaVersion(const QString &verId, const QString &mcDir) const;
    explicit KernelBridge(QObject *parent = nullptr);
    ~KernelBridge() override;

    static KernelBridge *s_instance;

    QString m_mcDir;
    int m_launchMemory = 4096;
    bool m_javaDownloading = false;
    bool m_launchCancelled = false;
    QQuickWindow *m_mainWindow = nullptr;
    QQmlEngine *m_engine = nullptr;
    VersionManager *m_versionManager = nullptr;
    DownloadManager *m_downloadManager = nullptr;
    AuthManager *m_authManager = nullptr;
    JavaManager *m_javaManager = nullptr;
    ModManager *m_modManager = nullptr;
    InstallManager *m_installManager = nullptr;
    ModpackManager *m_modpackManager = nullptr;
    LaunchManager *m_launchManager = nullptr;
    InstanceManager *m_instanceManager = nullptr;
    SettingsManager *m_settingsManager = nullptr;
    SkinManager *m_skinManager = nullptr;
};

#endif
