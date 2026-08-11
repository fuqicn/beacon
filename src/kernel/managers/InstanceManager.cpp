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
#include <QUrl>
#include <QDirIterator>

static QString localizeDir(const QString &dir)
{
    QString d = dir.trimmed();
    if (d.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        QUrl u(d);
        if (u.isLocalFile()) {
            QString local = u.toLocalFile();
            if (!local.isEmpty()) return local;
        }
    }
    return d;
}

static bool copyRecursively(const QString &src, const QString &dst)
{
    QDir dstDir;
    if (!dstDir.mkpath(dst)) return false;

    QDir srcDir(src);
    for (const auto &info : srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString srcPath = info.absoluteFilePath();
        const QString dstPath = QDir(dst).filePath(info.fileName());
        if (info.isDir()) {
            if (!copyRecursively(srcPath, dstPath)) return false;
        } else {
            if (!QFile::copy(srcPath, dstPath)) return false;
        }
    }
    return true;
}

static QString uniqueDirPath(const QString &baseDir, QString name)
{
    QString candidate = name;
    int n = 1;
    while (QFile::exists(QDir(baseDir).filePath(candidate))) {
        candidate = QStringLiteral("%1 (%2)").arg(name).arg(n++);
    }
    return candidate;
}

// Rewrite the "id" field inside a version JSON to match its folder name,
// and fix any sibling version JSONs that inherit from the old id.
static bool fixVersionJsonId(const QString &rootDir, const QString &oldId, const QString &newId)
{
    if (oldId == newId) return true;

    QDir versionsDir(QDir(rootDir).filePath("versions"));
    QString selfPath = QDir(versionsDir.filePath(newId)).filePath(newId + ".json");
    QFile f(selfPath);
    if (f.open(QIODevice::ReadWrite)) {
        QByteArray raw = f.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            obj["id"] = newId;
            f.resize(0);
            f.write(QJsonDocument(obj).toJson());
            f.close();
        }
    }

    for (const auto &d : versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString sub = QDir(versionsDir.filePath(d.fileName())).filePath(d.fileName() + ".json");
        QFile sf(sub);
        if (sf.open(QIODevice::ReadWrite)) {
            QByteArray raw = sf.readAll();
            QJsonDocument doc = QJsonDocument::fromJson(raw);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj["inheritsFrom"].toString() == oldId) {
                    obj["inheritsFrom"] = newId;
                    sf.resize(0);
                    sf.write(QJsonDocument(obj).toJson());
                }
            }
            sf.close();
        }
    }
    return true;
}

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
            m_rootDirs << localizeDir(v.toString());
    }

    m_selectedId = root["selectedId"].toString();
    m_currentRootDir = localizeDir(root["currentRootDir"].toString());
    if (m_currentRootDir.isEmpty() && !m_rootDirs.isEmpty())
        m_currentRootDir = m_rootDirs.first();
}

void InstanceManager::saveConfig()
{
    QJsonObject root;
    QJsonArray dirs;
    for (const auto &d : m_rootDirs)
        dirs.append(d);
    root["rootDirs"] = dirs;
    root["selectedId"] = m_selectedId;
    root["currentRootDir"] = m_currentRootDir;

    QFile f(m_configPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson());
}

QVariantList InstanceManager::instances() const
{
    if (m_currentRootDir.isEmpty())
        return m_instances;
    QVariantList out;
    for (const auto &v : m_instances) {
        if (v.toMap()["rootDir"].toString() == m_currentRootDir)
            out.append(v);
    }
    return out;
}

void InstanceManager::setDefaultRootDir(const QString &mcDir)
{
    const QString local = localizeDir(mcDir);
    if (m_rootDirs.isEmpty()) {
        m_rootDirs << local;
        m_currentRootDir = local;
        saveConfig();
        emit rootDirsChanged();
        emit currentRootDirChanged();
    } else if (m_currentRootDir.isEmpty()) {
        m_currentRootDir = m_rootDirs.contains(mcDir) ? mcDir : m_rootDirs.first();
        saveConfig();
        emit currentRootDirChanged();
    }
}

