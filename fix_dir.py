from pathlib import Path
path = Path(r'E:\launcher\launcher\src\cpp\main.cpp')
content = path.read_text(encoding='utf-8')

old = '''#ifdef Q_OS_WIN
    QString exeDir = QCoreApplication::applicationDirPath();
    // If running from .../x1/beacon/, use .../x1/game/ for persistent data.
    QFileInfo exeInfo(exeDir);
    if (exeInfo.dir().dirName() == QLatin1String("beacon")) {
        QString gameDir = exeInfo.dir().path() + QLatin1String("/..")
                        + QLatin1String("/game");
        QDir().mkpath(gameDir);
        return gameDir;
    }
    return exeDir;'''

new = '''#ifdef Q_OS_WIN
    QString exeDir = QCoreApplication::applicationDirPath();
    // If running from .../x1/beacon/, use .../x1/game/ for persistent data.
    // Also fall back when the launcher exe is directly in the dist root (next to a
    // sibling "game" directory created by the packager).
    QFileInfo exeInfo(exeDir);
    if (exeInfo.dir().dirName() == QLatin1String("beacon")) {
        // .../x1/beacon/Beacon.exe -> .../x1/game/
        QString gameDir = exeInfo.dir().path() + QLatin1String("/..")
                        + QLatin1String("/game");
        QDir().mkpath(gameDir);
        return gameDir;
    }
    // Directly in dist/ or similar: check for sibling game/ directory.
    {
        QString gameDir = exeDir + QLatin1String("/game");
        if (QDir(gameDir).exists()) {
            QDir().mkpath(gameDir);
            return gameDir;
        }
    }
    return exeDir;'''

if old in content:
    content = content.replace(old, new)
    path.write_text(content, encoding='utf-8')
    print('Fixed resolveLauncherDir')
else:
    print('Pattern not found')
