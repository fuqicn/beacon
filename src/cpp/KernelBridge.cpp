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
#include "KernelBridge.h"
#include "VersionManager.h"
#include "DownloadManager.h"
#include "AuthManager.h"
#include "JavaManager.h"
#include "ModManager.h"
#include "InstallManager.h"
#include "LaunchManager.h"
#include "InstanceManager.h"
#include "SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <QQuickWindow>
#include <QQmlEngine>
#include "WindowEffects.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfoList>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <thread>
#include <mc_log.h>
#include <mc_i18n.h>
#include <mc_path.h>
#include <mc_java.h>
#include <mc_download.h>
#include <mc_mod.h>
#include <mc_download_qt.h>

#ifdef Q_OS_WIN
struct EnumMinecraftData { DWORD pid; bool found; };

static BOOL CALLBACK enumMinecraftWindow(HWND hwnd, LPARAM lParam)
{
    auto *ed = reinterpret_cast<EnumMinecraftData*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ed->pid) return TRUE;
    wchar_t title[256];
    int len = GetWindowTextW(hwnd, title, 256);
    if (len > 0 && wcsstr(title, L"Minecraft")) {
        ed->found = true;
        return FALSE;
    }
    return TRUE;
}
#endif

KernelBridge *KernelBridge::s_instance = nullptr;

KernelBridge *KernelBridge::instance()
{
    return s_instance;
}

