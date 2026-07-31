#include "InstanceManager.h"
#include <mc_log.h>
#include <mc_path.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfoList>
#include <QStandardPaths>

InstanceManager::InstanceManager(QObject *parent) : QObject(parent)
{
    char path[1024];
    mc_path_appdata(path, sizeof(path));
    mc_path_join(path, "launcher", path, sizeof(path));
    mc_path_mkdir_p(path);
    mc_path_join(path, "instances.json", path, sizeof(path));
    m_configPath = QString::fromUtf8(path);

    loadConfig();
}

InstanceManager::~InstanceManager() = default;

void InstanceManager::loadConfig()
{
    QFile f(m_configPath);
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    QJsonObject root = doc.object();

    m_rootDirs.clear();
    if (root.contains("rootDirs")) {
        for (const auto &v : root["rootDirs"].toArray())
            m_rootDirs << v.toString();
    }

    m_selectedId = root["selectedId"].toString();
}

void InstanceManager::saveConfig()
{
    QJsonObject root;
    QJsonArray dirs;
    for (const auto &d : m_rootDirs)
        dirs.append(d);
    root["rootDirs"] = dirs;
    root["selectedId"] = m_selectedId;

    QFile f(m_configPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson());
}

void InstanceManager::setDefaultRootDir(const QString &mcDir)
{
    if (m_rootDirs.isEmpty()) {
        m_rootDirs << mcDir;
        saveConfig();
        emit rootDirsChanged();
    }
}

void InstanceManager::addRootDir(const QString &dir)
{
    if (!m_rootDirs.contains(dir)) {
        m_rootDirs << dir;
        saveConfig();
        emit rootDirsChanged();
        scanInstances();
    }
}

void InstanceManager::removeRootDir(const QString &dir)
{
    m_rootDirs.removeAll(dir);
    saveConfig();
    emit rootDirsChanged();
    scanInstances();
}

void InstanceManager::removeInstance(const QString &id)
{
    for (int i = 0; i < m_instances.size(); ++i) {
        QVariantMap vm = m_instances[i].toMap();
        if (vm["id"].toString() == id) {
            QString verDir = vm["verDir"].toString();
            if (!verDir.isEmpty()) {
                QDir dir(verDir);
                if (dir.exists()) {
                    if (!QFile::moveToTrash(verDir)) {
                        mc_error("moveToTrash failed for %s, falling back to permanent delete",
                                 verDir.toUtf8().constData());
                        if (!dir.removeRecursively())
                            mc_error("removeRecursively failed for %s", verDir.toUtf8().constData());
                    }
                }
            }
            m_instances.removeAt(i);
            if (m_selectedId == id) {
                m_selectedId.clear();
                emit selectedChanged();
            }
            saveConfig();
            emit instancesChanged();
            return;
        }
    }
}

void InstanceManager::scanInstances()
{
    m_instances.clear();

    for (const auto &rootDir : m_rootDirs)
        scanDirectory(rootDir);

    emit instancesChanged();
}

void InstanceManager::scanDirectory(const QString &rootDir)
{
    QDir versionsDir(QDir(rootDir).filePath("versions"));
    if (!versionsDir.exists()) return;

    QFileInfoList dirs = versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &d : dirs) {
        QString verId = d.fileName();
        QString jsonPath = d.filePath() + "/" + verId + ".json";
        QString jarPath = d.filePath() + "/" + verId + ".jar";

        if (!QFile::exists(jsonPath))
            continue;

        InstanceInfo inst;
        inst.id = verId;
        inst.name = verId;
        inst.rootDir = rootDir;
        inst.verDir = d.filePath();
        inst.jsonPath = jsonPath;
        inst.jarPath = jarPath;
        inst.hasJar = QFile::exists(jarPath);
        inst.libraryCount = 0;
        inst.javaMajorVersion = 17;
        inst.mainClass = "";

        // Parse JSON for metadata
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            QJsonObject root = doc.object();

            inst.type = root["type"].toString("release");
            inst.mainClass = root["mainClass"].toString();

            // Follow inheritsFrom chain to find javaVersion
            QString lookupId = verId;
            while (!lookupId.isEmpty()) {
                QString lookupPath = QDir(rootDir).filePath(
                    QStringLiteral("versions/%1/%1.json").arg(lookupId));
                QFile lf(lookupPath);
                if (lf.open(QIODevice::ReadOnly)) {
                    QJsonObject lr = QJsonDocument::fromJson(lf.readAll()).object();
                    if (lr.contains("javaVersion")) {
                        inst.javaMajorVersion = lr["javaVersion"].toObject()["majorVersion"].toInt(17);
                        break;
                    }
                    lookupId = lr["inheritsFrom"].toString();
                } else {
                    break;
                }
            }

            if (root.contains("libraries"))
                inst.libraryCount = root["libraries"].toArray().size();
        }

        QVariantMap vm;
        vm["id"] = inst.id;
        vm["name"] = inst.name;
        vm["type"] = inst.type;
        vm["rootDir"] = inst.rootDir;
        vm["verDir"] = inst.verDir;
        vm["jsonPath"] = inst.jsonPath;
        vm["jarPath"] = inst.jarPath;
        vm["hasJar"] = inst.hasJar;
        vm["libraryCount"] = inst.libraryCount;
        vm["javaMajorVersion"] = inst.javaMajorVersion;
        vm["mainClass"] = inst.mainClass;

        m_instances.append(vm);
    }
}

void InstanceManager::selectInstance(const QString &id)
{
    if (m_selectedId != id) {
        m_selectedId = id;
        saveConfig();
        emit selectedChanged();
    }
}

QVariantMap InstanceManager::getSelectedInstance() const
{
    for (const auto &v : m_instances) {
        QVariantMap vm = v.toMap();
        if (vm["id"].toString() == m_selectedId)
            return vm;
    }
    return QVariantMap();
}
