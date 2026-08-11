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
#ifndef I18NMANAGER_H
#define I18NMANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QHash>
#include <QVariantList>

class I18nManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLangKey READ currentLangKey NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
public:
    explicit I18nManager(const QString &launcherDir, QObject *parent = nullptr);

    QString currentLangKey() const { return m_langKey; }
    QVariantList languages() const { return QVariantList() << "zh" << "en" << "ja" << "fr"; }

    Q_INVOKABLE QString tr(const QString &key);
    Q_INVOKABLE void setLanguage(int index);

signals:
    void languageChanged();

private:
    void load(int index);
    void loadDict(QHash<QString, QString> &dst, const QString &langKey);

    QSettings m_settings;
    QHash<QString, QString> m_dict;
    QHash<QString, QString> m_zhDict;
    QString m_langKey;
};

#endif
