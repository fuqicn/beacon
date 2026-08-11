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
#ifndef SKINMANAGER_H
#define SKINMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QDir>

class SkinManager : public QObject
{
    Q_OBJECT
public:
    explicit SkinManager(QObject *parent = nullptr);

    void setCacheDir(const QString &dir);

    Q_INVOKABLE void fetchSkin(const QString &uuid);
    Q_INVOKABLE QString cachedSkin(const QString &uuid) const;
    Q_INVOKABLE bool hasCachedSkin(const QString &uuid) const;

signals:
    void skinReady(const QString &uuid);

private slots:
    void onProfileFetched(const QString &uuid);
    void onSkinDownloaded(const QString &uuid);

private:
    QImage cropFace(const QImage &skin) const;

    QNetworkAccessManager *m_nam;
    QString m_cacheDir;
};

#endif
