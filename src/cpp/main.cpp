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
#include <QGuiApplication>
#include <QCoreApplication>
#include <QApplication>
#include <QWidget>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QOperatingSystemVersion>
#include <QDir>
#include <QDateTime>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSettings>
#include <QStyleHints>
#include <QThreadPool>
#include <QVector>
#include <QFile>
#include <QFileInfo>
#include <QPixmapCache>
#include <QCache>
#include <QTimer>
#include <QLocale>
#include <QSemaphore>
#include <QCryptographicHash>
#include <QPointer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <cstdlib>
#include <exception>
#include <mc_log.h>
#include <mc_mod.h>
#include <mc_download_qt.h>

class SplashWidget : public QWidget
{
public:
    explicit SplashWidget(const QPixmap &pixmap)
        : m_pixmap(pixmap)
    {
        // Transparent background so the rounded-rect pixmap's corners are
        // visibly rounded from the very first frame instead of showing a
        // square window until the content is painted.
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFixedSize(pixmap.size() / pixmap.devicePixelRatio());
    }

    void setPixmap(const QPixmap &pixmap)
    {
        m_pixmap = pixmap;
        setFixedSize(pixmap.size() / pixmap.devicePixelRatio());
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.drawPixmap(0, 0, m_pixmap);
    }

private:
    QPixmap m_pixmap;
};

#include "KernelBridge.h"
#include "I18nManager.h"
#include "VersionGroupModel.h"
#include <mc_download.h>
#include <mc_http.h>
#include <mc_mod.h>
#include "ModManager.h"
#include <mc_log.h>

class TintedIconProvider : public QQuickImageProvider
{
public:
    TintedIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
        m_cache.setMaxCost(64);
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        QStringList parts = id.split('/', Qt::SkipEmptyParts);
        QString name = parts.value(0);
        QColor color = Qt::white;
        if (parts.size() >= 4) {
            color = QColor(
                qBound(0, parts[1].toInt(), 255),
                qBound(0, parts[2].toInt(), 255),
                qBound(0, parts[3].toInt(), 255)
            );
        }

        QSize s = requestedSize.isValid() ? requestedSize : QSize(24, 24);

        // Simple icon cache keyed on name+tint+size (size mismatch causes blurry scaling)
        QString cacheKey = name + "/" + QString::number(color.rgba(), 16)
            + "/" + QString::number(s.width()) + "x" + QString::number(s.height());
        if (QImage *cached = m_cache.object(cacheKey)) {
            if (size) *size = cached->size();
            return *cached;
        }

        QSvgRenderer renderer(QStringLiteral(":/icons/%1.svg").arg(name));
        if (!renderer.isValid()) {
            if (size) *size = QSize(1, 1);
            QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
            m_cache.insert(cacheKey, new QImage(empty));
            return empty;
        }

        QImage image(s, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        painter.end();

        for (int y = 0; y < image.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                int a = qAlpha(line[x]);
                if (a > 0) {
                    line[x] = qRgba(
                        qBound(0, (color.red() * a + 127) / 255, 255),
                        qBound(0, (color.green() * a + 127) / 255, 255),
                        qBound(0, (color.blue() * a + 127) / 255, 255),
                        a
                    );
                }
            }
        }

        if (size) *size = s;
        m_cache.insert(cacheKey, new QImage(image));
        return image;
    }

private:
    QCache<QString, QImage> m_cache;
};

class ModIconResponse : public QQuickImageResponse
{
public:
    ModIconResponse(const QString &url)
        : m_url(url), m_cancelled(false)
    {
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    void cancel() override
    {
        m_cancelled = true;
    }

    void deliver(const QImage &image)
    {
        if (m_cancelled)
            return;
        m_image = image;
        emit finished();
    }

    QString url() const { return m_url; }
    bool cancelled() const { return m_cancelled; }

private:
    QString m_url;
    QImage m_image;
    bool m_cancelled;
};

class ModIconProvider : public QQuickAsyncImageProvider
{
public:
    ModIconProvider()
    {
        m_memCache.setMaxCost(512);
        m_pool.setMaxThreadCount(8);
        m_pool.setExpiryTimeout(30000);
    }

