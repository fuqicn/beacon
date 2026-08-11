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
#include "SkinManager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QImageWriter>
#include <QBuffer>
#include <QUrl>
#include <QPainter>

SkinManager::SkinManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_cacheDir("skins")
{
}

void SkinManager::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
    QDir().mkpath(m_cacheDir);
}

QString SkinManager::cachedSkin(const QString &uuid) const
{
    return m_cacheDir + "/" + uuid + ".png";
}

bool SkinManager::hasCachedSkin(const QString &uuid) const
{
    return QFile::exists(cachedSkin(uuid));
}

void SkinManager::fetchSkin(const QString &uuid)
{
    if (hasCachedSkin(uuid)) {
        emit skinReady(uuid);
        return;
    }

    QString cleaned = uuid;
    cleaned.remove('-');

    QUrl url("https://sessionserver.mojang.com/session/minecraft/profile/" + cleaned);
    QNetworkRequest req(url);
    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, uuid]() {
        onProfileFetched(uuid);
    });
}

void SkinManager::onProfileFetched(const QString &uuid)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    QJsonArray props = root["properties"].toArray();

    QString texturesBase64;
    for (const QJsonValue &v : props) {
        QJsonObject p = v.toObject();
        if (p["name"].toString() == "textures") {
            texturesBase64 = p["value"].toString();
            break;
        }
    }

    if (texturesBase64.isEmpty()) {
        emit skinReady(uuid);
        return;
    }

    QByteArray decoded = QByteArray::fromBase64(texturesBase64.toUtf8());
    QJsonDocument texDoc = QJsonDocument::fromJson(decoded);
    QJsonObject texRoot = texDoc.object();
    QJsonObject textures = texRoot["textures"].toObject();
    QJsonObject skinObj = textures["SKIN"].toObject();
    QString skinUrl = skinObj["url"].toString();

    if (skinUrl.isEmpty()) {
        emit skinReady(uuid);
        return;
    }

    QNetworkRequest req(skinUrl);
    QNetworkReply *skinReply = m_nam->get(req);

    QVariant savedUuid(uuid);
    skinReply->setProperty("uuid", savedUuid);

    connect(skinReply, &QNetworkReply::finished, this, [this]() {
        onSkinDownloaded(QString());
    });
}

void SkinManager::onSkinDownloaded(const QString &)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    QString uuid = reply->property("uuid").toString();
    if (uuid.isEmpty()) return;

    QByteArray pngData = reply->readAll();
    if (pngData.isEmpty()) {
        emit skinReady(uuid);
        return;
    }

    QImage skin;
    if (!skin.loadFromData(pngData)) {
        emit skinReady(uuid);
        return;
    }

    QImage face = cropFace(skin);
    QString path = cachedSkin(uuid);
    face.save(path, "PNG");

    emit skinReady(uuid);
}

QImage SkinManager::cropFace(const QImage &skin) const
{
    int w = skin.width();
    int faceSize = w / 8;

    QImage face(faceSize, faceSize, QImage::Format_ARGB32);
    face.fill(Qt::transparent);

    QPainter p(&face);
    // Face area: x=w*1/8, y=w*1/8, size=w/8
    QRect faceRect(w / 8, w / 8, faceSize, faceSize);
    // Hat overlay area: x=w*5/8, y=w*1/8, size=w/8
    QRect hatRect(w * 5 / 8, w / 8, faceSize, faceSize);

    p.drawImage(QRect(0, 0, faceSize, faceSize), skin, faceRect);
    p.drawImage(QRect(0, 0, faceSize, faceSize), skin, hatRect);
    p.end();

    // Scale up using nearest-neighbor to avoid blur when displayed larger
    const int displaySize = 64;
    QImage scaled = face.scaled(displaySize, displaySize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return scaled;
}
