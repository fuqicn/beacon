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
#include "AuthManager.h"
#include <mc_log.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QHostInfo>
#include <QPointer>

AuthManager::AuthManager(QObject *parent) : QObject(parent)
{
    mc_auth_init(&m_session);
}

static QByteArray xorCrypt(const QByteArray &data, const QByteArray &key)
{
    QByteArray result(data.size(), '\0');
    for (int i = 0; i < data.size(); ++i)
        result[i] = data[i] ^ key[i % key.size()];
    return result;
}

static QByteArray cryptKey()
{
    return QCryptographicHash::hash(
        QHostInfo::localHostName().toUtf8() + "mclauncher-key-v2",
        QCryptographicHash::Sha256).left(32);
}

static QByteArray encrypt(const QByteArray &data)
{
    return xorCrypt(data, cryptKey());
}

static QByteArray decrypt(const QByteArray &data)
{
    return xorCrypt(data, cryptKey());
}

static QString formatUuid(const QString &raw)
{
    if (raw.length() < 32) return raw;
    return raw.mid(0, 8) + '-' + raw.mid(8, 4) + '-' + raw.mid(12, 4) + '-' + raw.mid(16, 4) + '-' + raw.mid(20);
}

void AuthManager::setLauncherDir(const QString &dir)
{
    m_cacheDir = dir + "/auth/cache";
    m_accountsPath = dir + "/auth/accounts.json";
    QDir().mkpath(dir + "/auth");
    QDir().mkpath(m_cacheDir);
    loadAccounts();
}

QVariantList AuthManager::accounts() const
{
    QVariantList list;
    for (const auto &a : m_accounts) {
        QVariantMap m;
        m["type"] = a.type;
        m["username"] = a.username;
        m["uuid"] = a.uuid;
        list.append(m);
    }
    return list;
}

void AuthManager::saveAccounts()
{
    QJsonArray arr;
    for (const auto &a : m_accounts) {
        QJsonObject obj;
        obj["type"] = a.type;
        obj["username"] = a.username;
        obj["uuid"] = a.uuid;
        obj["accessToken"] = QString::fromUtf8(encrypt(a.accessToken.toUtf8()).toBase64());
        obj["clientToken"] = QString::fromUtf8(encrypt(a.clientToken.toUtf8()).toBase64());
        obj["refreshToken"] = QString::fromUtf8(encrypt(a.refreshToken.toUtf8()).toBase64());
        arr.append(obj);
    }
    QJsonObject root;
    root["currentIndex"] = m_currentIndex;
    root["accounts"] = arr;
    QFile f(m_accountsPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson());
}

void AuthManager::loadAccounts()
{
    m_accounts.clear();
    m_currentIndex = -1;

    QFile f(m_accountsPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_currentIndex = root["currentIndex"].toInt(-1);

    QJsonArray arr = root["accounts"].toArray();
    for (const auto &v : arr) {
        QJsonObject obj = v.toObject();
        Account a;
        a.type = obj["type"].toInt();
        a.username = obj["username"].toString();
        a.uuid = obj["uuid"].toString();
        a.accessToken = QString::fromUtf8(decrypt(QByteArray::fromBase64(obj["accessToken"].toString().toUtf8())));
        a.clientToken = QString::fromUtf8(decrypt(QByteArray::fromBase64(obj["clientToken"].toString().toUtf8())));
        a.refreshToken = QString::fromUtf8(decrypt(QByteArray::fromBase64(obj["refreshToken"].toString().toUtf8())));
        m_accounts.append(a);
    }

    if (m_currentIndex >= 0 && m_currentIndex < m_accounts.size())
        activateAccount(m_currentIndex);

    emit accountsChanged();
}

void AuthManager::activateAccount(int index)
{
    if (index < 0 || index >= m_accounts.size()) return;
    const Account &a = m_accounts[index];
    mc_auth_init(&m_session);
    m_session.is_authenticated = 1;
    qstrncpy(m_session.name, a.username.toUtf8().constData(), sizeof(m_session.name));
    qstrncpy(m_session.uuid, a.uuid.toUtf8().constData(), sizeof(m_session.uuid));
    qstrncpy(m_session.access_token, a.accessToken.toUtf8().constData(), sizeof(m_session.access_token));
    qstrncpy(m_session.client_token, a.clientToken.toUtf8().constData(), sizeof(m_session.client_token));
    if (a.type == 2) {
        qstrncpy(m_session.user_type, "msa", sizeof(m_session.user_type));
        qstrncpy(m_session.msa_refresh_token, a.refreshToken.toUtf8().constData(), sizeof(m_session.msa_refresh_token));
    } else {
        qstrncpy(m_session.user_type, "offline", sizeof(m_session.user_type));
    }
    m_currentIndex = index;
    emit currentAccountChanged();
}

bool AuthManager::loggedIn() const
{
    return m_currentIndex >= 0 && m_currentIndex < m_accounts.size();
}

QString AuthManager::username() const
{
    if (!loggedIn()) return {};
    return m_accounts[m_currentIndex].username;
}

QString AuthManager::uuid() const
{
    if (!loggedIn()) return {};
    return m_accounts[m_currentIndex].uuid;
}

int AuthManager::loginType() const
{
    if (!loggedIn()) return 0;
    return m_accounts[m_currentIndex].type;
}

void AuthManager::addMicrosoftAccount()
{
    if (m_loggingIn) return;
    m_loggingIn = true;
    emit loggingInChanged();

    auto *result = new McAuthSession;
    mc_auth_init(result);

    QPointer<AuthManager> guard(this);
    std::thread([this, result, guard]() {
        int ret = mc_auth_msa_login(result);
        if (!guard) {
            delete result;
            return;
        }
        QMetaObject::invokeMethod(this, [this, result, ret]() {
            m_loggingIn = false;
            emit loggingInChanged();

            if (ret && result->is_authenticated) {
                Account a;
                a.type = 2;
                a.username = QString::fromUtf8(result->name);
                a.uuid = QString::fromUtf8(result->uuid);
                a.accessToken = QString::fromUtf8(result->access_token);
                a.clientToken = QString::fromUtf8(result->client_token);
                a.refreshToken = QString::fromUtf8(result->msa_refresh_token);
                m_accounts.append(a);
                m_currentIndex = m_accounts.size() - 1;
                saveAccounts();
                activateAccount(m_currentIndex);
                emit accountsChanged();
                emit loginCompleted(a.username, a.uuid);
            } else {
                emit loginFailed(QString::fromUtf8(result->error));
            }
            delete result;
        }, Qt::QueuedConnection);
    }).detach();
}

void AuthManager::addOfflineAccount(const QString &username)
{
    if (username.trimmed().isEmpty()) return;

    Account a;
    a.type = 1;
    a.username = username.trimmed();
    QByteArray hash = QCryptographicHash::hash(
        ("OfflinePlayer:" + a.username).toUtf8(), QCryptographicHash::Md5);
    a.uuid = formatUuid(QString::fromLatin1(hash.toHex()));
    a.accessToken = "offline";

    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i].type == 1 && m_accounts[i].username == a.username) {
            switchAccount(i);
            return;
        }
    }

    m_accounts.append(a);
    m_currentIndex = m_accounts.size() - 1;
    saveAccounts();
    activateAccount(m_currentIndex);
    emit accountsChanged();
    emit loginCompleted(a.username, a.uuid);
}