    void setIconsDir(const QString &dir)
    {
        m_iconsDir = dir;
        QDir().mkpath(m_iconsDir);
    }

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override
    {
        Q_UNUSED(requestedSize);
        QByteArray decoded = QByteArray::fromBase64(id.toUtf8());
        QString url = QString::fromUtf8(decoded);
        if (url.isEmpty())
            return new ModIconResponse(url);

        if (QImage *cached = m_memCache.object(url)) {
            ModIconResponse *r = new ModIconResponse(url);
            QTimer::singleShot(0, r, [r, cached]() { r->deliver(*cached); });
            return r;
        }

        QString cacheKey = QString::fromLatin1(
            QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
        QString cachePath = m_iconsDir + QLatin1String("/") + cacheKey + QLatin1String(".png");
        if (QFile::exists(cachePath)) {
            QImage img(cachePath);
            if (!img.isNull()) {
                m_memCache.insert(url, new QImage(img));
                ModIconResponse *r = new ModIconResponse(url);
                QTimer::singleShot(0, r, [r, img]() { r->deliver(img); });
                return r;
            }
        }

        ModIconResponse *r = new ModIconResponse(url);
        startDownload(r, url, cachePath);
        return r;
    }

private:
    void startDownload(ModIconResponse *resp, const QString &url, const QString &cachePath)
    {
        QPointer<ModIconResponse> guard(resp);
        m_pool.start([guard, url, cachePath, this]() {
            QImage image = fetch(url, cachePath);
            if (!guard.isNull()) {
                QMetaObject::invokeMethod(guard.data(), [guard, image, this]() {
                    if (guard.isNull())
                        return;
                    if (!image.isNull())
                        m_memCache.insert(guard->url(), new QImage(image));
                    guard->deliver(image);
                }, Qt::QueuedConnection);
            }
        });
    }

    static QImage tryFetch(const char *target)
    {
        McHttpClient client;
        mc_http_init(&client);
        mc_http_set_timeout(&client, 8000);
        McHttpResponse *resp = mc_http_get(&client, target);
        QImage image;
        if (resp && resp->success && resp->data && resp->data_len > 0)
            image = QImage::fromData(reinterpret_cast<const uchar *>(resp->data), int(resp->data_len));
        mc_http_response_free(resp);
        return image;
    }

    static void saveCache(const QImage &image, const QString &cachePath)
    {
        if (image.isNull()) return;
        QFile out(cachePath);
        if (out.open(QIODevice::WriteOnly)) {
            image.save(&out, "PNG");
            out.close();
        }
    }

    static QImage fetch(const QString &url, const QString &cachePath)
    {
        QByteArray urlBytes = url.toUtf8();
        char mirrored[1024];
        const char *direct = urlBytes.constData();
        const char *target = direct;
        if (mc_mod_translate_download_url(urlBytes.constData(), mirrored, sizeof(mirrored)) > 0)
            target = mirrored;

        QImage image = tryFetch(target);
        if (image.isNull() && target != direct)
            image = tryFetch(direct);
        saveCache(image, cachePath);
        return image;
    }

    QCache<QString, QImage> m_memCache;
    QString m_iconsDir;
    QThreadPool m_pool;
};

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>


static wchar_t g_crashLogPath[MAX_PATH];

static LONG WINAPI crashHandler(EXCEPTION_POINTERS *exInfo) {
    HANDLE hFile = CreateFileW(g_crashLogPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD written;
    char buf[4096];

    DWORD code = exInfo->ExceptionRecord->ExceptionCode;
    PVOID addr = exInfo->ExceptionRecord->ExceptionAddress;
    DWORD tid = GetCurrentThreadId();
    DWORD pid = GetCurrentProcessId();

    int len = snprintf(buf, sizeof(buf),
        "=== CRASH at %s ===\n"
        "pid=%lu tid=%lu\n"
        "Exception code: 0x%08lX%s\n"
        "Exception address: %p\n"
        "RIP: %p\n",
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toUtf8().constData(),
        (unsigned long)pid, (unsigned long)tid,
        (unsigned long)code,
        code == 0xC00000FD ? " (STACK OVERFLOW)" : "",
        addr,
#ifdef _WIN64
        (void *)exInfo->ContextRecord->Rip
#else
        (void *)exInfo->ContextRecord->Eip
#endif
    );
    WriteFile(hFile, buf, len, &written, NULL);

    void *frames[64];
    int nf = CaptureStackBackTrace(0, 64, frames, nullptr);

    // Symbolize via dbghelp loaded on demand (never hard-linked) so the crash
    // path itself cannot pull in extra dependencies. Symbol names are
    // best-effort; the module base + RVA below is always accurate.
    typedef BOOL(WINAPI *SymInitFn)(HANDLE, LPCSTR, BOOL);
    typedef BOOL(WINAPI *SymFromAddrFn)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
    typedef BOOL(WINAPI *SymCleanupFn)(HANDLE);
    SymInitFn pSymInit = nullptr;
    SymFromAddrFn pSymFromAddr = nullptr;
    SymCleanupFn pSymCleanup = nullptr;
    HMODULE hDbg = LoadLibraryW(L"dbghelp.dll");
    if (hDbg) {
        pSymInit = (SymInitFn)GetProcAddress(hDbg, "SymInitialize");
        pSymFromAddr = (SymFromAddrFn)GetProcAddress(hDbg, "SymFromAddr");
        pSymCleanup = (SymCleanupFn)GetProcAddress(hDbg, "SymCleanup");
        if (pSymInit && !pSymInit(GetCurrentProcess(), nullptr, TRUE))
            pSymFromAddr = nullptr;
    }

    if (nf > 0) {
        len = snprintf(buf, sizeof(buf), "BT depth=%d\n", nf);
        WriteFile(hFile, buf, len, &written, NULL);
        for (int i = 0; i < nf; i++) {
            uintptr_t fp = (uintptr_t)frames[i];
            HMODULE hMod = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)fp, &hMod);
            uintptr_t base = (uintptr_t)hMod;
            wchar_t modName[MAX_PATH] = L"???";
            if (hMod)
                GetModuleBaseNameW(GetCurrentProcess(), hMod, modName, MAX_PATH);

            const char *symName = nullptr;
            char symRaw[sizeof(SYMBOL_INFO) + 512];
            PSYMBOL_INFO psi = (PSYMBOL_INFO)symRaw;
            if (pSymFromAddr) {
                memset(symRaw, 0, sizeof(symRaw));
                psi->SizeOfStruct = sizeof(SYMBOL_INFO);
                psi->MaxNameLen = 512;
                DWORD64 disp = 0;
                if (pSymFromAddr(GetCurrentProcess(), (DWORD64)fp, &disp, psi))
                    symName = psi->Name;
            }
            len = snprintf(buf, sizeof(buf), "BT%03d 0x%p  %-16ls +0x%08llX%s%s%s\n",
                           i, frames[i], modName,
                           (unsigned long long)(base ? (fp - base) : fp),
                           symName ? "  " : "", symName ? symName : "",
                           symName ? "()" : "");
            WriteFile(hFile, buf, len, &written, NULL);
        }
    }
    if (hDbg) {
        if (pSymCleanup) pSymCleanup(GetCurrentProcess());
        FreeLibrary(hDbg);
    }
    CloseHandle(hFile);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void signalHandler(int sig) {
    const char *name = "Unknown";
    if (sig == SIGSEGV) name = "SIGSEGV (Segmentation Fault)";
    else if (sig == SIGABRT) name = "SIGABRT";
    else if (sig == SIGFPE) name = "SIGFPE";
    else if (sig == SIGILL) name = "SIGILL";

    char buf[256];
    int len = snprintf(buf, sizeof(buf), "\n=== SIGNAL %d: %s ===\n", sig, name);
    HANDLE hFile = CreateFileW(g_crashLogPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, buf, len, &written, NULL);
        CloseHandle(hFile);
    }
    _exit(1);
}

static void installCrashHandlers()
{
    QString crashLog = QCoreApplication::applicationDirPath() + "/crash.log";
    crashLog.toWCharArray(g_crashLogPath);
    g_crashLogPath[crashLog.size()] = L'\0';
    SetUnhandledExceptionFilter(crashHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGFPE, signalHandler);
    signal(SIGILL, signalHandler);
    // Backstop for hard terminates (uncaught exceptions, noexcept violations)
    // that never reach the SEH filter / signal handlers. This is what the
    // "crashed right after a download finished" reports produced: an empty
    // crash.log means the death bypassed both handlers.
    std::set_terminate([]() {
        HANDLE hFile = CreateFileW(g_crashLogPath, GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            const char *buf = "=== std::terminate ===\n";
            WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
            CloseHandle(hFile);
        }
        abort();
    });
    HANDLE hFile = CreateFileW(g_crashLogPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char *marker = "Launcher started, watching for crashes...\n";
        DWORD written;
        WriteFile(hFile, marker, (DWORD)strlen(marker), &written, NULL);
        CloseHandle(hFile);
    }
}
#endif

// Headless download repro (--test-dl): replicates ModpackWorker by running the
// download on a worker QThread and reporting progress via a queued signal.
class DlWorker : public QObject {
    Q_OBJECT
public:
    DlWorker(QString url, QString mirror, QString path, QString sha1, long size)
        : m_url(std::move(url)), m_mirror(std::move(mirror)), m_path(std::move(path)),
          m_sha1(std::move(sha1)), m_size(size) {}
public slots:
    void run() {
        QByteArray urlBa = m_url.toUtf8();
        QByteArray mirrorBa = m_mirror.toUtf8();
        const char *urls[2];
        int urlCount = 1;
        urls[0] = urlBa.constData();
        if (!mirrorBa.isEmpty()) {
            urls[0] = mirrorBa.constData();
            urls[1] = urlBa.constData();
            urlCount = 2;
        }
        QByteArray sha1Ba;
        const char *sha1c = nullptr;
        if (!m_sha1.isEmpty()) { sha1Ba = m_sha1.toUtf8(); sha1c = sha1Ba.constData(); }
        struct Ctx { qreal last = -1; DlWorker *self = nullptr; };
        Ctx ctx;
        ctx.self = this;
        auto cb = [](const char *, long long received, long long total, void *userdata) {
            auto *c = static_cast<Ctx *>(userdata);
            long long base = total > 0 ? total : (received > 0 ? received * 2 : 0);
            if (base <= 0) return;
            qreal p = qBound<qreal>(0.0, (qreal)received / (qreal)base, 1.0);
            if (c->last < 0 || p - c->last > 0.005) {
                c->last = p;
                if (c->self) c->self->notifyProgress(p);
            }
        };
        QByteArray pathBa = m_path.toUtf8();
        mc_info("[TESTDL] sources=%d path=%s", urlCount, pathBa.constData());
        int ok = mc_qt_download_multi_progress(urls, urlCount, pathBa.constData(),
                                               sha1c, m_size, 120000, cb, &ctx);
        mc_info("[TESTDL] result=%d", ok);
        emit finished(ok != 0);
    }
    void notifyProgress(qreal p) { emit progress(p); }
signals:
    void progress(qreal p);
    void finished(bool ok);
private:
    QString m_url, m_mirror, m_path, m_sha1;
    long m_size;
};

// Resolve the persistent launcher data directory.
// In AppImage mode BEACON_LAUNCHER_DIR is set by the GTK launcher (beacon_gtk.c)
// to the directory containing the .AppImage file; data lives in <that>/beacon/.
// Outside AppImage, fall back to the directory containing the executable.
static QString resolveLauncherDir()
{
    const char *env = getenv("BEACON_LAUNCHER_DIR");
    if (env && env[0]) {
        QString dir = QString::fromUtf8(env) + "/beacon";
        QDir().mkpath(dir);
        return dir;
    }
    return QCoreApplication::applicationDirPath();
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    bool debugMode = false;
    int downloadJavaVersion = 0;
    QString downloadVersionId;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) { debugMode = true; }
        if (strcmp(argv[i], "--download-java") == 0 && i + 1 < argc) {
            downloadJavaVersion = atoi(argv[i + 1]);
            i++;
        }
        if (strcmp(argv[i], "--download-version") == 0 && i + 1 < argc) {
            downloadVersionId = QString::fromUtf8(argv[i + 1]);
            i++;
        }
    }
    if (downloadJavaVersion > 0) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("Beacon");
        app.setApplicationVersion("1.0.0");
        (void)QLocale::system(); // init system locale before worker threads start

        // Log to file for capture
        mc_log_set_file((QCoreApplication::applicationDirPath() + "/java-test.log").toUtf8().constData());

        QString mcDir = QCoreApplication::applicationDirPath() + "/.minecraft";
        KernelBridge::initialize("zh", mcDir);
        mc_log_set_level(MC_LOG_DEBUG); // override after KernelBridge::initialize resets it
        auto *dl = KernelBridge::instance()->downloadManager();

        QString javaDir = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral(".java-test-%1").arg(downloadJavaVersion));

        QObject::connect(dl, &DownloadManager::allCompleted, [&](bool ok) {
            mc_info("[TEST] Java %d download %s", downloadJavaVersion, ok ? "SUCCEEDED" : "FAILED");
            QCoreApplication::quit();
        });
        QObject::connect(dl, &DownloadManager::errorOccurred, [&](const QString &msg) {
            mc_info("[TEST] Error: %s", msg.toUtf8().constData());
        });
        QObject::connect(dl, &DownloadManager::progressChanged, [&](qreal p, const QString &task) {
            if (!task.isEmpty())
                mc_info("[TEST] %.0f%% - %s", p * 100.0, task.toUtf8().constData());
        });

        mc_info("[TEST] Starting Java %d download to %s", downloadJavaVersion, javaDir.toUtf8().constData());
        QTimer::singleShot(100, [dl, downloadJavaVersion, javaDir]() {
            dl->downloadJava(downloadJavaVersion, javaDir);
        });

        app.exec();

        // Close log file and dump to stdout for parent capture
        mc_log_set_file(nullptr);
        QFile dumpFile(QCoreApplication::applicationDirPath() + "/java-test.log");
        if (dumpFile.open(QIODevice::ReadOnly)) {
            QByteArray data = dumpFile.readAll();
            printf("%s", data.constData());
            fflush(stdout);
            dumpFile.close();
        }

        QDir(javaDir).removeRecursively();
        KernelBridge::shutdown();
        return 0;
    }
    if (!downloadVersionId.isEmpty()) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("Beacon");
        app.setApplicationVersion("1.0.0");
        (void)QLocale::system(); // init system locale before worker threads start

        // Log to file for capture
        mc_log_set_file((QCoreApplication::applicationDirPath() + "/version-test.log").toUtf8().constData());

        QString mcDir = QCoreApplication::applicationDirPath() + "/.minecraft";
        KernelBridge::initialize("zh", mcDir);
        mc_log_set_level(MC_LOG_DEBUG); // override after KernelBridge::initialize resets it
        auto *dl = KernelBridge::instance()->downloadManager();

        QObject::connect(dl, &DownloadManager::allCompleted, [&](bool ok) {
            mc_info("[TEST] Version %s download %s",
                    downloadVersionId.toUtf8().constData(), ok ? "SUCCEEDED" : "FAILED");
            QCoreApplication::quit();
        });
        QObject::connect(dl, &DownloadManager::errorOccurred, [&](const QString &msg) {
            mc_info("[TEST] Error: %s", msg.toUtf8().constData());
        });
        QObject::connect(dl, &DownloadManager::progressChanged, [&](qreal p, const QString &task) {
            if (!task.isEmpty())
                mc_info("[TEST] %.0f%% - %s", p * 100.0, task.toUtf8().constData());
        });

        mc_info("[TEST] Starting version %s download to %s",
                downloadVersionId.toUtf8().constData(), mcDir.toUtf8().constData());
        QTimer::singleShot(100, [dl, downloadVersionId, mcDir]() {
            dl->startDownload(downloadVersionId, mcDir);
        });

        app.exec();

        // Close log file and dump to stdout for parent capture
        mc_log_set_file(nullptr);
        QFile dumpFile(QCoreApplication::applicationDirPath() + "/version-test.log");
        if (dumpFile.open(QIODevice::ReadOnly)) {
            QByteArray data = dumpFile.readAll();
            printf("%s", data.constData());
            fflush(stdout);
            dumpFile.close();
        }

        KernelBridge::shutdown();
        return 0;
    }
    bool testMod = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test-mod") == 0) { testMod = true; }
    }
    if (testMod) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("Beacon");
        app.setApplicationVersion("1.0.0");
        (void)QLocale::system(); // init system locale before worker threads start

        mc_log_set_file((QCoreApplication::applicationDirPath() + "/mod-test.log").toUtf8().constData());

        QString mcDir = QCoreApplication::applicationDirPath() + "/.minecraft";
        KernelBridge::initialize("zh", mcDir);
        mc_log_set_level(MC_LOG_DEBUG);
        auto *mm = KernelBridge::instance()->modManager();

        QString rootDir = QCoreApplication::applicationDirPath() + "/.modtest";

        auto quit = [&]() {
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
        };

        QObject::connect(mm, &ModManager::errorOccurred, [&](const QString &msg) {
            mc_info("[MODTEST] Error: %s", msg.toUtf8().constData());
        });

        QObject::connect(mm, &ModManager::searchCompleted, [&](const QVariantList &results) {
            mc_info("[MODTEST] Search returned %d results", results.size());
            for (const QVariant &r : results) {
                QVariantMap m = r.toMap();
mc_info("[MODTEST]   - %s (%s) downloads=%s",
        m.value("name").toString().toUtf8().constData(),
        m.value("id").toString().toUtf8().constData(),
        m.value("downloadCount").toString().toUtf8().constData());
            }
            if (!results.isEmpty()) {
                mm->getProject(results.first().toMap().value("id").toString());
            } else {
                quit();
            }
        });

        QObject::connect(mm, &ModManager::projectLoaded, [&](const QVariantMap &project) {
            mc_info("[MODTEST] Project loaded: %s (%s)",
                    project.value("name").toString().toUtf8().constData(),
                    project.value("id").toString().toUtf8().constData());
            mm->getVersions(project.value("id").toString());
        });

        QObject::connect(mm, &ModManager::versionsLoaded, [&](const QVariantList &versions) {
            mc_info("[MODTEST] Versions returned %d", versions.size());
            int installed = 0;
            for (const QVariant &v : versions) {
                if (installed >= 3) break;
                QVariantMap vm = v.toMap();
                if (vm.value("fileName").toString().endsWith(".jar")) {
                    mc_info("[MODTEST] Enqueue %s", vm.value("fileName").toString().toUtf8().constData());
                    mm->installMod(vm, rootDir);
                    installed++;
                }
            }
            if (installed == 0) { quit(); return; }
            mc_info("[MODTEST] Enqueued %d mods", installed);
        });

        QObject::connect(mm, &ModManager::tasksChanged, [&]() {
            static QString lastLog;
            const auto tasks = mm->tasks();
            QString line;
            for (const QVariant &t : tasks) {
                QVariantMap m = t.toMap();
                line += QString("%1:%2(%3%%) ")
                        .arg(m.value("fileName").toString())
                        .arg(m.value("status").toString())
                        .arg(qRound(m.value("progress").toDouble() * 100));
            }
            if (line != lastLog) {
                mc_info("[MODTEST] tasks: %s", line.toUtf8().constData());
                lastLog = line;
            }
        });

        int completed = 0;
        QObject::connect(mm, &ModManager::installCompleted, [&](bool ok, const QString &path) {
            completed++;
            mc_info("[MODTEST] Install #%d %s -> %s", completed, ok ? "SUCCEEDED" : "FAILED",
                    path.toUtf8().constData());
            if (completed < 3) return;
            auto mods = mm->listInstalledMods(rootDir);
            mc_info("[MODTEST] Installed mods in dir: %d", mods.size());
            for (const QVariant &m : mods) {
                QVariantMap vm = m.toMap();
                mc_info("[MODTEST]   - %s (%s bytes)",
                        vm.value("fileName").toString().toUtf8().constData(),
                        vm.value("size").toString().toUtf8().constData());
            }
            for (const QVariant &m : mods) {
                QString fn = m.toMap().value("fileName").toString();
                bool removed = mm->removeMod(rootDir, fn);
                mc_info("[MODTEST] removeMod(%s) = %s", fn.toUtf8().constData(), removed ? "true" : "false");
            }
            mods = mm->listInstalledMods(rootDir);
            mc_info("[MODTEST] After removal, mods left: %d", mods.size());
            QDir(rootDir).removeRecursively();
            quit();
        });

        mc_info("[MODTEST] Starting search for 'sodium'");
        QTimer::singleShot(100, [mm]() {
            mm->search(QStringLiteral("sodium"), QStringLiteral("relevance"), 3);
        });

        app.exec();

        mc_log_set_file(nullptr);
        QFile dumpFile(QCoreApplication::applicationDirPath() + "/mod-test.log");
        if (dumpFile.open(QIODevice::ReadOnly)) {
            QByteArray data = dumpFile.readAll();
            printf("%s", data.constData());
            fflush(stdout);
            dumpFile.close();
        }