void KernelBridge::initialize(const QString &lang, const QString &mcDir)
{
    if (s_instance) return;

    mc_console_init();
    mc_log_set_level(MC_LOG_INFO);
    mc_qt_download_init();
    mc_i18n_set(lang.toUtf8().constData());

    s_instance = new KernelBridge();
    s_instance->m_mcDir = mcDir.isEmpty() ? QDir::homePath() + "/.minecraft" : mcDir;
    s_instance->m_versionManager = new VersionManager(s_instance);
    s_instance->m_downloadManager = new DownloadManager(s_instance);
    s_instance->m_authManager = new AuthManager(s_instance);
    s_instance->m_javaManager = new JavaManager(s_instance);
    s_instance->m_modManager = new ModManager(s_instance);
    s_instance->m_installManager = new InstallManager(s_instance);
    s_instance->m_modpackManager = new ModpackManager(s_instance);
    s_instance->m_launchManager = new LaunchManager(s_instance);
    s_instance->m_instanceManager = new InstanceManager(s_instance);
    s_instance->m_settingsManager = new SettingsManager(s_instance);
    s_instance->m_skinManager = new SkinManager(s_instance);

    // Set launcher dir for auth & settings
    QString launcherDir = QCoreApplication::applicationDirPath();
    s_instance->m_authManager->setLauncherDir(launcherDir);
    s_instance->m_authManager->refresh();
    s_instance->m_settingsManager->setLauncherDir(launcherDir);

    // Ensure system preference keys exist, initializing any missing ones from OS settings
    {
        QVariantMap sys = s_instance->m_settingsManager->detectSystemSettings();
        if (!s_instance->m_settingsManager->contains("theme/mode")
            && !s_instance->m_settingsManager->contains("theme/colorScheme")) {
            s_instance->m_settingsManager->setValue("theme/mode", "system");
        }
        if (!s_instance->m_settingsManager->contains("system/scrollbars"))
            s_instance->m_settingsManager->setValue("system/scrollbars",
                                                     sys.value("scrollbars", false));
        if (!s_instance->m_settingsManager->contains("system/transparency"))
            s_instance->m_settingsManager->setValue("system/transparency",
                                                     sys.value("transparency", true));
        if (!s_instance->m_settingsManager->contains("system/animations"))
            s_instance->m_settingsManager->setValue("system/animations",
                                                     sys.value("animations", true));
        if (!s_instance->m_settingsManager->contains("download/threads"))
            s_instance->m_settingsManager->setValue("download/threads", 64);
        if (!s_instance->m_settingsManager->contains("download/source"))
            s_instance->m_settingsManager->setValue("download/source", "auto");
    }

    // Apply download concurrency from settings (PCL-style parallel ranges).
    s_instance->setDownloadThreads(
        s_instance->m_settingsManager->value("download/threads", 64).toInt());

    // Apply download source (mirror) selection.
    s_instance->setDownloadSource(
        s_instance->m_settingsManager->value("download/source", "auto").toString());

    // Set skin cache dir
    s_instance->m_skinManager->setCacheDir(launcherDir + "/cache/skins");

    // Set runtime dir for java
    QString runtimeDir = launcherDir + "/.runtime";
    s_instance->m_javaManager->setRuntimeDir(runtimeDir);

    // Set default mc dir for instances
    s_instance->m_instanceManager->setDefaultRootDir(s_instance->m_mcDir);

    // Set mcDir on launch manager
    s_instance->m_launchManager->setMcDir(s_instance->m_mcDir);

    // After a Minecraft version download finishes, register its target directory
    // and refresh the instance list even if the download dialog/page was unloaded.
    QObject::connect(s_instance->m_downloadManager, &DownloadManager::allCompleted,
                     s_instance, [](bool) {
        auto *kb = KernelBridge::instance();
        if (!kb) return;
        const QString dir = kb->m_downloadManager->lastDownloadDir();
        if (!dir.isEmpty())
            kb->m_instanceManager->addRootDir(dir);
    });

    // Defer instance scan to after UI is shown
    QTimer::singleShot(0, s_instance, []() {
        s_instance->m_instanceManager->scanInstances();
        QVariantMap sel = s_instance->m_instanceManager->getSelectedInstance();
        if (!sel.isEmpty() && sel.contains("id")) {
            QString selectedVerId = sel["id"].toString();
            QString selectedRootDir = sel["rootDir"].toString();
            s_instance->m_launchManager->setVersionId(selectedVerId);
            if (!selectedRootDir.isEmpty())
                s_instance->m_launchManager->setMcDir(selectedRootDir);
            s_instance->m_launchManager->setGameDir(
                s_instance->gameDirFor(selectedRootDir, selectedVerId));
            mc_info("Auto-selected instance: %s (dir=%s)",
                    selectedVerId.toUtf8().constData(),
                    selectedRootDir.toUtf8().constData());
        }
    });

    mc_info("KernelBridge initialized (lang=%s, mcDir=%s)",
            lang.toUtf8().constData(),
            s_instance->m_mcDir.toUtf8().constData());

    // After a modpack installs, force isolation for its fresh instance so the
    // game data (mods/overrides) inside versions/<id> is treated as its dir,
    // then rescan so the new instance shows up immediately.
    QObject::connect(s_instance->m_modpackManager, &ModpackManager::installCompleted,
                     s_instance, [s_instance](const QString &instanceId) {
                         if (!instanceId.isEmpty()) {
                             s_instance->m_settingsManager->setInstance(instanceId);
                             s_instance->m_settingsManager->setValue("launch/isolationOverride", true);
                             s_instance->m_settingsManager->setValue("launch/isolation", true);
                             s_instance->m_settingsManager->endInstance();
                             s_instance->m_instanceManager->scanInstances();
                         }
                     });
}

void KernelBridge::shutdown()
{
    if (!s_instance) return;
    mc_info("[Bridge] Shutting down...");

    // Cancel all ongoing operations first
    s_instance->m_downloadManager->cancelAll();
    s_instance->m_installManager->cancelAll();
    s_instance->m_modpackManager->cancelAll();
    s_instance->m_launchManager->stop();

    // Give threads time to respond to cancellation
    QThread::msleep(500);

    delete s_instance;
    s_instance = nullptr;

    mc_qt_download_cleanup();
    mc_info("[Bridge] Shutdown complete");
}

KernelBridge::KernelBridge(QObject *parent) : QObject(parent) {}
KernelBridge::~KernelBridge() = default;

void KernelBridge::setJavaDownloading(bool v)
{
    if (m_javaDownloading == v) return;
    m_javaDownloading = v;
    emit javaDownloadingChanged();
}

void KernelBridge::setLanguage(const QString &lang)
{
    mc_i18n_set(lang.toUtf8().constData());
}

