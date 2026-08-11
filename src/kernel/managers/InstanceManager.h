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
#ifndef INSTANCEMANAGER_H
#define INSTANCEMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>

struct InstanceInfo {
    QString id;
    QString name;
    QString type;       // release, snapshot, old_beta, old_alpha
    QString rootDir;    // root .minecraft directory
    QString verDir;     // version-specific directory
    QString jsonPath;
    QString jarPath;
    bool hasJar;
    int libraryCount;
    int javaMajorVersion;
    QString mainClass;
};

class InstanceManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList instances READ instances NOTIFY instancesChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectedChanged)
    Q_PROPERTY(QStringList rootDirs READ rootDirs NOTIFY rootDirsChanged)
    Q_PROPERTY(QString currentRootDir READ currentRootDir NOTIFY currentRootDirChanged)

public:
    explicit InstanceManager(QObject *parent = nullptr);
    ~InstanceManager() override;

    QVariantList instances() const;
    QString selectedId() const { return m_selectedId; }
    QStringList rootDirs() const { return m_rootDirs; }
    QString currentRootDir() const { return m_currentRootDir; }

    Q_INVOKABLE void scanInstances();
    Q_INVOKABLE void addRootDir(const QString &dir);
    Q_INVOKABLE void removeRootDir(const QString &dir);
    Q_INVOKABLE void removeInstance(const QString &id);
    Q_INVOKABLE void selectInstance(const QString &id);
    Q_INVOKABLE void setCurrentRootDir(const QString &dir);
    Q_INVOKABLE QVariantMap getSelectedInstance() const;
    Q_INVOKABLE void setDefaultRootDir(const QString &mcDir);
    Q_INVOKABLE QString copyInstance(const QString &id, const QString &newId = QString());
    Q_INVOKABLE bool renameInstance(const QString &id, const QString &newId);
    Q_INVOKABLE bool importVersionFolder(const QString &rootDir, const QString &srcPath);

signals:
    void instancesChanged();
    void selectedChanged();
    void rootDirsChanged();
    void currentRootDirChanged();

private:
    void loadConfig();
    void saveConfig();
    void scanDirectory(const QString &rootDir);

    QVariantList m_instances;
    QString m_selectedId;
    QStringList m_rootDirs;
    QString m_currentRootDir;
    QString m_configPath;
};

#endif
