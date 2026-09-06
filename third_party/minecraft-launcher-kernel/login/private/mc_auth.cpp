/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_auth.h"
#include "mc_http.h"
#include "mc_log.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <QtCore/QUuid>
#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QTextStream>

static const char *DEFAULT_AUTH_URL = "https://authserver.mojang.com";

void mc_auth_init(McAuthSession *session) {
    memset(session, 0, sizeof(McAuthSession));
    strcpy(session->server_url, DEFAULT_AUTH_URL);
    strcpy(session->user_type, "mojang");
}

void mc_auth_set_server(McAuthSession *session, const char *url) {
    if (session && url)
        strncpy(session->server_url, url, sizeof(session->server_url) - 1);
}

void mc_auth_generate_client_token(McAuthSession *session) {
    if (!session) return;
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray ba = uuid.toUtf8();
    strncpy(session->client_token, ba.constData(), sizeof(session->client_token) - 1);
}

static int do_auth_request(McAuthSession *session, const char *endpoint,
    const char *json_body, char *out_access_token, size_t out_access_size,
    char *out_uuid, size_t out_uuid_size,
    char *out_name, size_t out_name_size)
{
    char url[1024];
    snprintf(url, sizeof(url), "%s%s", session->server_url, endpoint);

    mc_debug("[auth] POST %s", url);
    mc_debug("[auth] request: %s", json_body);

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 30000);

    McHttpResponse *resp = mc_http_post_json(&client, url, json_body);
    if (!resp) {
        snprintf(session->error, sizeof(session->error), "No response from server");
        return 0;
    }

    int ok = 0;
    if (resp->success && resp->data) {
        mc_debug("[auth] response: %s", resp->data);
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data));
        if (doc.isObject()) {
            QJsonObject jObj = doc.object();
            QJsonValue errVal = jObj.value("errorMessage");
            if (errVal.isString()) {
                QByteArray errMsg = errVal.toString().toUtf8();
                snprintf(session->error, sizeof(session->error), "%s", errMsg.constData());
            } else {
                QJsonValue atVal = jObj.value("accessToken");
                QJsonValue ctVal = jObj.value("clientToken");
                if (atVal.isString()) {
                    QByteArray at = atVal.toString().toUtf8();
                    strncpy(out_access_token, at.constData(), out_access_size - 1);
                }
                if (ctVal.isString()) {
                    QByteArray ct = ctVal.toString().toUtf8();
                    strncpy(session->client_token, ct.constData(), sizeof(session->client_token) - 1);
                }

                QJsonValue profileVal = jObj.value("selectedProfile");
                if (profileVal.isObject()) {
                    QJsonObject profile = profileVal.toObject();
                    QJsonValue pidVal = profile.value("id");
                    QJsonValue pnameVal = profile.value("name");
                    if (pidVal.isString()) {
                        QByteArray pid = pidVal.toString().toUtf8();
                        strncpy(out_uuid, pid.constData(), out_uuid_size - 1);
                    }
                    if (pnameVal.isString()) {
                        QByteArray pname = pnameVal.toString().toUtf8();
                        strncpy(out_name, pname.constData(), out_name_size - 1);
                    }
                }
                ok = 1;
            }
        } else {
            snprintf(session->error, sizeof(session->error), "Failed to parse JSON response");
        }
    } else {
        if (resp->error[0])
            snprintf(session->error, sizeof(session->error), "%s", resp->error);
        else
            snprintf(session->error, sizeof(session->error), "HTTP %ld", resp->status_code);
    }

    mc_http_response_free(resp);
    return ok;
}

int mc_auth_authenticate(McAuthSession *session, const char *username, const char *password) {
    if (!session || !username || !password) return 0;
    session->is_authenticated = 0;
    session->error[0] = '\0';

    if (!session->client_token[0])
        mc_auth_generate_client_token(session);

    char body[2048];
    snprintf(body, sizeof(body),
        "{"
        "\"agent\":{\"name\":\"Minecraft\",\"version\":1},"
        "\"username\":\"%s\","
        "\"password\":\"%s\","
        "\"clientToken\":\"%s\""
        "}",
        username, password, session->client_token);

    char access[MC_AUTH_TOKEN_SIZE] = "";
    char uuid[MC_AUTH_UUID_SIZE] = "";
    char name[MC_AUTH_NAME_SIZE] = "";

    if (do_auth_request(session, "/authenticate", body,
                        access, sizeof(access),
                        uuid, sizeof(uuid),
                        name, sizeof(name)))
    {
        strncpy(session->access_token, access, sizeof(session->access_token) - 1);
        strncpy(session->uuid, uuid, sizeof(session->uuid) - 1);
        strncpy(session->name, name, sizeof(session->name) - 1);
        session->is_authenticated = 1;
        return 1;
    }
    return 0;
}

