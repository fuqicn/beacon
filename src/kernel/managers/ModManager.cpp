#include "ModManager.h"
#include <mc_log.h>
#include <cstring>

ModManager::ModManager(QObject *parent) : QObject(parent) {}
ModManager::~ModManager() {}

static int sortFromString(const QString &s)
{
    if (s == "downloads") return MC_MOD_SORT_DOWNLOADS;
    if (s == "follows") return MC_MOD_SORT_FOLLOWS;
    if (s == "newest") return MC_MOD_SORT_NEWEST;
    if (s == "updated") return MC_MOD_SORT_UPDATED;
    return MC_MOD_SORT_RELEVANCE;
}

void ModManager::search(const QString &query, const QString &sort, int limit)
{
    if (m_searching) return;
    m_searching = true;
    emit searchingChanged();

    McModProject results[100];
    for (int i = 0; i < 100; ++i) mc_mod_project_init(&results[i]);

    int count = mc_mod_search(
        query.isEmpty() ? nullptr : query.toUtf8().constData(),
        MC_MOD_MODRINTH,
        qMin(limit, 100),
        sortFromString(sort),
        results, 100);

    QVariantList list;
    for (int i = 0; i < count; ++i) {
        QVariantMap m;
        m["id"] = QString::fromUtf8(results[i].id);
        m["slug"] = QString::fromUtf8(results[i].slug);
        m["name"] = QString::fromUtf8(results[i].name);
        m["description"] = QString::fromUtf8(results[i].description);
        m["logoUrl"] = QString::fromUtf8(results[i].logo_url);
        m["downloadCount"] = results[i].download_count;
        m["gameVersions"] = QString::fromUtf8(results[i].game_versions);
        m["loaders"] = QString::fromUtf8(results[i].loaders);
        m["websiteUrl"] = QString::fromUtf8(results[i].website_url);
        list.append(m);
        mc_mod_project_free(&results[i]);
    }

    m_searching = false;
    emit searchingChanged();
    emit searchCompleted(list);
}

void ModManager::getProject(const QString &projectId)
{
    McModProject project;
    mc_mod_project_init(&project);

    if (!mc_mod_get_project(projectId.toUtf8().constData(), MC_MOD_MODRINTH, &project)) {
        emit errorOccurred(QString("Project not found: %1").arg(projectId));
        return;
    }

    QVariantMap m;
    m["id"] = QString::fromUtf8(project.id);
    m["slug"] = QString::fromUtf8(project.slug);
    m["name"] = QString::fromUtf8(project.name);
    m["description"] = QString::fromUtf8(project.description);
    m["logoUrl"] = QString::fromUtf8(project.logo_url);
    m["downloadCount"] = project.download_count;
    m["gameVersions"] = QString::fromUtf8(project.game_versions);
    m["loaders"] = QString::fromUtf8(project.loaders);
    m["websiteUrl"] = QString::fromUtf8(project.website_url);

    mc_mod_project_free(&project);
    emit projectLoaded(m);
}

void ModManager::getVersions(const QString &projectId,
                             const QString &mcVersion,
                             const QString &loader)
{
    McModFile files[100];
    for (int i = 0; i < 100; ++i) mc_mod_file_init(&files[i]);

    int count = mc_mod_get_versions(
        projectId.toUtf8().constData(), MC_MOD_MODRINTH,
        mcVersion.isEmpty() ? nullptr : mcVersion.toUtf8().constData(),
        loader.isEmpty() ? nullptr : loader.toUtf8().constData(),
        files, 100);

    QVariantList list;
    for (int i = 0; i < count; ++i) {
        QVariantMap m;
        m["id"] = QString::fromUtf8(files[i].id);
        m["projectId"] = QString::fromUtf8(files[i].project_id);
        m["displayName"] = QString::fromUtf8(files[i].display_name);
        m["fileName"] = QString::fromUtf8(files[i].file_name);
        m["downloadUrl"] = QString::fromUtf8(files[i].download_url);
        m["sha1"] = QString::fromUtf8(files[i].sha1);
        m["size"] = (qlonglong)files[i].size;
        m["releaseType"] = QString::fromUtf8(files[i].release_type);
        m["releaseDate"] = QString::fromUtf8(files[i].release_date);
        m["downloadCount"] = files[i].download_count;
        list.append(m);
        mc_mod_file_free(&files[i]);
    }

    emit versionsLoaded(list);
}
