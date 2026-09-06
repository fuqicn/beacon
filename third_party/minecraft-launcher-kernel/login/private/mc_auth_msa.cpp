/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_auth_msa.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include "mc_http.h"
#include "mc_log.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <QtCore/QUrl>
#include <QtCore/QProcess>
#include <QtCore/QElapsedTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#ifdef _WIN32
#include <windows.h>
#endif

#define MSA_CLIENT_ID "00000000402B5328"

static void url_encode(const char *src, char *dst, size_t dst_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else if (c == ' ') {
            dst[j++] = '+';
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0xF];
        }
    }
    dst[j] = '\0';
}

static void copy_to_clipboard(const char *text) {
#ifdef _WIN32
    if (!text || !text[0]) return;
    size_t len = strlen(text);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, len + 1);
    if (!h) return;
    memcpy(GlobalLock(h), text, len + 1);
    GlobalUnlock(h);
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, h);
        CloseClipboard();
    } else {
        GlobalFree(h);
    }
#else
    // Cross-platform via the Qt clipboard (null-safe when no QGuiApplication).
    QClipboard *cb = QGuiApplication::clipboard();
    if (cb) cb->setText(QString::fromUtf8(text));
#endif
}

static void open_url(const QUrl &url) {
    QString urlStr = url.toString();
#ifdef _WIN32
    QProcess::startDetached("cmd.exe", QStringList() << "/c" << "start" << "" << urlStr);
#elif defined(__APPLE__)
    QProcess::startDetached("open", QStringList() << urlStr);
#else
    QProcess::startDetached("xdg-open", QStringList() << urlStr);
#endif
}

static void format_uuid(const char *raw, char *out, size_t out_size) {
    size_t len = strlen(raw);
    if (len >= 32 && out_size >= 37) {
        memcpy(out, raw, 8);
        out[8] = '-';
        memcpy(out + 9, raw + 8, 4);
        out[13] = '-';
        memcpy(out + 14, raw + 12, 4);
        out[18] = '-';
        memcpy(out + 19, raw + 16, 4);
        out[23] = '-';
        memcpy(out + 24, raw + 20, 12);
        out[36] = '\0';
    } else {
        strncpy(out, raw, out_size - 1);
    }
}

static int http_ok(McHttpResponse *resp) {
    return resp && resp->data && resp->status_code >= 200 && resp->status_code < 300;
}

static McHttpResponse *post_form(const char *url, const char *form_body) {
    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 30000);
    return mc_http_post(&client, url, "application/x-www-form-urlencoded",
                        (const unsigned char *)form_body, strlen(form_body));
}