KernelBridge::shutdown();
        return 0;
    }
    // ── Headless download test: --test-dl <url> <out> [--dl-sha1 <hex>] [--dl-size <bytes>] [--dl-mirror <url>]
    QString testDlUrl, testDlPath, testDlSha1, testDlMirror;
    long testDlSize = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test-dl") == 0 && i + 2 < argc) {
            testDlUrl = QString::fromUtf8(argv[i + 1]);
            testDlPath = QString::fromUtf8(argv[i + 2]);
            i += 2;
        } else if (strcmp(argv[i], "--dl-sha1") == 0 && i + 1 < argc) {
            testDlSha1 = QString::fromUtf8(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--dl-size") == 0 && i + 1 < argc) {
            testDlSize = atol(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--dl-mirror") == 0 && i + 1 < argc) {
            testDlMirror = QString::fromUtf8(argv[i + 1]);
            i++;
        }
    }
    if (!testDlUrl.isEmpty()) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("Beacon");
        app.setApplicationVersion("1.0.0");
        (void)QLocale::system();

        mc_log_set_file((QCoreApplication::applicationDirPath() + "/dl-test.log").toUtf8().constData());

        QString mcDir = QCoreApplication::applicationDirPath() + "/.minecraft";
        KernelBridge::initialize("zh", mcDir);
        mc_log_set_level(MC_LOG_DEBUG);

        QByteArray mirrorBa;
        {
            QByteArray urlBa = testDlUrl.toUtf8();
            char translated[2048] = "";
            if (!testDlMirror.isEmpty()) {
                mirrorBa = testDlMirror.toUtf8();
            } else if (mc_mod_translate_download_url(urlBa.constData(), translated, sizeof(translated))) {
                mirrorBa = QByteArray(translated);
            }
        }

        auto *worker = new DlWorker(testDlUrl, QString::fromUtf8(mirrorBa), testDlPath, testDlSha1, testDlSize);
        auto *thread = new QThread;
        worker->moveToThread(thread);
        QObject::connect(thread, &QThread::started, worker, &DlWorker::run, Qt::DirectConnection);
        QObject::connect(worker, &DlWorker::progress, [](qreal p) {
            mc_info("[TESTDL] %d%%", qRound(p * 100));
        });
        bool finishedFlag = false;
        QObject::connect(worker, &DlWorker::finished, [&]() {
            finishedFlag = true;
            QCoreApplication::quit();
        });
        thread->start();
        app.exec();
        thread->quit();
        thread->wait(8000);
        delete worker;
        delete thread;
        mc_info("[TESTDL] final finishedFlag=%d", (int)finishedFlag);
        mc_log_set_file(nullptr);
        QFile dumpFile(QCoreApplication::applicationDirPath() + "/dl-test.log");
        if (dumpFile.open(QIODevice::ReadOnly)) {
            QByteArray data = dumpFile.readAll();
            printf("%s", data.constData());
            fflush(stdout);
            dumpFile.close();
        }
        KernelBridge::shutdown();
        return 0;
    }
    // ── Headless install repro: --test-install <url> <out> <rootdir> [--dl-sha1 <hex>] [--dl-size <bytes>]
    // Replicates ModpackManager::doInstallProject (mrpack downloadFile -> installPack)
    // on the real ModpackWorker thread so a crash in that transition can be
    // reproduced under a debugger without driving the GUI. When the local file
    // at <out> already matches --dl-sha1 the download is skipped instantly and
    // the install step is reached within a second.
    QString testInstallUrl, testInstallOut, testInstallRoot, testInstallSha1;
    long testInstallSize = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test-install") == 0 && i + 3 < argc) {
            testInstallUrl = QString::fromUtf8(argv[i + 1]);
            testInstallOut = QString::fromUtf8(argv[i + 2]);
            testInstallRoot = QString::fromUtf8(argv[i + 3]);
            i += 3;
        } else if (strcmp(argv[i], "--dl-sha1") == 0 && i + 1 < argc) {
            testInstallSha1 = QString::fromUtf8(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--dl-size") == 0 && i + 1 < argc) {
            testInstallSize = atol(argv[i + 1]);
            i++;
        }
    }
    if (!testInstallUrl.isEmpty()) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("Beacon");
        app.setApplicationVersion("1.0.0");
        (void)QLocale::system();

        mc_log_set_file((QCoreApplication::applicationDirPath() + "/install-test.log").toUtf8().constData());