void KernelBridge::setDownloadThreads(int n)
{
    mc_qt_download_set_thread_limit(n);
    mc_info("[Bridge] download threads -> %d", n);
}

void KernelBridge::setDownloadSource(const QString &source)
{
    QString type = source.trimmed().toLower();
    if (type.isEmpty())
        type = QStringLiteral("auto");
    if (type == QStringLiteral("official"))
        type = QStringLiteral("mojang");

    m_settingsManager->setValue("download/source", type);
    mc_download_set_mirror(type.toUtf8().constData());
    mc_mod_set_mirror(type.toUtf8().constData());
    mc_info("[Bridge] download source -> %s", type.toUtf8().constData());

    // Warm the mirror decision (Mojang probe + Modrinth comparison) in the
    // background so the first mod search / cover load never blocks on it.
    QThread *warmup = QThread::create([]() {
        mc_mod_warmup_mirror();
    });
    warmup->setObjectName("modMirrorWarmup");
    QObject::connect(warmup, &QThread::finished, warmup, &QObject::deleteLater);
    warmup->start();
}

void KernelBridge::setColorScheme(bool dark)
{
    QGuiApplication::styleHints()->setColorScheme(
        dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
}

void KernelBridge::applyThemeMode(const QString &mode)
{
    if (mode == "dark")
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    else if (mode == "light")
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    else
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Unknown);

    applyWindowTransparency(
        m_settingsManager ? m_settingsManager->value("system/transparency", true).toBool() : true);
}

void KernelBridge::applyWindowTransparency(bool enabled)
{
    if (!m_mainWindow) return;
    bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    WindowEffects::applyTransparency(m_mainWindow, enabled, dark);
}

void KernelBridge::restartApp()
{
    const QString exe = QCoreApplication::applicationFilePath();
    const qint64 pid = QCoreApplication::applicationPid();
#ifdef Q_OS_WIN
    // Wait for this process to fully exit (releasing the single-instance
    // mutex) before starting the new instance, to avoid the guard rejecting it.
    const QString cmd = QStringLiteral("Wait-Process -Id %1; Start-Process -FilePath '%2'")
                            .arg(pid).arg(exe);
    QProcess::startDetached("powershell.exe",
                            QStringList() << "-NoProfile" << "-Command" << cmd);
#else
    const QString script = QStringLiteral("while kill -0 %1 2>/dev/null; do sleep 0.1; done; exec '%2'")
                               .arg(pid).arg(exe);
    QProcess::startDetached("/bin/sh", QStringList() << "-c" << script);
#endif
    QCoreApplication::quit();
}

void KernelBridge::selectInstance(const QString &versionId, const QString &rootDir)
{
    if (!m_launchManager || !m_instanceManager) return;
    m_instanceManager->selectInstance(versionId);
    m_launchManager->setVersionId(versionId);
    QString rd = rootDir.isEmpty() ? m_mcDir : rootDir;
    if (!rootDir.isEmpty())
        m_launchManager->setMcDir(rootDir);
    else
        m_launchManager->setMcDir(m_mcDir);
    m_launchManager->setGameDir(gameDirFor(rd, versionId));
    mc_info("selectInstance: verId=%s dir=%s gameDir=%s",
            versionId.toUtf8().constData(),
            m_launchManager->mcDir().toUtf8().constData(),
            m_launchManager->gameDir().toUtf8().constData());
}

QString KernelBridge::gameDirFor(const QString &rootDir, const QString &versionId) const
{
    if (rootDir.isEmpty())
        return rootDir;
    if (!instanceIsolationEffective(rootDir, versionId))
        return rootDir;

    QString dir = QDir(rootDir).filePath("versions/" + versionId);
    QDir().mkpath(dir);

    // One-time migration: the old isolation layout kept game data in
    // <root>/instances/<versionId> together with the instance settings file.
    // Move everything except settings.ini into the version folder.
    QString old = QDir(rootDir).filePath("instances/" + versionId);
    QDir oldDir(old);
    if (oldDir.exists()) {
        const QFileInfoList entries = oldDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : entries) {
            if (fi.fileName() == QStringLiteral("settings.ini"))
                continue;
            QString target = QDir(dir).filePath(fi.fileName());
            if (QFile::exists(target))
                continue;
            if (fi.isDir())
                QDir().rename(fi.absoluteFilePath(), target);
            else
                QFile::rename(fi.absoluteFilePath(), target);
        }
    }
    return dir;
}

