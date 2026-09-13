/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_HTTP_H
#define MC_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int success;
    char *data;
    size_t data_len;
    long status_code;
    char error[512];
} McHttpResponse;

typedef struct {
    char proxy_host[256];
    int proxy_port;
    int use_proxy;
    int timeout_ms;
    char user_agent[256];
} HttpClient;

void mc_http_init(HttpClient *client);
void mc_http_set_proxy(HttpClient *client, const char *host, int port);
void mc_http_set_timeout(HttpClient *client, int timeout_ms);
void mc_http_set_user_agent(HttpClient *client, const char *user_agent);

// Global user-agent override: when set, new QNetworkAccessManager
// instances created by the kernel use this UA. Intended for launchers that
// link libmcbase and want a consistent app UA (or none: pass "" to suppress).
void mc_http_set_global_user_agent(const char *user_agent);

// The user agent applied to download requests (global override when set,
// else the built-in browser UA). Used by the Qt download pool.
const char *mc_http_default_user_agent(void);

McHttpResponse *mc_http_get(HttpClient *client, const char *url);
McHttpResponse *mc_http_get_with_headers(HttpClient *client, const char *url, const char **headers, int header_count);
McHttpResponse *mc_http_post(HttpClient *client, const char *url, const char *content_type, const unsigned char *body, size_t body_len);
McHttpResponse *mc_http_post_json(HttpClient *client, const char *url, const char *json_body);
McHttpResponse *mc_http_head(HttpClient *client, const char *url);

void mc_http_response_free(McHttpResponse *resp);

// Free the calling thread's thread-local QNetworkAccessManager. Call this from
// a bare std::thread after its mc_http_* work so the thread's TLS does not
// keep a live NAM (and its 100ms Qt timer) running after the thread exits.
void mc_http_release_thread_resources(void);

void mc_http_sleep(int ms);

#ifdef __cplusplus
}
#endif

#endif