void InstanceManager::addRootDir(const QString &dir)
{
    const QString local = localizeDir(dir);
    if (!m_rootDirs.contains(local))
        m_rootDirs << local;
    if (m_currentRootDir != local) {
        m_currentRootDir = local;
        emit currentRootDirChanged();
    }
    saveConfig();
    emit rootDirsChanged();
    scanInstances();
}

void InstanceManager::removeRootDir(const QString &dir)
{
    m_rootDirs.removeAll(dir);
    if (m_currentRootDir == dir) {
        m_currentRootDir = m_rootDirs.isEmpty() ? QString() : m_rootDirs.first();
        emit currentRootDirChanged();
    }
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

// Detect the loader/edition state of a version instance, mirroring PCL's
// McInstanceState so we can pick a matching block/loader icon.
static QString detectInstanceState(const QJsonObject &root, const QString &verId)
{
    if (root["type"].toString() == "snapshot")
        return "snapshot";

    QString probe = QString(verId).toLower();
    QJsonArray libs = root["libraries"].toArray();
    for (const auto &v : libs) {
        const QString n = v.toObject()["name"].toString().toLower();
        if (probe.indexOf("neoforge") >= 0 || n.contains("net.neoforged"))
            return "neoforge";
        if (probe.indexOf("fabric") >= 0 || n.contains("net.fabricmc"))
            return "fabric";
        if (n.contains("org.quiltmc"))
            return "quilt";
        if (n.contains("net.minecraftforge"))
            return "forge";
    }
    for (const auto &v : libs) {
        const QString n = v.toObject()["name"].toString().toLower();
        if (n.contains("net.minecraftforge"))
            return "forge";
    }
    if (root["mainClass"].toString().toLower().contains("optifine"))
        return "optifine";
    return "release";
}

static QString iconKeyForState(const QString &state)
{
    // PCL mapping: original->grass, snapshot->commandblock, old->cobblestone,
    // forge->anvil, neoforge->neoforge, fabric/quilt->fabric, optifine->grasspath,
    // fool->goldblock, else->redstoneblock.
    if (state == "snapshot") return "commandblock";
    if (state == "old") return "cobblestone";
    if (state == "forge") return "anvil";
    if (state == "neoforge") return "neoforge";
    if (state == "fabric" || state == "quilt") return "fabric";
    if (state == "optifine") return "grasspath";
    if (state == "fool") return "goldblock";
    return "grass";
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
        QJsonObject root;
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            root = doc.object();

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

        QString state = detectInstanceState(root, verId);
        if (inst.type == "old_beta" || inst.type == "old_alpha")
            state = "old";
        if (QFile::exists(d.filePath() + "/icon.png"))
            state = "custom";

        QVariantMap vm;
        vm["id"] = inst.id;
        vm["name"] = inst.name;
        vm["type"] = inst.type;
        vm["state"] = state;
        vm["iconKey"] = iconKeyForState(state);
        vm["customIcon"] = QFile::exists(d.filePath() + "/icon.png")
                           ? d.filePath() + "/icon.png" : "";
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
    for (const auto &v : m_instances) {
        QVariantMap vm = v.toMap();
        if (vm["id"].toString() == id) {
            QString root = vm["rootDir"].toString();
            if (!root.isEmpty() && root != m_currentRootDir) {
                m_currentRootDir = root;
                saveConfig();
                emit currentRootDirChanged();
                emit instancesChanged();
            }
            break;
        }
    }
    if (m_selectedId != id) {
        m_selectedId = id;
        saveConfig();
        emit selectedChanged();
    }
}

void InstanceManager::setCurrentRootDir(const QString &dir)
{
    const QString local = localizeDir(dir);
    if (m_currentRootDir == local) return;
    if (!m_rootDirs.contains(local))
        m_rootDirs << local;
    m_currentRootDir = local;
    saveConfig();
    emit currentRootDirChanged();
    emit rootDirsChanged();
    emit instancesChanged();
}

QString InstanceManager::copyInstance(const QString &id, const QString &newId)
{
    QVariantMap vm;
    for (const auto &v : m_instances) {
        QVariantMap cand = v.toMap();
        if (cand["id"].toString() == id) { vm = cand; break; }
    }
    if (vm.isEmpty()) return QString();

    QString rootDir = vm["rootDir"].toString();
    if (rootDir.isEmpty()) return QString();

    QString baseName = newId.trimmed();
    if (baseName.isEmpty())
        baseName = id + " Copy";
    baseName = uniqueDirPath(QDir(rootDir).filePath("versions"), baseName);

    QString src = vm["verDir"].toString();
    QString dst = QDir(QDir(rootDir).filePath("versions")).filePath(baseName);
    if (!copyRecursively(src, dst)) return QString();

    // Fix the copied version JSON so its id matches the new folder name.
    QString jsonPath = QDir(dst).filePath(baseName + ".json");
    QFile f(jsonPath);
    if (f.open(QIODevice::ReadWrite)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj["id"].toString() == id) {
                obj["id"] = baseName;
                f.resize(0);
                f.write(QJsonDocument(obj).toJson());
            }
        }
        f.close();
    }

    mc_info("Instance copied: %s -> %s", id.toUtf8().constData(), baseName.toUtf8().constData());
    scanInstances();
    return baseName;
}