void AuthManager::removeAccount(int index)
{
    if (index < 0 || index >= m_accounts.size()) return;
    m_accounts.removeAt(index);
    if (m_currentIndex == index) {
        m_currentIndex = -1;
        if (!m_accounts.isEmpty()) {
            m_currentIndex = 0;
            activateAccount(0);
        } else {
            mc_auth_init(&m_session);
            emit currentAccountChanged();
        }
    } else if (m_currentIndex > index) {
        m_currentIndex--;
    }
    saveAccounts();
    emit accountsChanged();
}

void AuthManager::switchAccount(int index)
{
    if (index < 0 || index >= m_accounts.size() || index == m_currentIndex) return;
    activateAccount(index);
    saveAccounts();
}

void AuthManager::refreshBeforeLaunch()
{
    if (!loggedIn()) {
        emit refreshCompleted(false);
        return;
    }

    const Account &a = m_accounts[m_currentIndex];
    if (a.type == 2 && !a.refreshToken.isEmpty()) {
        mc_info("Refreshing MSA token before launch...");
        mc_auth_init(&m_session);
        qstrncpy(m_session.msa_refresh_token, a.refreshToken.toUtf8().constData(), sizeof(m_session.msa_refresh_token));
        int ret = mc_auth_msa_refresh(&m_session);
        if (ret && m_session.is_authenticated) {
            m_accounts[m_currentIndex].accessToken = QString::fromUtf8(m_session.access_token);
            m_accounts[m_currentIndex].clientToken = QString::fromUtf8(m_session.client_token);
            m_accounts[m_currentIndex].refreshToken = QString::fromUtf8(m_session.msa_refresh_token);
            saveAccounts();
            emit currentAccountChanged();
            emit refreshCompleted(true);
        } else {
            emit loginFailed("Token refresh failed");
            emit refreshCompleted(false);
        }
    } else {
        emit refreshCompleted(true);
    }
}

void AuthManager::refresh()
{
    if (m_loggingIn) return;
    m_loggingIn = true;
    emit loggingInChanged();

    if (loggedIn()) {
        const Account &a = m_accounts[m_currentIndex];
        if (a.type == 2) {
            mc_auth_init(&m_session);
            qstrncpy(m_session.access_token, a.accessToken.toUtf8().constData(), sizeof(m_session.access_token));
            qstrncpy(m_session.client_token, a.clientToken.toUtf8().constData(), sizeof(m_session.client_token));
            mc_auth_refresh(&m_session);
            if (m_session.is_authenticated) {
                m_accounts[m_currentIndex].accessToken = QString::fromUtf8(m_session.access_token);
                m_accounts[m_currentIndex].clientToken = QString::fromUtf8(m_session.client_token);
                saveAccounts();
            }
        }
    }

    m_loggingIn = false;
    emit loggingInChanged();
    emit currentAccountChanged();
}