bool KernelBridge::isLoaderVersion(const QString &versionId) const
{
    static const char *const keys[] = { "fabric", "forge", "neoforge", "quilt", "liteloader" };
    const QString id = versionId.toLower();
    for (const char *k : keys) {
        if (id.contains(QLatin1String(k)))
            return true;
    }
    return false;
}

bool KernelBridge::instanceIsolationEffective(const QString &rootDir, const QString &versionId) const
{
    if (rootDir.isEmpty() || versionId.isEmpty() || !m_settingsManager)
        return false;
    m_settingsManager->setInstance(versionId);
    bool hasOverride = m_settingsManager->contains("launch/isolationOverride");
    if (hasOverride) {
        bool isolation = m_settingsManager->value("launch/isolation", false).toBool();
        m_settingsManager->endInstance();
        return isolation;
    }
    m_settingsManager->endInstance();

    const QString policy = m_settingsManager->value("launch/isolationPolicy", "off").toString();
    if (policy == "all")
        return true;
    if (policy == "loader")
        return isLoaderVersion(versionId);
    return false;
}

void KernelBridge::setInstanceIsolation(const QString &versionId, bool override, bool enabled)
{
    if (!m_settingsManager || versionId.isEmpty()) return;
    m_settingsManager->setInstance(versionId);
    m_settingsManager->setValue("launch/isolationOverride", override);
    m_settingsManager->setValue("launch/isolation", enabled);
    m_settingsManager->endInstance();

    // Apply to the current launch immediately so a relaunch uses the new dir
    if (m_launchManager && m_launchManager->versionId() == versionId) {
        m_launchManager->setGameDir(gameDirFor(m_launchManager->mcDir(), versionId));
    }
}

int KernelBridge::readRequiredJavaVersion(const QString &verId, const QString &mcDir) const
{
    QString curId = verId;
    while (!curId.isEmpty()) {
        QString jsonPath = QDir(mcDir).filePath(
            QStringLiteral("versions/%1/%1.json").arg(curId));
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            QJsonObject root = doc.object();
            if (root.contains("javaVersion"))
                return root["javaVersion"].toObject()["majorVersion"].toInt(17);
            curId = root["inheritsFrom"].toString();
        } else {
            break;
        }
    }
    return 17;
}

int KernelBridge::getRequiredJavaVersion() const
{
    return readRequiredJavaVersion(m_launchManager->versionId(), m_launchManager->mcDir());
}

bool KernelBridge::isAnyMinecraftRunning() const
{
#if defined(Q_OS_WIN)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (!Process32FirstW(snapshot, &pe)) {
        CloseHandle(snapshot);
        return false;
    }

    do {
        QString exe = QString::fromWCharArray(pe.szExeFile).toLower();
        if (exe != "javaw.exe" && exe != "java.exe") continue;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (!hProcess) continue;

        EnumMinecraftData ed = { pe.th32ProcessID, false };
        EnumWindows(enumMinecraftWindow, reinterpret_cast<LPARAM>(&ed));
        CloseHandle(hProcess);

        if (ed.found) {
            CloseHandle(snapshot);
            return true;
        }
    } while (Process32NextW(snapshot, &pe));

    CloseHandle(snapshot);
    return false;
#elif defined(Q_OS_UNIX)
    // Unix: pgrep for the Minecraft main class.
    QProcess proc;
    proc.start(QStringLiteral("pgrep"), QStringList() << "-f" << "net.minecraft.client");
    if (!proc.waitForFinished(3000)) return false;
    return proc.exitCode() == 0;
#else
    return false;
#endif
}

