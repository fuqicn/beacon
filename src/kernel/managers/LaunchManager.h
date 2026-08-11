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
#ifndef LAUNCHMANAGER_H
#define LAUNCHMANAGER_H

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QElapsedTimer>
#include <QFile>
#include <mc_version.h>
#include <mc_auth.h>

class LaunchManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool verifying READ verifying NOTIFY verifyingChanged)
    Q_PROPERTY(qreal verifyProgress READ verifyProgress NOTIFY verifyProgressChanged)
    Q_PROPERTY(QString verifyTask READ verifyTask NOTIFY verifyProgressChanged)
    Q_PROPERTY(QString versionId READ versionId WRITE setVersionId NOTIFY versionChanged)
    Q_PROPERTY(QString javaPath READ javaPath WRITE setJavaPath NOTIFY javaChanged)
    Q_PROPERTY(QString mcDir READ mcDir WRITE setMcDir NOTIFY mcDirChanged)
    Q_PROPERTY(QString gameDir READ gameDir WRITE setGameDir NOTIFY gameDirChanged)
    Q_PROPERTY(int memory READ memory WRITE setMemory NOTIFY memoryChanged)

public:
    explicit LaunchManager(QObject *parent = nullptr);
    ~LaunchManager() override;

    bool running() const { return m_running; }
    bool ready() const { return m_ready; }
    bool verifying() const { return m_verifying; }
    qreal verifyProgress() const { return m_verifyProgress; }
    QString verifyTask() const { return m_verifyTask; }
    QString versionId() const { return m_versionId; }
    QString javaPath() const { return m_javaPath; }
    QString mcDir() const { return m_mcDir; }
    QString gameDir() const { return m_gameDir; }
    int memory() const { return m_memory; }

    void setVersionId(const QString &id);
    void setJavaPath(const QString &path);
    void setMemory(int mb);
    void setMcDir(const QString &dir);
    void setGameDir(const QString &dir);
    void setDir(const QString &dir) { m_mcDir = dir; }
    void setUsername(const QString &name) { m_username = name; }
    void setSession(const McAuthSession *session);
    void setExtraJvmArgs(const QString &args) { m_extraJvmArgs = args; }
    void setFullscreen(bool on) { m_fullscreen = on; }
    void setResolution(int w, int h) { m_resolutionW = w; m_resolutionH = h; }

    Q_INVOKABLE void launch();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setCancelRequested(bool v);
    bool cancelRequested() const { return m_cancelRequested; }

signals:
    void runningChanged();
    void readyChanged();
    void verifyingChanged();
    void verifyProgressChanged();
    void versionChanged();
    void javaChanged();
    void mcDirChanged();
    void gameDirChanged();
    void memoryChanged();
    void launchStarted();
    void launchCompleted(int exitCode);
    void logLine(const QString &line);
    void errorOccurred(const QString &message);

private:
    void doVerifyAndLaunch();
    void verifyClientJar();
    void verifyLibraries();
    void verifyAssets();
    void downloadMissingFiles();
    void doLaunch();

    void buildClasspath(McVersion *ver, const char *mcDir, QStringList &cp);
    void buildJvmArgs(McVersion *ver, const char *mcDir,
                      const QStringList &classpath, QStringList &args);
    void buildGameArgs(McVersion *ver, const char *mcDir,
                       const QStringList &jvmArgs, QStringList &gameArgs);

    bool abortVerifyIfCancelled();

    // Async verify state
    struct VerifyState {
        QStringList missingLibs;      // paths to download
        QStringList missingLibUrls;   // urls
        QStringList missingLibSha1s;  // sha1s
        QList<long> missingLibSizes;  // sizes
        QStringList missingAssets;    // paths
        QStringList missingAssetUrls;
        QStringList missingAssetSha1s;
        bool clientJarOk = false;
        bool clientJarExists = false;
        int totalLibs = 0;
        int okLibs = 0;
        int totalAssets = 0;
        int okAssets = 0;
        McVersion ver{};
        bool verLoaded = false;
    };
    VerifyState m_vs;

    QString m_versionId;
    QString m_javaPath;
    QString m_mcDir;
    QString m_gameDir;
    int m_memory = 4096;
    QString m_username = "Player";
    QString m_extraJvmArgs;
    bool m_fullscreen = false;
    int m_resolutionW = 0;
    int m_resolutionH = 0;
    McAuthSession m_session;
    bool m_running = false;
    bool m_ready = false;
    bool m_verifying = false;
    qreal m_verifyProgress = 0.0;
    QString m_verifyTask;
    bool m_cancelRequested = false;
    QProcess *m_process = nullptr;
    QFile *m_gameLog = nullptr;
};

#endif