static int do_device_code_flow(char *msa_token, size_t msa_size,
                                char *refresh_token, size_t refresh_size) {
    // Step 1: Request device code (using legacy login.live.com endpoint with Title ID)
    char body[1024];
    snprintf(body, sizeof(body),
        "client_id=%s&scope=XboxLive.signin%%20offline_access&response_type=device_code",
        MSA_CLIENT_ID);

    McHttpResponse *resp = post_form(
        "https://login.live.com/oauth20_connect.srf", body);
    mc_debug("[MSA] Device code POST body: %s", body);

    if (!resp) {
        mc_error("[MSA] Device code request failed (no response)");
        return 0;
    }
    mc_debug("[MSA] Device code response: status=%ld, success=%d, error=[%s]",
        resp->status_code, resp->success, resp->error);
    if (resp->data) mc_debug("[MSA] Device code body: %.300s", resp->data);
    if (!http_ok(resp)) {
        mc_error("[MSA] Device code request failed: status=%ld, error=%s, body=%s",
            resp->status_code, resp->error[0] ? resp->error : "none",
            resp->data ? resp->data : "(no body)");
        mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError json_err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &json_err);
    mc_http_response_free(resp);
    if (json_err.error != QJsonParseError::NoError) { mc_error("[MSA] Failed to parse device code response"); return 0; }
    QJsonObject j = doc.object();

    QJsonValue user_code_val = j.value("user_code");
    QJsonValue device_code_val = j.value("device_code");
    QJsonValue verif_uri_val = j.value("verification_uri");
    int interval = (int)j.value("interval").toDouble(5);

    if (!user_code_val.isString() || !device_code_val.isString() || !verif_uri_val.isString()) {
        QJsonValue err_val = j.value("error");
        mc_error("[MSA] Device code error: %s", err_val.isString() ? err_val.toString().toUtf8().constData() : "incomplete response");
        return 0;
    }

    QString saved_device_code = device_code_val.toString();
    QString saved_user_code = user_code_val.toString();
    QString saved_verif_uri = verif_uri_val.toString();

    // Step 2: Display to user and open browser
    mc_info("");
    mc_info("========================================");
    mc_info(" Microsoft Login");
    mc_info("========================================");
    mc_info(" 1. Open: %s", saved_verif_uri.toUtf8().constData());
    mc_info(" 2. Enter code: %s (copied to clipboard)", saved_user_code.toUtf8().constData());
    mc_info("========================================");
    mc_info("");

    copy_to_clipboard(saved_user_code.toUtf8().constData());
    QUrl verifUrl(saved_verif_uri);
    open_url(verifUrl);

    // Step 3: Poll for token
    mc_info("[MSA] Waiting for authentication...");

    QElapsedTimer timer;
    timer.start();
    int max_wait_sec = 300; // 5 minutes

    while (timer.elapsed() < max_wait_sec * 1000) {
        mc_http_sleep(interval * 1000);

        char poll_body[2048];
        char enc_dev_code[2048];
        url_encode(saved_device_code.toUtf8().constData(), enc_dev_code, sizeof(enc_dev_code));
        snprintf(poll_body, sizeof(poll_body),
            "grant_type=device_code&client_id=%s&device_code=%s",
            MSA_CLIENT_ID, enc_dev_code);

        McHttpResponse *poll_resp = post_form(
            "https://login.live.com/oauth20_token.srf", poll_body);

        if (!poll_resp || !poll_resp->data) {
            if (poll_resp) mc_http_response_free(poll_resp);
            mc_info("[MSA] Poll error, retrying...");
            continue;
        }

        QJsonParseError poll_err;
        QJsonDocument poll_doc = QJsonDocument::fromJson(QByteArray(poll_resp->data), &poll_err);
        mc_http_response_free(poll_resp);
        if (poll_err.error != QJsonParseError::NoError) continue;
        QJsonObject pj = poll_doc.object();

        QJsonValue err_val = pj.value("error");
        if (err_val.isString()) {
            QString err = err_val.toString();
            if (err == "authorization_pending") {
                continue;
            }
            if (err == "slow_down") {
                interval += 5;
                continue;
            }
            mc_error("[MSA] Auth error: %s", err.toUtf8().constData());
            return 0;
        }

        QJsonValue at_val = pj.value("access_token");
        QJsonValue rt_val = pj.value("refresh_token");
        if (!at_val.isString()) {
            mc_error("[MSA] No access_token in device code response");
            return 0;
        }

        QString at = at_val.toString();
        strncpy(msa_token, at.toUtf8().constData(), msa_size - 1);
        if (rt_val.isString()) {
            QString rt = rt_val.toString();
            strncpy(refresh_token, rt.toUtf8().constData(), refresh_size - 1);
        }
        return 1;
    }

    mc_error("[MSA] Device code auth timed out");
    return 0;
}

static char *xbl_authenticate(const char *msa_token, char *uhs, size_t uhs_size) {
    mc_debug("[MSA] XBL msa_token starts with: %.100s", msa_token ? msa_token : "(null)");
    char body[4096];
    int n = snprintf(body, sizeof(body),
        "{"
        "\"Properties\":{"
        "\"AuthMethod\":\"RPS\","
        "\"SiteName\":\"user.auth.xboxlive.com\","
        "\"RpsTicket\":\"d=%s\""
        "},"
        "\"RelyingParty\":\"http://auth.xboxlive.com\","
        "\"TokenType\":\"JWT\""
        "}", msa_token ? msa_token : "");
    mc_debug("[MSA] XBL request body length: %d", n);

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);
    McHttpResponse *resp = mc_http_post_json(&client,
        "https://user.auth.xboxlive.com/user/authenticate", body);
    if (!resp) {
        mc_error("[MSA] XBL authentication failed (no response)");
        return nullptr;
    }
    mc_debug("[MSA] XBL response: status=%ld, success=%d, error=[%s], data_len=%zu",
        resp->status_code, resp->success, resp->error, resp->data_len);
    if (resp->data) {
        mc_debug("[MSA] XBL body: %.200s", resp->data);
    }
    if (!http_ok(resp)) {
        mc_error("[MSA] XBL authentication failed: status=%ld, error=%s, body=%s",
            resp->status_code, resp->error[0] ? resp->error : "empty error",
            resp->data ? resp->data : "(no body)");
        mc_http_response_free(resp);
        return nullptr;
    }

    QJsonParseError xbl_err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &xbl_err);
    mc_http_response_free(resp);
    if (xbl_err.error != QJsonParseError::NoError) { mc_error("[MSA] Failed to parse XBL response"); return nullptr; }
    QJsonObject j = doc.object();

    QJsonValue token_val = j.value("Token");
    if (!token_val.isString()) { mc_error("[MSA] No XBL token"); return nullptr; }
    QString token = token_val.toString();

    QJsonValue claims = j.value("DisplayClaims");
    if (claims.isObject()) {
        QJsonValue xui = claims.toObject().value("xui");
        if (xui.isArray()) {
            QJsonArray xui_arr = xui.toArray();
            if (!xui_arr.isEmpty()) {
                QJsonValue first = xui_arr.at(0);
                if (first.isObject()) {
                    QJsonValue uhs_val = first.toObject().value("uhs");
                    if (uhs_val.isString()) {
                        strncpy(uhs, uhs_val.toString().toUtf8().constData(), uhs_size - 1);
                    }
                }
            }
        }
    }

    char *result = strdup(token.toUtf8().constData());
    return result;
}