#ifdef Q_OS_WIN
        installCrashHandlers();
#endif
        QString mcDir = QCoreApplication::applicationDirPath() + "/.minecraft";
        KernelBridge::initialize("zh", mcDir);
        mc_log_set_level(MC_LOG_DEBUG);
        mc_info("[TESTINST] url=%s out=%s root=%s", testInstallUrl.toUtf8().constData(),
                testInstallOut.toUtf8().constData(), testInstallRoot.toUtf8().constData());

        bool done = false;
        ModpackManager *pm = KernelBridge::instance()->modpackManager();
        QObject::connect(pm, &ModpackManager::installCompleted, [&done](const QString &id) {
            mc_info("[TESTINST] installCompleted: %s", id.toUtf8().constData());
            done = true;
            QCoreApplication::quit();
        });
        QObject::connect(pm, &ModpackManager::errorOccurred, [&done](const QString &msg) {
            mc_error("[TESTINST] errorOccurred: %s", msg.toUtf8().constData());
            done = true;
            QCoreApplication::quit();
        });
        // A clean install reaches the long Minecraft-base download phase; stop
        // before that so a successful repro run exits fast instead of burning
        // minutes. A crash (if any) happens long before this fires.
        QTimer::singleShot(30000, []() {
            mc_info("[TESTINST] 30s timeout, quitting (no crash in the download->install window)");
            QCoreApplication::quit();
        });

        QVariantMap file;
        file["fileName"] = QFileInfo(testInstallOut).fileName();
        file["downloadUrl"] = testInstallUrl;
        file["sha1"] = testInstallSha1;
        file["size"] = (qlonglong)testInstallSize;
        file["iconUrl"] = "";
        pm->installFromProject(file, testInstallRoot);

        app.exec();
        mc_info("[TESTINST] final done=%d", (int)done);
        mc_log_set_file(nullptr);
        QFile dumpFile(QCoreApplication::applicationDirPath() + "/install-test.log");
        if (dumpFile.open(QIODevice::ReadOnly)) {
            QByteArray data = dumpFile.readAll();
            printf("%s", data.constData());
            fflush(stdout);
            dumpFile.close();
        }
        KernelBridge::shutdown();
        return 0;
    }
    // ── Headless batch test: --test-batch <url> <outdir> <count> [--batch] [--tb-size <bytes>]
    // Exercises the two install download paths: mc_qt_download_batch_ex (ModpackManager,
    // QThread::create worker pool) and mc_qt_download_batch (InstallManager engine).
    QString testBatchUrl, testBatchOutDir;
    int testBatchCount = 0;
    bool testBatchUseEngine = false;
    long testBatchSize = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test-batch") == 0 && i + 3 < argc) {
            testBatchUrl = QString::fromUtf8(argv[i + 1]);
            testBatchOutDir = QString::fromUtf8(argv[i + 2]);
            testBatchCount = atoi(argv[i + 3]);
            i += 3;
        } else if (strcmp(argv[i], "--batch") == 0) {
            testBatchUseEngine = true;
        } else if (strcmp(argv[i], "--tb-size") == 0 && i + 1 < argc) {
            testBatchSize = atol(argv[i + 1]);
            i++;
        }
    }
    if (!testBatchUrl.isEmpty() && testBatchCount > 0) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("Beacon");
        app.setApplicationVersion("1.0.0");
        (void)QLocale::system();

        mc_log_set_file((QCoreApplication::applicationDirPath() + "/dl-test.log").toUtf8().constData());

        QString mcDir = QCoreApplication::applicationDirPath() + "/.minecraft";
        KernelBridge::initialize("zh", mcDir);
        mc_log_set_level(MC_LOG_DEBUG);

        QDir outDir(testBatchOutDir);
        if (!outDir.exists()) outDir.mkpath(".");

        QVector<long> sizes;
        QVector<QByteArray> pathBas;
        int n = qMin(testBatchCount, 256);
        for (int i = 0; i < n; i++) {
            QString p = QString("%1/%2.bin").arg(testBatchOutDir).arg(i + 1, 5, 10, QLatin1Char('0'));
            pathBas.append(p.toUtf8());
            sizes.append(testBatchSize);
        }
        QVector<const char *> paths;
        for (auto &b : pathBas) paths.append(b.constData());

        auto *bw = new QObject;
        auto *thread = new QThread;
        bw->moveToThread(thread);
        int resultFlag = -1;
        QObject::connect(thread, &QThread::started, bw, [=, &resultFlag, &paths]() {
            QByteArray urlBa = testBatchUrl.toUtf8();
            const char *srcs[1] = { urlBa.constData() };
            int results[256];
            int ok = 0;
            if (testBatchUseEngine) {
                QVector<const char *> urls, sha1s;
                for (int i = 0; i < n; i++) { urls.append(srcs[0]); sha1s.append(nullptr); }
                ok = mc_qt_download_batch(urls.data(), paths.data(), sha1s.data(),
                                          sizes.constData(), n, 120000, results);
                mc_info("[TESTB] engine ok=%d/%d", ok, n);
            } else {
                QVector<const char *> itemUrls;
                for (int i = 0; i < n; i++) itemUrls.append(srcs[0]);
                QVector<McQtBatchItem> items;
                for (int i = 0; i < n; i++) {
                    McQtBatchItem it;
                    it.urls = itemUrls.constData() + i;
                    it.url_count = 1;
                    it.path = paths[i];
                    it.sha1 = nullptr;
                    it.size = sizes[i];
                    items.append(it);
                }
                ok = mc_qt_download_batch_ex(items.constData(), n, 120000, results);
                mc_info("[TESTB] batch_ex ok=%d/%d", ok, n);
            }
            for (int i = 0; i < n; i++) mc_info("[TESTB] file %d -> %d", i + 1, results[i]);
            resultFlag = ok;
            QCoreApplication::quit();
        }, Qt::DirectConnection);
        thread->start();
        app.exec();
        thread->quit();
        thread->wait(8000);
        delete bw;
        delete thread;
        mc_info("[TESTB] final resultFlag=%d", resultFlag);
        mc_log_set_file(nullptr);
        QFile dumpFile(QCoreApplication::applicationDirPath() + "/dl-test.log");
        if (dumpFile.open(QIODevice::ReadOnly)) {
            QByteArray data = dumpFile.readAll();
            printf("%s", data.constData());
            fflush(stdout);
            dumpFile.close();
        }
        KernelBridge::shutdown();
