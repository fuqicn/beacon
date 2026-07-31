#ifndef SKINMANAGER_H
#define SKINMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QDir>

class SkinManager : public QObject
{
    Q_OBJECT
public:
    explicit SkinManager(QObject *parent = nullptr);

    void setCacheDir(const QString &dir);

    Q_INVOKABLE void fetchSkin(const QString &uuid);
    Q_INVOKABLE QString cachedSkin(const QString &uuid) const;
    Q_INVOKABLE bool hasCachedSkin(const QString &uuid) const;

signals:
    void skinReady(const QString &uuid);

private slots:
    void onProfileFetched(const QString &uuid);
    void onSkinDownloaded(const QString &uuid);

private:
    QImage cropFace(const QImage &skin) const;

    QNetworkAccessManager *m_nam;
    QString m_cacheDir;
};

#endif
