/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_AUTH_H
#define MC_AUTH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_AUTH_TOKEN_SIZE 4096
#define MC_AUTH_UUID_SIZE  40
#define MC_AUTH_NAME_SIZE  64

typedef struct {
    char access_token[MC_AUTH_TOKEN_SIZE];
    char client_token[MC_AUTH_TOKEN_SIZE];
    char uuid[MC_AUTH_UUID_SIZE];
    char name[MC_AUTH_NAME_SIZE];
    char user_type[16];
    int  is_authenticated;
    char error[512];
    char server_url[512];
    char msa_refresh_token[MC_AUTH_TOKEN_SIZE]; // for MSA offline_access
} McAuthSession;

void mc_auth_init(McAuthSession *session);
void mc_auth_set_server(McAuthSession *session, const char *url);

int mc_auth_authenticate(McAuthSession *session, const char *username, const char *password);
int mc_auth_refresh(McAuthSession *session);
int mc_auth_validate(McAuthSession *session);
int mc_auth_signout(const char *username, const char *password);
int mc_auth_invalidate(McAuthSession *session);

int mc_auth_save(const McAuthSession *session, const char *path);
int mc_auth_load(McAuthSession *session, const char *path);
void mc_auth_generate_client_token(McAuthSession *session);

#ifdef __cplusplus
}
#endif

#endif