return 0;
    }
    if (debugMode) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
    }
#endif

#if defined(Q_OS_LINUX)
    // GNOME (Wayland) renders no client-side title bar for this Qt build, so
    // the main window shows up undecorated. Fall back to the xcb (XWayland)
    // platform where the WM provides server-side decorations and reliable
    // close handling. Respect an explicit override from the environment.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
#endif

    QApplication app(argc, argv);
    app.setApplicationName("Beacon");

    // Required for any translucent QQuickWindow: without the alpha buffer,
    // color:"transparent" (used for the Windows 11 Mica backdrop) does not
    // compose properly and resizing the window leaves a stale, non-repainted
    // region (a fixed-size gray/white block that no longer matches the window).
    QQuickWindow::setDefaultAlphaBuffer(true);
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Beacon");
    (void)QLocale::system(); // init system locale before worker threads start

    qint64 tProc = QDateTime::currentMSecsSinceEpoch();

    // Log to file next to the executable for GUI-mode diagnosis
    mc_log_set_file((resolveLauncherDir() + "/launcher.log").toUtf8().constData());

    mc_info("[Startup] t0=0ms (after QApplication ctor)");

    // Single-instance guard: multiple launchers sharing one data dir corrupt
    // each other's chunked downloads (.chunk.N) and .mrpack_dl / versions/
    // folders, causing install failures and crashes.
