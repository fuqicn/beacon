/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
extern "C" {
#include "mc_http.h"
}

#include <QtCore/QCoreApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QUrl>
#include <QtCore/QByteArray>
#include <QtNetwork/QNetworkProxy>
#include "mc_download_qt.h"

static char g_qt_argv0[256] = "opencode-launcher";
static char *g_qt_argv[] = { g_qt_argv0, nullptr };
static int g_qt_argc = 1;

static QNetworkAccessManager *get_nam(void) {
    thread_local QNetworkAccessManager *nam = nullptr;
    if (!nam) nam = new QNetworkAccessManager();
    return nam;
}

static void ensure_qt(void) {
    if (!QCoreApplication::instance()) {
        static QCoreApplication *app = new QCoreApplication(g_qt_argc, g_qt_argv);
        (void)app;
    }
    get_nam();
}

extern "C" void mc_http_init(McHttpClient *client) {
    if (!client) return;
    memset(client, 0, sizeof(McHttpClient));
    client->timeout_ms = 30000;
    strcpy(client->user_agent, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
}

extern "C" void mc_http_set_proxy(McHttpClient *client, const char *host, int port) {
    if (!client || !host) return;
    strncpy(client->proxy_host, host, sizeof(client->proxy_host) - 1);
    client->proxy_port = port;
    client->use_proxy = 1;
}

extern "C" void mc_http_set_timeout(McHttpClient *client, int timeout_ms) {
    if (client) client->timeout_ms = timeout_ms > 0 ? timeout_ms : 30000;
}

static McHttpResponse *do_request(McHttpClient *client, const char *url,
    const char *method, const char *content_type, const unsigned char *body, size_t body_len,
    const char **extra_headers, int header_count)
{
    McHttpResponse *resp = (McHttpResponse *)calloc(1, sizeof(McHttpResponse));
    if (!resp) return NULL;

    ensure_qt();

    QUrl qurl(QString::fromUtf8(url));
    if (!qurl.isValid()) {
        snprintf(resp->error, sizeof(resp->error), "Invalid URL: %s", url);
        return resp;
    }

    QNetworkRequest req(qurl);
    req.setRawHeader("User-Agent", QByteArray::fromStdString(client->user_agent));
    req.setRawHeader("Accept", "*/*");
    req.setTransferTimeout(client->timeout_ms);

    if (client->use_proxy) {
        QNetworkProxy proxy(QNetworkProxy::HttpProxy,
            QString::fromUtf8(client->proxy_host), client->proxy_port);
        get_nam()->setProxy(proxy);
    } else {
        get_nam()->setProxy(QNetworkProxy::NoProxy);
    }

    for (int i = 0; i < header_count && extra_headers && extra_headers[i]; i++) {
        const char *h = extra_headers[i];
        const char *colon = strchr(h, ':');
        if (colon) {
            QByteArray name = QByteArray(h, colon - h).trimmed();
            QByteArray value = QByteArray(colon + 1).trimmed();
            req.setRawHeader(name, value);
        }
    }

    QNetworkReply *reply = nullptr;
    if (strcmp(method, "GET") == 0) {
        reply = get_nam()->get(req);
    } else if (strcmp(method, "HEAD") == 0) {
        reply = get_nam()->head(req);
    } else if (strcmp(method, "POST") == 0) {
        QByteArray postBody;
        if (body && body_len > 0)
            postBody = QByteArray((const char *)body, body_len);
        if (content_type)
            req.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromUtf8(content_type));
        reply = get_nam()->post(req, postBody);
    } else {
        snprintf(resp->error, sizeof(resp->error), "Unsupported method: %s", method);
        return resp;
    }

    QEventLoop loop;
    QTimer timer;
    QTimer cancel_timer;
    cancel_timer.setInterval(100);
    QObject::connect(&cancel_timer, &QTimer::timeout, [&loop]() {
        if (mc_qt_download_cancel()) loop.quit();
    });
    cancel_timer.start();
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(client->timeout_ms);
    loop.exec();

    if (mc_qt_download_cancel()) {
        reply->abort();
        snprintf(resp->error, sizeof(resp->error), "Request cancelled");
        delete reply;
        return resp;
    }
    cancel_timer.stop();

    if (!timer.isActive()) {
        reply->abort();
        snprintf(resp->error, sizeof(resp->error), "Request timed out after %d ms", client->timeout_ms);
        delete reply;
        return resp;
    }
    timer.stop();

    // ALWAYS read the response body, even on HTTP errors (4xx/5xx).
    // The server may include JSON error details in the body.
    QByteArray data = reply->readAll();
    resp->data_len = data.size();
    resp->data = (char *)malloc(resp->data_len + 1);
    if (resp->data) {
        memcpy(resp->data, data.constData(), resp->data_len);
        resp->data[resp->data_len] = '\0';
    }

    resp->status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() == QNetworkReply::NoError) {
        resp->success = 1;
    } else if (!resp->error[0]) {
        snprintf(resp->error, sizeof(resp->error), "%s",
            reply->errorString().toUtf8().constData());
    }

    // If status_code is valid (HTTP response received), treat as success
    // so callers can inspect the body even on 4xx/5xx
    if (resp->status_code != 0)
        resp->success = 1;

    delete reply;
    return resp;
}

extern "C" McHttpResponse *mc_http_get(McHttpClient *client, const char *url) {
    return do_request(client, url, "GET", NULL, NULL, 0, NULL, 0);
}

extern "C" McHttpResponse *mc_http_get_with_headers(McHttpClient *client, const char *url,
    const char **headers, int header_count)
{
    return do_request(client, url, "GET", NULL, NULL, 0, headers, header_count);
}

extern "C" McHttpResponse *mc_http_post(McHttpClient *client, const char *url,
    const char *content_type, const unsigned char *body, size_t body_len)
{
    return do_request(client, url, "POST", content_type, body, body_len, NULL, 0);
}

extern "C" McHttpResponse *mc_http_post_json(McHttpClient *client, const char *url, const char *json_body) {
    return do_request(client, url, "POST", "application/json",
        (const unsigned char *)json_body, json_body ? strlen(json_body) : 0, NULL, 0);
}

extern "C" McHttpResponse *mc_http_head(McHttpClient *client, const char *url) {
    return do_request(client, url, "HEAD", NULL, NULL, 0, NULL, 0);
}

extern "C" void mc_http_response_free(McHttpResponse *resp) {
    if (!resp) return;
    free(resp->data);
    free(resp);
}

extern "C" void mc_http_sleep(int ms) {
    QThread::msleep(ms);
}