static char *xsts_authenticate(const char *xbl_token) {
    char body[4096];
    snprintf(body, sizeof(body),
        "{"
        "\"Properties\":{"
        "\"SandboxId\":\"RETAIL\","
        "\"UserTokens\":[\"%s\"]"
        "},"
        "\"RelyingParty\":\"rp://api.minecraftservices.com/\","
        "\"TokenType\":\"JWT\""
        "}", xbl_token);

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);
    McHttpResponse *resp = mc_http_post_json(&client,
        "https://xsts.auth.xboxlive.com/xsts/authorize", body);
    if (!http_ok(resp)) {
        mc_error("[MSA] XSTS authentication failed: status=%ld, body=%s",
            resp ? resp->status_code : 0,
            (resp && resp->data) ? resp->data : "(no response)");
        if (resp) mc_http_response_free(resp);
        return nullptr;
    }

    QJsonParseError xsts_err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &xsts_err);
    mc_http_response_free(resp);
    if (xsts_err.error != QJsonParseError::NoError) { mc_error("[MSA] Failed to parse XSTS response"); return nullptr; }
    QJsonObject j = doc.object();

    QJsonValue xerr_val = j.value("XErr");
    if (xerr_val.isString()) {
        mc_error("[MSA] XSTS error XErr=%s", xerr_val.toString().toUtf8().constData());
        return nullptr;
    }

    QJsonValue token_val = j.value("Token");
    if (!token_val.isString()) { mc_error("[MSA] No XSTS token"); return nullptr; }

    char *result = strdup(token_val.toString().toUtf8().constData());
    return result;
}

static char *mc_login(const char *uhs, const char *xsts_token) {
    char identity[4096];
    snprintf(identity, sizeof(identity), "XBL3.0 x=%s;%s", uhs, xsts_token);

    char body[4096];
    snprintf(body, sizeof(body),
        "{\"identityToken\":\"%s\"}", identity);

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);
    McHttpResponse *resp = mc_http_post_json(&client,
        "https://api.minecraftservices.com/authentication/login_with_xbox", body);
    if (!http_ok(resp)) {
        mc_error("[MSA] Minecraft login failed: status=%ld, body=%s",
            resp ? resp->status_code : 0,
            (resp && resp->data) ? resp->data : "(no response)");
        if (resp) mc_http_response_free(resp);
        return nullptr;
    }

    mc_debug("[MSA] MC login response: %.800s", resp->data);

    QJsonParseError mc_err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &mc_err);
    mc_http_response_free(resp);
    if (mc_err.error != QJsonParseError::NoError) { mc_error("[MSA] Failed to parse Minecraft login response"); return nullptr; }
    QJsonObject j = doc.object();

    QJsonValue tt_val = j.value("token_type");
    mc_debug("[MSA] MC login token_type: %s", tt_val.isString() ? tt_val.toString().toUtf8().constData() : "(null)");

    QJsonValue token_val = j.value("access_token");
    if (!token_val.isString()) { mc_error("[MSA] No Minecraft access_token"); return nullptr; }

    char *result = strdup(token_val.toString().toUtf8().constData());
    return result;
}

