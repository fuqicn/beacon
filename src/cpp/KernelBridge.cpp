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
#include <QQuickWindow>
#include "WindowEffects.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <thread>
#include <mc_log.h>
#include <mc_i18n.h>
#include <mc_path.h>
#include <mc_java.h>
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
    }

    // Set skin cache dir
    s_instance->m_skinManager->setCacheDir(launcherDir + "/cache/skins");

    // Set runtime dir for java
    QString runtimeDir = launcherDir + "/.runtime";
    s_instance->m_javaManager->setRuntimeDir(runtimeDir);

    // Set default mc dir for instances
    s_instance->m_instanceManager->setDefaultRootDir(s_instance->m_mcDir);

    // Set mcDir on launch manager
    s_instance->m_launchManager->setMcDir(s_instance->m_mcDir);

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
            mc_info("Auto-selected instance: %s (dir=%s)",
                    selectedVerId.toUtf8().constData(),
                    selectedRootDir.toUtf8().constData());
        }
    });

    mc_info("KernelBridge initialized (lang=%s, mcDir=%s)",
            lang.toUtf8().constData(),
            s_instance->m_mcDir.toUtf8().constData());
}

void KernelBridge::shutdown()
{
    if (!s_instance) return;
    mc_info("[Bridge] Shutting down...");

    // Cancel all ongoing operations first
    s_instance->m_downloadManager->cancelAll();
    s_instance->m_installManager->cancelAll();
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

void KernelBridge::selectInstance(const QString &versionId, const QString &rootDir)
{
    if (!m_launchManager || !m_instanceManager) return;
    m_instanceManager->selectInstance(versionId);
    m_launchManager->setVersionId(versionId);
    if (!rootDir.isEmpty())
        m_launchManager->setMcDir(rootDir);
    else
        m_launchManager->setMcDir(m_mcDir);
    mc_info("selectInstance: verId=%s dir=%s", versionId.toUtf8().constData(),
            m_launchManager->mcDir().toUtf8().constData());
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
#ifdef Q_OS_WIN
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
#endif
    return false;
}

void KernelBridge::killAllMinecraft()
{
#ifdef Q_OS_WIN
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
    std::thread([this, requiredJava]() {
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
        QString jlinkPath = javaBinDir + "/jlink.exe";
        if (QFile::exists(bundledPath) && QFile::exists(jlinkPath)) {
            mc_info("Found bundled Java %d: %s", requiredJava, bundledPath.toUtf8().constData());
            QMetaObject::invokeMethod(this, [this, bundledPath]() {
                setJavaDownloading(false);
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
            QMetaObject::invokeMethod(this, [this, bestPath]() {
                setJavaDownloading(false);
                m_launchManager->setJavaPath(bestPath);
                doAuthAndLaunch();
            }, Qt::QueuedConnection);
            return;
        }

        // 3. No matching Java found — download required version
        mc_info("No matching Java found, downloading Java %d...", requiredJava);
        QMetaObject::invokeMethod(this, [this, requiredJava]() {
            m_javaManager->downloadJava(requiredJava);
            connect(m_javaManager, &JavaManager::javaDownloaded,
                    this, &KernelBridge::onJavaDownloaded, Qt::SingleShotConnection);
            connect(m_javaManager, &JavaManager::javaDownloadFailed, this, [this](int ver) {
                mc_error("Java %d download failed", ver);
                setJavaDownloading(false);
            }, Qt::SingleShotConnection);
        }, Qt::QueuedConnection);
    }).detach();
}

void KernelBridge::onJavaDownloaded(const QString &path, int ver)
{
    setJavaDownloading(false);
    mc_info("[Bridge] javaDownloaded: path=%s ver=%d", path.toUtf8().constData(), ver);
    m_launchManager->setJavaPath(path);
    onJavaReady();
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
        if (ok) {
            m_launchManager->setSession(m_authManager->session());
            m_launchManager->launch();
        }
    }, Qt::SingleShotConnection);

    m_authManager->refreshBeforeLaunch();
}