void KernelBridge::killAllMinecraft()
{
#if defined(Q_OS_WIN)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (!Process32FirstW(snapshot, &pe)) {
        CloseHandle(snapshot);
        return;
    }

    do {
        QString exe = QString::fromWCharArray(pe.szExeFile).toLower();
        if (exe != "javaw.exe" && exe != "java.exe") continue;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (!hProcess) continue;

        EnumMinecraftData ed = { pe.th32ProcessID, false };
        EnumWindows(enumMinecraftWindow, reinterpret_cast<LPARAM>(&ed));
        CloseHandle(hProcess);

        if (ed.found) {
            HANDLE hKill = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (hKill) {
                TerminateProcess(hKill, 0);
                CloseHandle(hKill);
            }
        }
    } while (Process32NextW(snapshot, &pe));

    CloseHandle(snapshot);

    emit minecraftRunningChanged();
#elif defined(Q_OS_UNIX)
    // Unix: pkill for the Minecraft main class.
    QProcess proc;
    proc.start(QStringLiteral("pkill"), QStringList() << "-f" << "net.minecraft.client");
    proc.waitForFinished(3000);
    emit minecraftRunningChanged();
#else
    Q_UNUSED(this);
#endif
}

void KernelBridge::qmlCollectGarbage()
{
    if (!m_engine) return;
    m_engine->trimComponentCache();
    m_engine->collectGarbage();
}

QString KernelBridge::readFileTail(const QString &path, int maxLines) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    // Read the last ~64 KB so huge logs don't stall the UI.
    const qint64 tailBytes = 64 * 1024;
    qint64 size = f.size();
    f.seek(qMax<qint64>(0, size - tailBytes));
    QByteArray raw = f.readAll();
    f.close();

    QString text = QString::fromUtf8(raw);
    // Strip a possibly-truncated leading line.
    int nl = text.indexOf('\n');
    if (nl > 0 && size > tailBytes)
        text = text.mid(nl + 1);

    QStringList lines = text.split('\n');
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join('\n');
}

QVariantList KernelBridge::diagnoseCrash(const QString &content) const
{
    QVariantList issues;
    if (content.isEmpty()) return issues;

    auto add = [&issues](const QString &level, const QString &title, const QString &detail) {
        QVariantMap m;
        m["level"] = level;
        m["title"] = title;
        m["detail"] = detail;
        issues.append(m);
    };

    if (content.contains("OutOfMemoryError") ||
        content.contains("Not enough memory") ||
        content.contains("unable to create new native thread"))
        add("error", "内存不足", "游戏因内存不足崩溃。请在上方设置中调大分配内存，并确保关闭其他大内存程序。");

    if (content.contains("Could not find or load main class"))
        add("error", "主类缺失", "无法找到游戏主类，版本文件可能损坏，或所需的库文件缺失。请重新安装该版本。");

    if (content.contains("UnsupportedClassVersionError") ||
        content.contains("Unsupported major.minor version") ||
        content.contains("class file has wrong version"))
        add("error", "Java 版本不匹配", "游戏库要求的 Java 版本与当前使用的 Java 不符。请在“下载 Java”中安装所需版本，或在实例设置中更换 Java。");

    if (content.contains("NoClassDefFoundError") ||
        content.contains("ClassNotFoundException"))
        add("warning", "缺少类/库", "运行时缺少某个类，通常是库文件不完整。建议重新下载该版本或检查 mod 冲突。");

    if (content.contains("java.net.ConnectException") ||
        content.contains("UnknownHostException") ||
        content.contains("SocketTimeoutException") ||
        content.contains("java.net.SocketException"))
        add("warning", "网络异常", "检测到网络连接错误，可能影响资源包/皮肤等在线内容加载。请检查网络或代理。");

    if (content.contains("GLFW") || content.contains("OpenGL") ||
        content.contains("glX") || content.contains("EAGL"))
        add("warning", "显卡/图形驱动", "检测到图形相关错误，可能由显卡驱动过旧引起。请更新显卡驱动后再试。");

    if (content.contains("Unable to make field") ||
        content.contains("IllegalAccessError"))
        add("warning", "反射权限错误", "Mod 与游戏版本不兼容，或缺少某个依赖 Mod。请检查已安装的 Mod。");

    if (issues.isEmpty())
        add("ok", "未发现明显问题", "日志尾部未检测到常见的崩溃关键字。可结合完整日志人工排查。");

    return issues;
}