#ifdef Q_OS_WIN
    // Windows: a kernel mutex is released automatically by the OS when the
    // owning process dies (even a crash), is atomic against simultaneous
    // launches, and has no stale-file/timeout takeover semantics.
    const QString mutexName = "Local\\Beacon-"
        + QString::fromLatin1(QCryptographicHash::hash(
            QCoreApplication::applicationDirPath().toUtf8(),
            QCryptographicHash::Md5).toHex());
    HANDLE hInstanceMutex = CreateMutexW(nullptr, TRUE,
                                         (const wchar_t *)mutexName.utf16());
    DWORD gle = GetLastError();
    mc_info("[Startup] single-instance guard: h=%p gle=%u name=%s",
            (void *)hInstanceMutex, (unsigned)gle, qPrintable(mutexName));
    if (!hInstanceMutex || gle == ERROR_ALREADY_EXISTS) {
        if (hInstanceMutex) CloseHandle(hInstanceMutex);
        mc_info("[Startup] another Beacon instance is already running; exiting");
        QMessageBox::warning(nullptr, "Beacon", QStringLiteral("Beacon 已在运行，请勿重复打开。"));
        return 0;
    }
    // Keep hInstanceMutex open for the whole process; the OS releases it on exit.
#else
    QLocalServer ipcServer;
    const QString ipcName = "Beacon-"
        + QString::fromLatin1(QCryptographicHash::hash(
            QCoreApplication::applicationDirPath().toUtf8(),
            QCryptographicHash::Md5).toHex());
    // On Unix a force-terminated instance (e.g. the std::_Exit path below)
    // leaves the socket file behind; QLocalServer::listen() would then fail
    // even though no Beacon is running. Probe for a live server first, and
    // only clear the stale socket when nobody answers.
    {
        QLocalSocket probe;
        probe.connectToServer(ipcName, QIODevice::ReadOnly);
        if (probe.waitForConnected(300)) {
            probe.abort();
            mc_info("[Startup] another Beacon instance is already running; exiting");
            QMessageBox::warning(nullptr, "Beacon", QStringLiteral("Beacon 已在运行，请勿重复打开。"));
            return 0;
        }
        probe.abort();
        QLocalServer::removeServer(ipcName); // discard stale socket, ignore result
    }
    if (!ipcServer.listen(ipcName)) {
        mc_info("[Startup] another Beacon instance is already running; exiting");
        QMessageBox::warning(nullptr, "Beacon", QStringLiteral("Beacon 已在运行，请勿重复打开。"));
        return 0;
    }