int mc_auth_refresh(McAuthSession *session) {
    if (!session || !session->access_token[0] || !session->client_token[0]) {
        if (session) snprintf(session->error, sizeof(session->error),
            "No existing session to refresh");
        return 0;
    }
    session->is_authenticated = 0;
    session->error[0] = '\0';

    char body[2048];
    snprintf(body, sizeof(body),
        "{"
        "\"accessToken\":\"%s\","
        "\"clientToken\":\"%s\""
        "}",
        session->access_token, session->client_token);

    char access[MC_AUTH_TOKEN_SIZE] = "";
    char uuid[MC_AUTH_UUID_SIZE] = "";
    char name[MC_AUTH_NAME_SIZE] = "";

    if (do_auth_request(session, "/refresh", body,
                        access, sizeof(access),
                        uuid, sizeof(uuid),
                        name, sizeof(name)))
    {
        strncpy(session->access_token, access, sizeof(session->access_token) - 1);
        if (uuid[0]) strncpy(session->uuid, uuid, sizeof(session->uuid) - 1);
        if (name[0]) strncpy(session->name, name, sizeof(session->name) - 1);
        session->is_authenticated = 1;
        return 1;
    }
    return 0;
}

int mc_auth_validate(McAuthSession *session) {
    if (!session || !session->access_token[0] || !session->client_token[0]) {
        if (session) snprintf(session->error, sizeof(session->error),
            "No session to validate");
        return 0;
    }
    session->error[0] = '\0';

    char url[1024];
    snprintf(url, sizeof(url), "%s/validate", session->server_url);

    char body[1024];
    snprintf(body, sizeof(body),
        "{\"accessToken\":\"%s\",\"clientToken\":\"%s\"}",
        session->access_token, session->client_token);

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);

    McHttpResponse *resp = mc_http_post_json(&client, url, body);
    if (!resp) {
        snprintf(session->error, sizeof(session->error), "No response");
        return 0;
    }

    int ok = 0;
    if (resp->success && resp->status_code == 204) {
        ok = 1;
    } else if (resp->data) {
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data));
        if (doc.isObject()) {
            QJsonObject jObj = doc.object();
            QJsonValue errVal = jObj.value("errorMessage");
            if (errVal.isString()) {
                QByteArray err = errVal.toString().toUtf8();
                snprintf(session->error, sizeof(session->error), "%s", err.constData());
            }
        }
    } else {
        snprintf(session->error, sizeof(session->error), "HTTP %ld", resp->status_code);
    }

    mc_http_response_free(resp);
    return ok;
}

int mc_auth_signout(const char *username, const char *password) {
    if (!username || !password) return 0;

    char body[1024];
    snprintf(body, sizeof(body),
        "{\"username\":\"%s\",\"password\":\"%s\"}",
        username, password);

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);

    McHttpResponse *resp = mc_http_post_json(&client,
        "https://authserver.mojang.com/signout", body);
    int ok = (resp && resp->success && resp->status_code == 204) ? 1 : 0;
    mc_http_response_free(resp);
    return ok;
}

int mc_auth_invalidate(McAuthSession *session) {
    if (!session || !session->access_token[0] || !session->client_token[0]) return 0;

    char url[1024];
    snprintf(url, sizeof(url), "%s/invalidate", session->server_url);

    char body[1024];
    snprintf(body, sizeof(body),
        "{\"accessToken\":\"%s\",\"clientToken\":\"%s\"}",
        session->access_token, session->client_token);

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);

    McHttpResponse *resp = mc_http_post_json(&client, url, body);
    int ok = (resp && resp->success && resp->status_code == 204) ? 1 : 0;
    mc_http_response_free(resp);
    return ok;
}

int mc_auth_save(const McAuthSession *session, const char *path) {
    if (!session || !path) return 0;

    QJsonObject j;
    j["accessToken"] = QString(session->access_token);
    j["clientToken"] = QString(session->client_token);
    j["uuid"] = QString(session->uuid);
    j["name"] = QString(session->name);
    j["userType"] = QString(session->user_type);
    j["isAuthenticated"] = (double)session->is_authenticated;
    j["serverUrl"] = QString(session->server_url);
    j["msaRefreshToken"] = QString(session->msa_refresh_token);

    QJsonDocument doc(j);
    QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);

    int ok = 0;
    QFile file(QString::fromUtf8(path));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(jsonBytes);
        file.close();
        ok = 1;
    }
    return ok;
}

int mc_auth_load(McAuthSession *session, const char *path) {
    if (!session || !path) return 0;
    mc_auth_init(session);

    if (!QFile::exists(QString::fromUtf8(path))) return 0;

    QString data;
    QFile file(QString::fromUtf8(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QTextStream in(&file);
    data = in.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isObject()) return 0;

    QJsonObject jObj = doc.object();

    auto readStr = [&](const char *key, char *dest, size_t destSize) {
        QJsonValue v = jObj.value(key);
        if (v.isString()) {
            QByteArray ba = v.toString().toUtf8();
            strncpy(dest, ba.constData(), destSize - 1);
        }
    };

    readStr("accessToken", session->access_token, sizeof(session->access_token));
    readStr("clientToken", session->client_token, sizeof(session->client_token));
    readStr("uuid", session->uuid, sizeof(session->uuid));
    readStr("name", session->name, sizeof(session->name));
    readStr("userType", session->user_type, sizeof(session->user_type));
    readStr("serverUrl", session->server_url, sizeof(session->server_url));
    readStr("msaRefreshToken", session->msa_refresh_token, sizeof(session->msa_refresh_token));

    QJsonValue authVal = jObj.value("isAuthenticated");
    if (authVal.isDouble())
        session->is_authenticated = (int)authVal.toDouble();

    return session->is_authenticated;
}
