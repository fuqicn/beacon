#include <QGuiApplication>
#include <QCoreApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQuickView>
#include <QQuickWindow>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QOperatingSystemVersion>
#include <QDir>
#include <QDateTime>
#include <QSvgRenderer>
#include <QPainter>
#include <QQuickImageProvider>
#include <QSettings>
#include <QStyleHints>
#include <QThreadPool>
#include <QFile>
#include <QPixmapCache>
#include <QTimer>

#include "KernelBridge.h"
#include <mc_download.h>
#include <mc_log.h>

class TintedIconProvider : public QQuickImageProvider
{
public:
    TintedIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Image) {}

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

        // Simple icon cache keyed on name+tint
        QString cacheKey = name + "/" + QString::number(color.rgba(), 16);
        auto it = m_cache.find(cacheKey);
        if (it != m_cache.end()) {
            QImage cached = *it;
            if (size) *size = cached.size();
            return cached;
        }

        QSvgRenderer renderer(QStringLiteral(":/icons/%1.svg").arg(name));
        if (!renderer.isValid()) {
            if (size) *size = QSize(1, 1);
            QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
            m_cache.insert(cacheKey, empty);
            return empty;
        }

        QSize s = requestedSize.isValid() ? requestedSize : QSize(24, 24);
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
        m_cache.insert(cacheKey, image);
        if (m_cache.size() > 64) {
            auto oldest = m_cache.begin();
            while (oldest != m_cache.end() && oldest != m_cache.find(cacheKey))
                ++oldest;
            if (oldest != m_cache.begin())
                m_cache.erase(m_cache.begin());
        }
        return image;
    }

private:
    QHash<QString, QImage> m_cache;
};

#ifdef Q_OS_WIN
#include <windows.h>

static wchar_t g_crashLogPath[MAX_PATH];

static LONG WINAPI crashHandler(EXCEPTION_POINTERS *exInfo) {
    HANDLE hFile = CreateFileW(g_crashLogPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        char buf[4096];

        DWORD code = exInfo->ExceptionRecord->ExceptionCode;
        PVOID addr = exInfo->ExceptionRecord->ExceptionAddress;

        int len = snprintf(buf, sizeof(buf),
            "=== CRASH at %s ===\n"
            "Exception code: 0x%08lX\n"
            "Exception address: %p\n"
            "RIP: %p\n",
            QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toUtf8().constData(),
            (unsigned long)code,
            addr,
#ifdef _WIN64
            (void *)exInfo->ContextRecord->Rip
#else
            (void *)exInfo->ContextRecord->Eip
#endif
        );
        WriteFile(hFile, buf, len, &written, NULL);
        CloseHandle(hFile);
    }
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
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    bool debugMode = false;
    int downloadJavaVersion = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) { debugMode = true; }
        if (strcmp(argv[i], "--download-java") == 0 && i + 1 < argc) {
            downloadJavaVersion = atoi(argv[i + 1]);
            i++;
        }
    }
    if (downloadJavaVersion > 0) {
        // Log to file for capture
        mc_log_set_file((QCoreApplication::applicationDirPath() + "/java-test.log").toUtf8().constData());

        QCoreApplication app(argc, argv);
        app.setApplicationName("Beacon");
        app.setApplicationVersion("1.0.0");

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
    if (debugMode) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
    }
#endif

    QGuiApplication app(argc, argv);
    app.setApplicationName("Beacon");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Beacon");

    QPixmapCache::setCacheLimit(2048); // 2MB image cache limit

    QThreadPool::globalInstance()->setMaxThreadCount(8);

    {
        QFont f = app.font();
        f.setFamilies({"Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"});
        app.setFont(f);
    }

#ifdef Q_OS_WIN
    {
        QString crashLog = QCoreApplication::applicationDirPath() + "/crash.log";
        crashLog.toWCharArray(g_crashLogPath);
        g_crashLogPath[crashLog.size()] = L'\0';
        SetUnhandledExceptionFilter(crashHandler);
        signal(SIGSEGV, signalHandler);
        signal(SIGABRT, signalHandler);
        signal(SIGFPE, signalHandler);
        signal(SIGILL, signalHandler);
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

#if defined(Q_OS_WIN)
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11)
        QQuickStyle::setStyle("FluentWinUI3");
    else
        QQuickStyle::setStyle("Windows");
#elif defined(Q_OS_MACOS)
    QQuickStyle::setStyle("macOS");
#elif defined(Q_OS_LINUX)
    QQuickStyle::setStyle("Fusion");
#endif

    // Load mirror configuration from mirrors.json next to the executable
    QString mirrorPath = QCoreApplication::applicationDirPath() + "/mirrors.json";
    if (QFile::exists(mirrorPath))
        mc_mirror_load_config(mirrorPath.toUtf8().constData());

    KernelBridge::initialize("zh", QCoreApplication::applicationDirPath() + "/.minecraft");

    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/qml");
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");

    engine.rootContext()->setContextProperty("kernel", KernelBridge::instance());

    bool isWin11 = false;
#if defined(Q_OS_WIN)
    isWin11 = QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
#endif
    engine.rootContext()->setContextProperty("isWin11", isWin11);

    engine.addImageProvider("tinted", new TintedIconProvider);

    QQmlComponent themeComponent(&engine, QUrl("qrc:///qml/theme/Theme.qml"));
    QObject *theme = themeComponent.create();
    if (themeComponent.isError())
        qWarning("Theme error: %s", qPrintable(themeComponent.errorString()));
    if (theme) {
        engine.rootContext()->setContextProperty("Theme", theme);

        // Apply saved color scheme
        QString settingsPath = QCoreApplication::applicationDirPath() + "/settings.ini";
        QSettings savedSettings(settingsPath, QSettings::IniFormat);
        QString scheme = savedSettings.value("theme/colorScheme", "light").toString();
        QGuiApplication::styleHints()->setColorScheme(
            scheme == "dark" ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    }

    QQuickView splashView;
    splashView.setFlags(Qt::SplashScreen | Qt::FramelessWindowHint);
    splashView.setSource(QUrl("qrc:///qml/SplashScreen.qml"));
    if (splashView.status() == QQuickView::Ready) {
        splashView.show();
        app.processEvents();
    }

    engine.load(QUrl("qrc:///qml/main.qml"));

    if (engine.rootObjects().isEmpty())
        return -1;

    splashView.close();

    if (auto *mainWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
        auto *kb = KernelBridge::instance();
        kb->setMainWindow(mainWindow);
        kb->applyWindowTransparency(
            kb->settingsManager()->value("system/transparency", true).toBool());
    }

    int ret = app.exec();
    KernelBridge::shutdown();
    return ret;
}
