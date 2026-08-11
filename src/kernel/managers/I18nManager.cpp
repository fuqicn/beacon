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
#include "I18nManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QDir>

static int systemLangIndex()
{
    QString name = QLocale::system().name().toLower();
    if (name.startsWith("zh")) return 0;
    if (name.startsWith("ja")) return 2;
    if (name.startsWith("fr")) return 3;
    return 1;
}

I18nManager::I18nManager(const QString &launcherDir, QObject *parent)
    : QObject(parent),
      m_settings(launcherDir + "/settings.ini", QSettings::IniFormat)
{
    m_settings.setFallbacksEnabled(false);
    load(m_settings.value("language/index", -1).toInt());
}

void I18nManager::loadDict(QHash<QString, QString> &dst, const QString &langKey)
{
    dst.clear();
    QFile f(QStringLiteral(":/i18n/%1.json").arg(langKey));
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        dst.insert(it.key(), it.value().toString());
}

void I18nManager::load(int index)
{
    static const char *const langs[4] = { "zh", "en", "ja", "fr" };
    int idx = index;
    if (idx < 0)
        idx = systemLangIndex();
    if (idx < 0 || idx >= 4)
        idx = 1;
    m_langKey = QLatin1String(langs[idx]);
    loadDict(m_dict, m_langKey);
    loadDict(m_zhDict, QStringLiteral("zh"));
}

QString I18nManager::tr(const QString &key)
{
    auto it = m_dict.constFind(key);
    if (it != m_dict.constEnd())
        return it.value();
    it = m_zhDict.constFind(key);
    if (it != m_zhDict.constEnd())
        return it.value();
    return key;
}

void I18nManager::setLanguage(int index)
{
    m_settings.setValue("language/index", index);
    m_settings.sync();
    load(index);
    emit languageChanged();
}