static void jwt_decode_profile(const char *jwt, char *uuid, size_t uuid_size,
                                char *name, size_t name_size) {
    uuid[0] = '\0';
    name[0] = '\0';
    if (!jwt) return;

    // Find the second dot (between payload and signature)
    const char *first_dot = strchr(jwt, '.');
    if (!first_dot) return;
    const char *second_dot = strchr(first_dot + 1, '.');
    if (!second_dot) return;

    // Extract payload (between first and second dot)
    size_t payload_len = second_dot - first_dot - 1;
    if (payload_len == 0) return;

    // Copy and convert base64url to base64
    char *b64 = (char *)malloc(payload_len + 4);
    if (!b64) return;
    for (size_t i = 0; i < payload_len; i++) {
        char c = first_dot[1 + i];
        b64[i] = (c == '-') ? '+' : (c == '_') ? '/' : c;
    }
    // Add padding
    size_t padded = payload_len;
    while (padded % 4) b64[padded++] = '=';
    b64[padded] = '\0';

    QByteArray decoded = QByteArray::fromBase64(QByteArray(b64, padded));
    free(b64);

    if (decoded.isEmpty()) return;

    QJsonParseError jwt_err;
    QJsonDocument doc = QJsonDocument::fromJson(decoded, &jwt_err);
    if (jwt_err.error != QJsonParseError::NoError) return;
    QJsonObject j = doc.object();

    // Extract UUID from profiles.mc
    QJsonValue profiles = j.value("profiles");
    if (profiles.isObject()) {
        QJsonValue mc_uuid = profiles.toObject().value("mc");
        if (mc_uuid.isString()) {
            // UUID from JWT is already formatted with hyphens
            strncpy(uuid, mc_uuid.toString().toUtf8().constData(), uuid_size - 1);
        }
    }

    // Extract username from pfd[0].name
    if (!name[0]) {
        QJsonValue pfd = j.value("pfd");
        if (pfd.isArray()) {
            QJsonArray pfd_arr = pfd.toArray();
            if (!pfd_arr.isEmpty()) {
                QJsonValue first = pfd_arr.at(0);
                if (first.isObject()) {
                    QJsonValue n = first.toObject().value("name");
                    if (n.isString()) {
                        strncpy(name, n.toString().toUtf8().constData(), name_size - 1);
                    }
                }
            }
        }
    }
}

static int get_mc_profile(const char *mc_token, char *uuid, size_t uuid_size,
                          char *name, size_t name_size) {
    char auth_header[MC_AUTH_TOKEN_SIZE + 128];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", mc_token);
    mc_debug("[MSA] Profile header: %.150s...", auth_header);
    const char *headers[] = { auth_header };

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);
    McHttpResponse *resp = mc_http_get_with_headers(&client,
        "https://api.minecraftservices.com/minecraft/profile", headers, 1);
    mc_debug("[MSA] Profile resp: status=%ld, success=%d, error=[%s]",
        resp ? resp->status_code : 0, resp ? resp->success : 0,
        resp ? resp->error : "null");
    if (!http_ok(resp)) {
        mc_error("[MSA] Failed to get Minecraft profile: status=%ld, body=%s",
            resp ? resp->status_code : 0,
            (resp && resp->data) ? resp->data : "(no response)");
        if (resp && resp->data) {
            QJsonParseError pj_err;
            QJsonDocument pj_doc = QJsonDocument::fromJson(QByteArray(resp->data), &pj_err);
            if (pj_err.error == QJsonParseError::NoError) {
                QJsonObject pj = pj_doc.object();
                QJsonValue err_v = pj.value("error");
                QJsonValue errmsg_v = pj.value("errorMessage");
                mc_debug("[MSA] Profile error: %s, errorMessage: %s",
                    err_v.isString() ? err_v.toString().toUtf8().constData() : "(none)",
                    errmsg_v.isString() ? errmsg_v.toString().toUtf8().constData() : "(none)");
            }
        }
        if (resp) mc_http_response_free(resp);
        // Fallback: decode profile from JWT
        mc_info("[MSA] Falling back to JWT profile decode...");
        jwt_decode_profile(mc_token, uuid, uuid_size, name, name_size);
        if (uuid[0] && name[0]) {
            mc_info("[MSA] Profile from JWT: %s (%s)", name, uuid);
            return 1;
        }
        return 0;
    }

    QJsonParseError prof_err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &prof_err);
    mc_http_response_free(resp);
    if (prof_err.error != QJsonParseError::NoError) { mc_error("[MSA] Failed to parse profile response"); return 0; }
    QJsonObject j = doc.object();

    QJsonValue err_v = j.value("error");
    if (err_v.isString()) {
        mc_error("[MSA] Profile error: %s", err_v.toString().toUtf8().constData());
        return 0;
    }

    QJsonValue raw_id_v = j.value("id");
    QJsonValue nm_v = j.value("name");
    if (!raw_id_v.isString() || !nm_v.isString()) {
        mc_error("[MSA] Incomplete profile data");
        return 0;
    }

    QString raw_id = raw_id_v.toString();
    QString nm = nm_v.toString();

    // Format UUID with hyphens
    format_uuid(raw_id.toUtf8().constData(), uuid, uuid_size);
    strncpy(name, nm.toUtf8().constData(), name_size - 1);
    return 1;
}