QVariantList KernelBridge::listDir(const QString &path) const
{
    QVariantList entries;
    QDir dir(path);
    if (!dir.exists()) return entries;

    const QFileInfoList infos = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
        QDir::DirsFirst | QDir::Name);

    for (const auto &fi : infos) {
        QVariantMap m;
        m["name"] = fi.fileName();
        m["path"] = fi.absoluteFilePath();
        m["isDir"] = fi.isDir();
        m["size"] = fi.isFile() ? fi.size() : qint64(0);
        m["modified"] = fi.lastModified().toString("yyyy-MM-dd HH:mm:ss");
        entries.append(m);
    }
    return entries;
}

void KernelBridge::revealInExplorer(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            QStringList() << QStringLiteral("/select,") << native);
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

void KernelBridge::launchGame(int memory)
{
    if (!m_launchManager || !m_authManager || !m_javaManager) return;

    // Prevent concurrent Java download from multiple rapid launch clicks
    if (m_javaDownloading) {
        mc_info("launchGame: Java download already in progress, ignoring");
        return;
    }

    m_launchMemory = memory;
    m_launchCancelled = false;

    // Apply per-instance launch settings (isolation, JVM args, java path,
    // memory, resolution/fullscreen) from the selected instance's settings.ini
    QString verId = m_launchManager->versionId();
    if (m_settingsManager && !verId.isEmpty()) {
        m_settingsManager->setInstance(verId);
        if (m_settingsManager->contains("launch/jvmArgs"))
            m_launchManager->setExtraJvmArgs(m_settingsManager->value("launch/jvmArgs").toString());
        if (m_settingsManager->contains("launch/javaPath"))
            m_launchManager->setJavaPath(m_settingsManager->value("launch/javaPath").toString());
        if (m_settingsManager->contains("launch/memory"))
            m_launchMemory = m_settingsManager->value("launch/memory").toInt();
        bool fullscreen = m_settingsManager->value("launch/fullscreen", false).toBool();
        int rw = m_settingsManager->value("launch/resolutionW", 0).toInt();
        int rh = m_settingsManager->value("launch/resolutionH", 0).toInt();
        m_settingsManager->endInstance();
        m_launchManager->setFullscreen(fullscreen);
        m_launchManager->setResolution(rw, rh);
    }

    mc_info("launchGame: verId=%s javaPath=%s mcDir=%s",
            m_launchManager->versionId().toUtf8().constData(),
            m_launchManager->javaPath().toUtf8().constData(),
            m_launchManager->mcDir().toUtf8().constData());

    // If Java is already set (from settings), proceed directly
    if (!m_launchManager->javaPath().isEmpty()) {
        doAuthAndLaunch();
        return;
    }

    setJavaDownloading(true);

    int requiredJava = readRequiredJavaVersion(
        m_launchManager->versionId(), m_launchManager->mcDir());

    // Run Java detection on background thread to prevent UI freeze
    QPointer<KernelBridge> guard(this);
    std::thread([this, guard, requiredJava]() {
        mc_info("Java detection: need version %d", requiredJava);

        // 1. Check bundled runtime for exact version match
        QString runtimeDir = QCoreApplication::applicationDirPath() + "/.runtime";
        QString bundledPath;
#ifdef Q_OS_WIN
        bundledPath = runtimeDir + QString("/java-%1/bin/java.exe").arg(requiredJava);
#else
        bundledPath = runtimeDir + QString("/java-%1/bin/java").arg(requiredJava);
#endif

        // Verify Java is fully installed (not mid-download) by checking for a sibling binary
        QString javaBinDir = QFileInfo(bundledPath).path();
#ifdef Q_OS_WIN
        QString jlinkPath = javaBinDir + "/jlink.exe";
#else
        QString jlinkPath = javaBinDir + "/jlink";
#endif
        if (QFile::exists(bundledPath) && QFile::exists(jlinkPath)) {
            mc_info("Found bundled Java %d: %s", requiredJava, bundledPath.toUtf8().constData());
            if (!guard) return;
            QMetaObject::invokeMethod(this, [this, bundledPath]() {
                setJavaDownloading(false);
                if (m_launchCancelled) return;
                m_launchManager->setJavaPath(bundledPath);
                doAuthAndLaunch();
            }, Qt::QueuedConnection);
            return;
        }

        // 2. Scan system Java for exact version match
        McJavaRuntime runtimes[128];
        int count = mc_java_find_all(runtimes, 128);
        mc_info("Found %d system Java runtimes", count);

        QString bestPath;
        for (int i = 0; i < count; ++i) {
            if (runtimes[i].major_version == requiredJava) {
                bestPath = QString::fromUtf8(runtimes[i].path);
                break;
            }
        }

        if (!bestPath.isEmpty()) {
            mc_info("Found exact Java %d: %s", requiredJava, bestPath.toUtf8().constData());
            if (!guard) return;
            QMetaObject::invokeMethod(this, [this, bestPath]() {
                setJavaDownloading(false);
                if (m_launchCancelled) return;
                m_launchManager->setJavaPath(bestPath);
                doAuthAndLaunch();
            }, Qt::QueuedConnection);
            return;
        }

        // 3. No matching Java found — download required version
        mc_info("No matching Java found, downloading Java %d...", requiredJava);
        if (!guard) return;
        QMetaObject::invokeMethod(this, [this, requiredJava]() {
            m_javaManager->downloadJava(requiredJava);
            connect(m_javaManager, &JavaManager::javaDownloaded,
                    this, &KernelBridge::onJavaDownloaded, Qt::SingleShotConnection);
            connect(m_javaManager, &JavaManager::javaDownloadFailed, this, [this](int ver) {
                mc_error("Java %d download failed", ver);
                setJavaDownloading(false);
                mc_qt_download_set_cancel(0);
            }, Qt::SingleShotConnection);
        }, Qt::QueuedConnection);
    }).detach();
}

