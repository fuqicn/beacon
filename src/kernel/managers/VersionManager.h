#ifndef VERSIONMANAGER_H
#define VERSIONMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <QTimer>

#include <mc_manifest.h>
#include <mc_version.h>

class VersionManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString latestRelease READ latestRelease NOTIFY manifestReady)
    Q_PROPERTY(QString latestSnapshot READ latestSnapshot NOTIFY manifestReady)
    Q_PROPERTY(int versionCount READ versionCount NOTIFY manifestReady)

public:
    explicit VersionManager(QObject *parent = nullptr);
    ~VersionManager() override;

    bool loading() const { return m_loading; }
    QString latestRelease() const { return QString::fromUtf8(m_manifest.latest_release); }
    QString latestSnapshot() const { return QString::fromUtf8(m_manifest.latest_snapshot); }
    int versionCount() const { return m_manifest.count; }

    Q_INVOKABLE void fetchManifest(const QString &mirror = QString());
    Q_INVOKABLE QVariantList listVersions(const QString &type = "all") const;
    Q_INVOKABLE QVariantList searchVersions(const QString &query) const;
    Q_INVOKABLE void fetchVersionInfo(const QString &versionId, const QString &mirror = QString());
    Q_INVOKABLE QVariantList classifyVersions();

signals:
    void manifestReady();
    void versionInfoReady(const QString &versionId, const QVariantMap &info);
    void loadingChanged();
    void errorOccurred(const QString &message);

private:
    void doFetchManifest(const QString &mirror);
    bool isAprilFools(const char *id, qint64 releaseTime) const;
    void buildCategories();
    McManifest m_manifest;
    bool m_loading = false;
    QTimer m_delayTimer;
    QString m_pendingMirror;
    QVariantList m_cachedCategories;
    bool m_categoriesDirty = true;
};

#endif
