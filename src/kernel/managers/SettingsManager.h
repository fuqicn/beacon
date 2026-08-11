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
#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QVariant>
#include <QVariantMap>

class SettingsManager : public QObject
{
    Q_OBJECT
public:
    explicit SettingsManager(QObject *parent = nullptr);

    void setLauncherDir(const QString &dir);

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void beginGroup(const QString &prefix);
    Q_INVOKABLE void endGroup();
    Q_INVOKABLE void remove(const QString &key);
    Q_INVOKABLE bool contains(const QString &key) const;

    Q_INVOKABLE QVariantMap detectSystemSettings() const;

    Q_INVOKABLE void setInstance(const QString &instanceId);
    Q_INVOKABLE void endInstance();

private:
    QSettings *m_launcherSettings = nullptr;
    QSettings *m_instanceSettings = nullptr;
    QSettings *m_active = nullptr;
    QString m_launcherDir;
};

#endif
