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
#include "SettingsManager.h"
#include <QDir>
#include <QProcess>

#if defined(Q_OS_MACOS)
static QString readMacDefault(const QString &domain, const QString &key)
{
    QProcess p;
    p.start("defaults", QStringList() << "read" << domain << key);
    if (!p.waitForFinished(2000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}
#endif

SettingsManager::SettingsManager(QObject *parent) : QObject(parent) {}

void SettingsManager::setLauncherDir(const QString &dir)
{
    m_launcherDir = dir;
    QDir().mkpath(dir);
    m_launcherSettings = new QSettings(dir + "/settings.ini", QSettings::IniFormat, this);
    m_launcherSettings->setFallbacksEnabled(false);
    m_active = m_launcherSettings;
}

QVariant SettingsManager::value(const QString &key, const QVariant &defaultValue) const
{
    return m_active ? m_active->value(key, defaultValue) : defaultValue;
}

void SettingsManager::setValue(const QString &key, const QVariant &value)
{
    if (!m_active) return;
    m_active->setValue(key, value);
    m_active->sync();
}

void SettingsManager::sync()
{
    if (m_active) m_active->sync();
}

void SettingsManager::beginGroup(const QString &prefix)
{
    if (m_active) m_active->beginGroup(prefix);
}

void SettingsManager::endGroup()
{
    if (m_active) m_active->endGroup();
}

void SettingsManager::remove(const QString &key)
{
    if (m_active) m_active->remove(key);
    if (m_active) m_active->sync();
}

bool SettingsManager::contains(const QString &key) const
{
    return m_active ? m_active->contains(key) : false;
}

QVariantMap SettingsManager::detectSystemSettings() const
{
    QVariantMap out;
    out.insert("theme", "light");
    out.insert("scrollbars", false);
    out.insert("transparency", true);
    out.insert("animations", true);

#if defined(Q_OS_WIN)
    {
        QSettings acc("HKEY_CURRENT_USER\\Control Panel\\Accessibility",
                      QSettings::NativeFormat);
        QString dynScroll = acc.value("DynamicScrollbars").toString();
        if (dynScroll == "0") out["scrollbars"] = true;
        else if (dynScroll == "1") out["scrollbars"] = false;

        QString anim = acc.value("AnimationEffects").toString();
        if (anim == "0") out["animations"] = false;
        else if (anim == "1") out["animations"] = true;

        QSettings personalize(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            QSettings::NativeFormat);
        QString transparency = personalize.value("EnableTransparency").toString();
        if (transparency == "0") out["transparency"] = false;
        else if (transparency == "1") out["transparency"] = true;

        QString light = personalize.value("AppsUseLightTheme").toString();
        out["theme"] = (light == "1") ? "light" : "dark";
    }
#elif defined(Q_OS_MACOS)
    {
        out["theme"] = (readMacDefault("-g", "AppleInterfaceStyle") == "Dark")
                           ? "dark" : "light";
        out["scrollbars"] =
            (readMacDefault("-g", "AppleShowScrollBars") == "Always");
        out["transparency"] =
            (readMacDefault("com.apple.universalaccess", "reduceTransparency") != "1");
        out["animations"] =
            (readMacDefault("com.apple.universalaccess", "reduceMotion") != "1");
    }
#elif defined(Q_OS_LINUX)
    {
        QProcess p;
        auto gs = [&p](const QString &key, const QString &get) -> QString {
            p.start("gsettings", QStringList() << "get" << key << get);
            if (p.waitForFinished(1500)) {
                return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            }
            return QString();
        };

        QString theme = gs("org.gnome.desktop.interface", "color-scheme");
        if (theme.contains("prefer-dark"))
            out["theme"] = "dark";

        QString overlay = gs("org.gnome.desktop.interface", "overlay-scrolling");
        if (!overlay.isEmpty())
            out["scrollbars"] = (overlay != "true");   // overlay off => always show

        QString anim = gs("org.gnome.desktop.interface", "enable-animations");
        if (!anim.isEmpty())
            out["animations"] = (anim != "false");
        // transparency: GNOME/KDE expose no simple gsettings key; keep default.
    }
#endif
    return out;
}

void SettingsManager::setInstance(const QString &instanceId)
{
    if (!m_active) return;
    m_active->sync();

    delete m_instanceSettings;
    m_instanceSettings = nullptr;

    QString path = m_launcherDir + "/instances/" + instanceId + "/settings.ini";
    QDir().mkpath(m_launcherDir + "/instances/" + instanceId);
    m_instanceSettings = new QSettings(path, QSettings::IniFormat, this);
    m_instanceSettings->setFallbacksEnabled(false);
    m_active = m_instanceSettings;
}

void SettingsManager::endInstance()
{
    if (m_instanceSettings) {
        m_instanceSettings->sync();
        delete m_instanceSettings;
        m_instanceSettings = nullptr;
    }
    m_active = m_launcherSettings;
}