bool InstanceManager::renameInstance(const QString &id, const QString &newId)
{
    const QString target = newId.trimmed();
    if (target.isEmpty() || target == id) return false;

    QVariantMap vm;
    for (const auto &v : m_instances) {
        QVariantMap cand = v.toMap();
        if (cand["id"].toString() == id) { vm = cand; break; }
    }
    if (vm.isEmpty()) return false;

    QString rootDir = vm["rootDir"].toString();
    QString versionsDir = QDir(rootDir).filePath("versions");
    if (QFile::exists(QDir(versionsDir).filePath(target))) return false;

    QDir dir(vm["verDir"].toString());
    if (!dir.exists()) return false;

    if (!dir.rename(vm["verDir"].toString(), QDir(versionsDir).filePath(target)))
        return false;

    // Rename json + jar to the new id.
    QString oldJson = QDir(QDir(versionsDir).filePath(target)).filePath(id + ".json");
    QString newJson = QDir(QDir(versionsDir).filePath(target)).filePath(target + ".json");
    if (QFile::exists(oldJson) && !QFile::rename(oldJson, newJson))
        return false;

    QString oldJar = QDir(QDir(versionsDir).filePath(target)).filePath(id + ".jar");
    QString newJar = QDir(QDir(versionsDir).filePath(target)).filePath(target + ".jar");
    if (QFile::exists(oldJar) && !QFile::rename(oldJar, newJar))
        return false;

    fixVersionJsonId(rootDir, id, target);

    if (m_selectedId == id) {
        m_selectedId = target;
        emit selectedChanged();
    }

    mc_info("Instance renamed: %s -> %s", id.toUtf8().constData(), target.toUtf8().constData());
    saveConfig();
    scanInstances();
    return true;
}

bool InstanceManager::importVersionFolder(const QString &rootDir, const QString &srcPath)
{
    QString src = localizeDir(srcPath);
    QFileInfo fi(src);
    if (!fi.isDir()) return false;

    QString versionsDir = QDir(rootDir).filePath("versions");
    QDir().mkpath(versionsDir);

    QString targetName = uniqueDirPath(versionsDir, fi.fileName());
    QString dst = QDir(versionsDir).filePath(targetName);
    if (!copyRecursively(src, dst)) return false;

    // Ensure json is named <folder>.json so the scanner picks it up.
    QString jsonPath = QDir(dst).filePath(targetName + ".json");
    if (!QFile::exists(jsonPath)) {
        QDirIterator it(dst, {"*.json"}, QDir::Files);
        if (it.hasNext()) {
            QString found = it.next();
            if (QFile::exists(found) && found != jsonPath)
                QFile::copy(found, jsonPath);
        }
    }

    fixVersionJsonId(rootDir, fi.fileName(), targetName);

    mc_info("Instance imported: %s -> %s", fi.fileName().toUtf8().constData(),
            targetName.toUtf8().constData());
    scanInstances();
    return true;
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
