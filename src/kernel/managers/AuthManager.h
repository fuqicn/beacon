#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QObject>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <thread>
#include <mc_auth.h>
#include <mc_auth_msa.h>

class AuthManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(int currentAccountIndex READ currentAccountIndex NOTIFY currentAccountChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY currentAccountChanged)
    Q_PROPERTY(QString username READ username NOTIFY currentAccountChanged)
    Q_PROPERTY(QString uuid READ uuid NOTIFY currentAccountChanged)
    Q_PROPERTY(int loginType READ loginType NOTIFY currentAccountChanged)
    Q_PROPERTY(bool loggingIn READ loggingIn NOTIFY loggingInChanged)

public:
    explicit AuthManager(QObject *parent = nullptr);
    ~AuthManager() override = default;

    QVariantList accounts() const;
    int currentAccountIndex() const { return m_currentIndex; }
    bool loggedIn() const;
    QString username() const;
    QString uuid() const;
    int loginType() const;
    bool loggingIn() const { return m_loggingIn; }

    Q_INVOKABLE void addMicrosoftAccount();
    Q_INVOKABLE void addOfflineAccount(const QString &username);
    Q_INVOKABLE void removeAccount(int index);
    Q_INVOKABLE void switchAccount(int index);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshBeforeLaunch();

    void setLauncherDir(const QString &dir);
    const McAuthSession *session() const { return &m_session; }

signals:
    void accountsChanged();
    void currentAccountChanged();
    void loggingInChanged();
    void loginCompleted(const QString &username, const QString &uuid);
    void loginFailed(const QString &error);
    void refreshCompleted(bool success);

private:
    struct Account {
        int type = 0;
        QString username;
        QString uuid;
        QString accessToken;
        QString clientToken;
        QString refreshToken;
    };


    void saveAccounts();
    void loadAccounts();
    void activateAccount(int index);

    McAuthSession m_session;
    QList<Account> m_accounts;
    int m_currentIndex = -1;
    bool m_loggingIn = false;
    QString m_accountsPath;
    QString m_cacheDir;
};
#endif