#endif

    // Lightweight QWidget splash shown immediately after QApplication
    // construction, before font/threadpool/mirror/ini/kernel/QML init,
    // so a window appears within a few ms of process start.
    QPixmap splashPixmap(420, 280);
    splashPixmap.fill(Qt::transparent);
    SplashWidget splash(splashPixmap);
    splash.setWindowFlags(Qt::SplashScreen | Qt::FramelessWindowHint);

    {
        // Render at the screen's device pixel ratio so the pixmap is not
        // upscaled (which would blur / alias the text on HiDPI displays).
        const qreal dpr = splash.devicePixelRatioF();
        QPixmap painted(QSize(qRound(420 * dpr), qRound(280 * dpr)));
        painted.setDevicePixelRatio(dpr);
        painted.fill(Qt::transparent);
        QPainter p(&painted);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        const QPalette pal = app.palette();
        p.setBrush(pal.color(QPalette::Window));
        p.setPen(QPen(pal.color(QPalette::Mid), 1));
        p.drawRoundedRect(QRectF(0.5, 0.5, 419, 279), 12, 12);

        // QPainter on a pixmap with devicePixelRatio works in logical
        // coordinates (420x280) and maps to physical pixels automatically,
        // so font sizes and the layout must use logical units only.
        p.setPen(pal.color(QPalette::Highlight));
        QFont title = app.font();
        title.setPixelSize(36);
        title.setBold(true);
        p.setFont(title);
        const QFontMetricsF tm = p.fontMetrics();
        const qreal titleH = tm.height();

        // Center the title vertically.
        const qreal top = (280 - titleH) / 2.0;

        p.setPen(pal.color(QPalette::Highlight));
        p.setFont(title);
        p.drawText(QRectF(0, top, 420, titleH),
                   Qt::AlignCenter, QStringLiteral("Beacon"));
        p.end();
        splash.setPixmap(painted);
    }
    mc_info("[Startup] splash content painted in %lldms",
            QDateTime::currentMSecsSinceEpoch() - tProc);

    // Paint first, then show: with the content already in place the very first
    // frame is already the rounded-rect card (no square flash).
    splash.show();
    app.processEvents();
    mc_info("[Startup] splash window shown in %lldms",
            QDateTime::currentMSecsSinceEpoch() - tProc);

    // TEMP DEBUG: capture all Qt messages (incl. QML console.log / Image errors)
    static FILE *qtDbgFile = nullptr;
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        if (!qtDbgFile)
            qtDbgFile = fopen("qtdebug.log", "w");
        if (qtDbgFile) {
            fprintf(qtDbgFile, "[%d] %s (%s:%d)\n", (int)type, qPrintable(msg),
                    ctx.file ? ctx.file : "?", ctx.line);
            fflush(qtDbgFile);
        }
    });

    mc_info("[Startup] splash shown in %lldms", QDateTime::currentMSecsSinceEpoch() - tProc);

    QPixmapCache::setCacheLimit(2048); // 2MB image cache limit

    QThreadPool::globalInstance()->setMaxThreadCount(8);

    {
        QFont f = app.font();
        f.setFamilies({"Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"});
        app.setFont(f);
    }

#ifdef Q_OS_WIN
    installCrashHandlers();
#endif

    QString uiStyle;
    {
        QSettings styleSettings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
        uiStyle = styleSettings.value("ui/style", "auto").toString();
    }

    // Resolved active style, exposed to QML so corners can follow it
    // (FluentWinUI3 / macOS / Imagine are rounded, Windows / Fusion are square).
    QString uiStyleName;
#if defined(Q_OS_WIN)
    if (uiStyle == "fluentwinui3") {
        QQuickStyle::setStyle("FluentWinUI3");
        uiStyleName = "fluentwinui3";
    } else if (uiStyle == "windows") {
        QQuickStyle::setStyle("Windows");
        uiStyleName = "windows";
    } else if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11) {
        QQuickStyle::setStyle("FluentWinUI3");
        uiStyleName = "fluentwinui3";
    } else {
        QQuickStyle::setStyle("Windows");
        uiStyleName = "windows";
    }
#elif defined(Q_OS_MACOS)
    if (uiStyle == "fluentwinui3") {
        QQuickStyle::setStyle("FluentWinUI3");
        uiStyleName = "fluentwinui3";
    } else {
        QQuickStyle::setStyle("macOS");
        uiStyleName = "macos";
    }
#elif defined(Q_OS_LINUX)
    if (uiStyle == "fluentwinui3") {
        QQuickStyle::setStyle("FluentWinUI3");
        uiStyleName = "fluentwinui3";
    } else if (uiStyle == "windows") {
        QQuickStyle::setStyle("Windows");
        uiStyleName = "windows";
    } else if (uiStyle == "fusion") {
        QQuickStyle::setStyle("Fusion");
        uiStyleName = "fusion";
    } else if (uiStyle == "imagine") {
        QQuickStyle::setStyle("Imagine");
        uiStyleName = "imagine";
    } else {
        // Let Qt auto-detect the platform theme (Breeze on KDE, generic
        // elsewhere); square corners by default.
        uiStyleName = "auto";
    }