int mc_auth_msa_login(McAuthSession *session) {
    if (!session) return 0;

    McAuthSession tmp;
    mc_auth_init(&tmp);

    char msa_token[2048] = "";
    char refresh_token[2048] = "";

    if (!do_device_code_flow(msa_token, sizeof(msa_token),
                            refresh_token, sizeof(refresh_token)))
        return 0;

    if (refresh_token[0])
        strncpy(tmp.msa_refresh_token, refresh_token,
                sizeof(tmp.msa_refresh_token) - 1);

    char uhs[256] = "";
    char *xbl_token = xbl_authenticate(msa_token, uhs, sizeof(uhs));
    if (!xbl_token) return 0;
    mc_info("[MSA] XBL authenticated");

    char *xsts_token = xsts_authenticate(xbl_token);
    free(xbl_token);
    if (!xsts_token) return 0;
    mc_info("[MSA] XSTS authorized");

    char *mc_token = mc_login(uhs, xsts_token);
    free(xsts_token);
    if (!mc_token) return 0;
    mc_info("[MSA] Minecraft authenticated");

    strncpy(tmp.access_token, mc_token, sizeof(tmp.access_token) - 1);
    strncpy(tmp.user_type, "msa", sizeof(tmp.user_type) - 1);

    if (!get_mc_profile(mc_token, tmp.uuid, sizeof(tmp.uuid),
                        tmp.name, sizeof(tmp.name))) {
        mc_error("[MSA] Failed to get Minecraft profile");
        free(mc_token);
        return 0;
    }
    free(mc_token);

    mc_info("[MSA] Logged in as: %s (%s)", tmp.name, tmp.uuid);
    tmp.is_authenticated = 1;
    *session = tmp;
    return 1;
}

int mc_auth_msa_refresh(McAuthSession *session) {
    if (!session || !session->msa_refresh_token[0]) {
        if (session) mc_error("[MSA] No refresh token available");
        return 0;
    }

    char body[2048];
    snprintf(body, sizeof(body),
        "client_id=%s&refresh_token=%s&grant_type=refresh_token"
        "&scope=XboxLive.signin+offline_access",
        MSA_CLIENT_ID, session->msa_refresh_token);

    McHttpResponse *resp = post_form("https://login.live.com/oauth20_token.srf", body);
    if (!http_ok(resp)) {
        mc_error("[MSA] Refresh failed: status=%ld, body=%s",
            resp ? resp->status_code : 0,
            (resp && resp->data) ? resp->data : "(no response)");
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError refresh_err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &refresh_err);
    mc_http_response_free(resp);
    if (refresh_err.error != QJsonParseError::NoError) { mc_error("[MSA] Failed to parse refresh response"); return 0; }
    QJsonObject j = doc.object();

    QJsonValue err_v = j.value("error");
    if (err_v.isString()) {
        mc_error("[MSA] Refresh error: %s", err_v.toString().toUtf8().constData());
        return 0;
    }

    QJsonValue msa_token_v = j.value("access_token");
    QJsonValue new_refresh_v = j.value("refresh_token");
    if (!msa_token_v.isString()) { mc_error("[MSA] No access_token in refresh"); return 0; }

    QString msa_token = msa_token_v.toString();
    if (new_refresh_v.isString())
        strncpy(session->msa_refresh_token, new_refresh_v.toString().toUtf8().constData(),
                sizeof(session->msa_refresh_token) - 1);

    char uhs[256] = "";
    char *xbl_token = xbl_authenticate(msa_token.toUtf8().constData(), uhs, sizeof(uhs));
    if (!xbl_token) return 0;

    char *xsts_token = xsts_authenticate(xbl_token);
    free(xbl_token);
    if (!xsts_token) return 0;

    char *mc_token = mc_login(uhs, xsts_token);
    free(xsts_token);
    if (!mc_token) return 0;

    strncpy(session->access_token, mc_token, sizeof(session->access_token) - 1);

    char uuid[MC_AUTH_UUID_SIZE] = "";
    char name[MC_AUTH_NAME_SIZE] = "";
    int profile_ok = get_mc_profile(mc_token, uuid, sizeof(uuid), name, sizeof(name));
    free(mc_token);

    if (profile_ok) {
        strncpy(session->uuid, uuid, sizeof(session->uuid) - 1);
        strncpy(session->name, name, sizeof(session->name) - 1);
    }

    session->is_authenticated = 1;
    mc_info("[MSA] Session refreshed for %s", session->name);
    return 1;
}