void KernelBridge::onJavaDownloaded(const QString &path, int ver)
{
    setJavaDownloading(false);
    mc_info("[Bridge] javaDownloaded: path=%s ver=%d", path.toUtf8().constData(), ver);
    if (m_launchCancelled) return;
    m_launchManager->setJavaPath(path);
    onJavaReady();
}

void KernelBridge::cancelLaunch()
{
    if (!m_launchManager) return;

    if (m_launchManager->running()) {
        mc_info("[Bridge] cancelLaunch: stopping running game");
        m_launchManager->stop();
        return;
    }
    if (m_javaDownloading) {
        mc_info("[Bridge] cancelLaunch: cancelling Java download");
        m_launchCancelled = true;
        m_downloadManager->cancelAll();
        return;
    }
    if (m_launchManager->verifying()) {
        mc_info("[Bridge] cancelLaunch: cancelling verification");
        m_launchCancelled = true;
        mc_qt_download_set_cancel(1);
        m_launchManager->setCancelRequested(true);
        return;
    }
}

void KernelBridge::onJavaReady()
{
    m_launchManager->setMemory(m_launchMemory);
    doAuthAndLaunch();
}

void KernelBridge::doAuthAndLaunch()
{
    // If not logged in, auto-login offline with default name
    if (!m_authManager->loggedIn()) {
        m_authManager->addOfflineAccount("Player");
    }
    m_launchManager->setSession(m_authManager->session());
    m_launchManager->setMemory(m_launchMemory);

    connect(m_authManager, &AuthManager::refreshCompleted, this, [this](bool ok) {
        if (m_launchCancelled) {
            setJavaDownloading(false);
            return;
        }
        if (ok) {
            m_launchManager->setSession(m_authManager->session());
            m_launchManager->launch();
        }
    }, Qt::SingleShotConnection);

    m_authManager->refreshBeforeLaunch();
}