#endif

    // Load mirror configuration from mirrors.json next to the executable
    QString mirrorPath = resolveLauncherDir() + "/mirrors.json";
    if (QFile::exists(mirrorPath))
        mc_mirror_load_config(mirrorPath.toUtf8().constData());

    KernelBridge::setLauncherDir(resolveLauncherDir());
    KernelBridge::initialize("zh", resolveLauncherDir() + "/.minecraft");

    // A crash during installPack can leave the mrpack download / extraction
    // temp dirs behind; sweep them at startup so the next install starts from a
    // clean slate instead of reusing stale pieces.
    const QString mcDir = resolveLauncherDir() + "/.minecraft";
    const QStringList staleTemps = {
        mcDir + "/versions/.mrpack_dl",
        mcDir + "/versions/.mrpack_tmp"
    };
    for (const QString &stale : staleTemps)
        QDir(stale).removeRecursively();

    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/qml");
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    // Qt 6.5+ embeds the Qt QML modules' qmldir/type-info under
    // qrc:/qt-project.org/imports, which shadows the installed Qt's qml dir in
    // the import path order. That embedded copy carries no native plugins
    // (QtQuick, QtQuick.Controls, ...). addImportPath() prepends, so adding the
    // installed Qt qml directory last puts the disk modules (with their plugin
    // DLLs) first; on a deployed install this is skipped via the existence check.
#ifdef QT6_QML_INSTALL_DIR
    const QString qtInstallQml = QStringLiteral(QT6_QML_INSTALL_DIR);
    if (QFileInfo::exists(qtInstallQml + "/QtQuick/qmldir"))
        engine.addImportPath(qtInstallQml);
#endif

    engine.rootContext()->setContextProperty("kernel", KernelBridge::instance());
    KernelBridge::instance()->setEngine(&engine);

    bool isWin11 = false;
#if defined(Q_OS_WIN)
    isWin11 = QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
#endif
    engine.rootContext()->setContextProperty("isWin11", isWin11);
    engine.rootContext()->setContextProperty("uiStyleName", uiStyleName);

    engine.addImageProvider("tinted", new TintedIconProvider);

    ModIconProvider *modIconProvider = new ModIconProvider;
    modIconProvider->setIconsDir(resolveLauncherDir() + "/cache/icons");
    engine.addImageProvider("modicon", modIconProvider);

    QQmlComponent themeComponent(&engine, QUrl("qrc:///qml/theme/AppTheme.qml"));
    QObject *theme = themeComponent.create();
    if (themeComponent.isError())
        qWarning("Theme error: %s", qPrintable(themeComponent.errorString()));
    if (theme) {
        engine.rootContext()->setContextProperty("Theme", theme);

        // Apply saved color scheme before the window is created so the very
        // first frame is already correct. theme/mode is the current key;
        // theme/colorScheme is the legacy one.
        QString settingsPath = resolveLauncherDir() + "/settings.ini";
        QSettings savedSettings(settingsPath, QSettings::IniFormat);
        QString scheme = savedSettings.value("theme/mode",
                           savedSettings.value("theme/colorScheme", "light").toString())
                         .toString();
        if (scheme != "dark" && scheme != "light" && scheme != "system")
            scheme = "system";
        KernelBridge::instance()->applyThemeMode(scheme);
    }

    I18nManager *i18n = new I18nManager(resolveLauncherDir());
    engine.rootContext()->setContextProperty("I18n", i18n);
    qWarning("I18N currentLang=%s", qPrintable(i18n->currentLangKey()));

    qmlRegisterType<VersionGroupModel>("Beacon", 1, 0, "VersionGroupModel");

    qint64 tStart = QDateTime::currentMSecsSinceEpoch();
    mc_info("[Startup] kernel/theme/i18n init done in %lldms",
QDateTime::currentMSecsSinceEpoch() - tStart);

    mc_info("[Startup] loading main.qml...");
    engine.load(QUrl("qrc:///qml/main.qml"));

    if (engine.rootObjects().isEmpty()) {
        splash.close();
        return -1;
    }

    mc_info("[Startup] main.qml loaded in %lldms", QDateTime::currentMSecsSinceEpoch() - tStart);

    auto *mainWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!mainWindow)
        splash.close();

    if (mainWindow) {
        auto *kb = KernelBridge::instance();
        kb->setMainWindow(mainWindow);
        // Applies the dark-mode title bar (Mica backdrop is disabled in
        // WindowEffects; the translucent Mica path leaves a persistent gray
        // block on some Windows 11 builds).
        kb->applyWindowTransparency(
            kb->settingsManager()->value("system/transparency", true).toBool());
        QObject::connect(mainWindow, &QQuickWindow::frameSwapped, mainWindow,
            [tStart, &splash]() {
                mc_info("[Startup] first frame rendered in %lldms",
                        QDateTime::currentMSecsSinceEpoch() - tStart);
                splash.close();
            },
            Qt::SingleShotConnection);
    }

    // Periodically reclaim transient JS allocations and unused compiled caches
    QTimer gcTimer;
    gcTimer.setInterval(60000);
    QObject::connect(&gcTimer, &QTimer::timeout, [&engine]() {
        engine.collectGarbage();
        engine.trimComponentCache();
    });
    gcTimer.start();

    int ret = app.exec();
    KernelBridge::shutdown();
    // The QML engine / QApplication teardown that runs after exec() can
    // deadlock with the (detached) download-pool threads' Qt-network cleanup
    // and never return, leaving the process in the background after the window
    // is closed. Everything is already torn down above (all managers cancelled,
    // worker threads stopped, download pool cleaned up, log flushed), so
    // terminating here is safe and guarantees a prompt exit.
    #ifdef Q_OS_WIN
    mc_info("[Bridge] Exit (pid=%lu)", (unsigned long)GetCurrentProcessId());
    // ExitProcess would run DLL detach handlers (Qt6 DllMain) which can hang on
    // the leftover download/network threads; TerminateProcess skips all of that
    // and guarantees immediate termination.
    TerminateProcess(GetCurrentProcess(), (UINT)ret);
    return 0;   // unreachable; keeps the compiler happy that qMain always returns
#else
    mc_info("[Bridge] Exit (pid=%lld)", (long long)QCoreApplication::applicationPid());
    // Same rationale as TerminateProcess above: running the QML engine /
    // QApplication destructors can deadlock with the leftover download/network
    // threads and leave the process hanging after the window closes. Everything
    // is already torn down, so force-exit without running destructors.
    std::_Exit(ret);
    return 0;   // unreachable
#endif
}

#include "main.moc"
