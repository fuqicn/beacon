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
#ifndef JAVAMANAGER_H
#define JAVAMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <mc_java.h>
#include <mc_java_dl.h>

class QThread;

class JavaManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList runtimes READ runtimes NOTIFY runtimesChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QString runtimeDir READ runtimeDir WRITE setRuntimeDir NOTIFY runtimeDirChanged)

public:
    explicit JavaManager(QObject *parent = nullptr);
    ~JavaManager() override;

    QVariantList runtimes() const { return m_runtimes; }
    bool searching() const { return m_searching; }
    QString runtimeDir() const { return m_runtimeDir; }
    void setRuntimeDir(const QString &dir);

    Q_INVOKABLE void findJava();
    Q_INVOKABLE QVariantMap findBest() const;
    Q_INVOKABLE void findJavaAsync();
    Q_INVOKABLE void downloadJava(int majorVersion);

signals:
    void runtimesChanged();
    void searchingChanged();
    void runtimeDirChanged();
    void javaDownloaded(const QString &path, int majorVersion);
    void javaDownloadFailed(int majorVersion);
    void errorOccurred(const QString &message);

private:
    void doFindJava();
    void addBundledJava();

    QVariantList m_runtimes;
    bool m_searching = false;
    QString m_runtimeDir;
    QThread *m_workerThread = nullptr;
};

#endif
