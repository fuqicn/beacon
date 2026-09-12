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

McHttpResponse *mc_http_get(HttpClient *client, const char *url);
McHttpResponse *mc_http_get_with_headers(HttpClient *client, const char *url, const char **headers, int header_count);
McHttpResponse *mc_http_post(HttpClient *client, const char *url, const char *content_type, const unsigned char *body, size_t body_len);
McHttpResponse *mc_http_post_json(HttpClient *client, const char *url, const char *json_body);
McHttpResponse *mc_http_head(HttpClient *client, const char *url);

void mc_http_response_free(McHttpResponse *resp);

void mc_http_sleep(int ms);

#ifdef __cplusplus
}
#endif

#endif
