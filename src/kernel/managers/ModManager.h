#ifndef MODMANAGER_H
#define MODMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <mc_mod.h>

class ModManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)

public:
    explicit ModManager(QObject *parent = nullptr);
    ~ModManager() override;

    bool searching() const { return m_searching; }

    Q_INVOKABLE void search(const QString &query, const QString &sort = "relevance",
                            int limit = 20);
    Q_INVOKABLE void getProject(const QString &projectId);
    Q_INVOKABLE void getVersions(const QString &projectId,
                                 const QString &mcVersion = QString(),
                                 const QString &loader = QString());

signals:
    void searchingChanged();
    void searchCompleted(const QVariantList &results);
    void projectLoaded(const QVariantMap &project);
    void versionsLoaded(const QVariantList &versions);
    void errorOccurred(const QString &message);

private:
    bool m_searching = false;
};

#endif
