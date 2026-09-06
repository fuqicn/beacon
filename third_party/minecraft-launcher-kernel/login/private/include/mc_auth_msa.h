/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_AUTH_MSA_H
#define MC_AUTH_MSA_H

#include "mc_auth.h"

#ifdef __cplusplus
extern "C" {
#endif

// Microsoft OAuth device code flow + Xbox Live + Minecraft authentication.
// User opens a URL and enters a short code to authenticate.
int mc_auth_msa_login(McAuthSession *session);

// Refresh a Microsoft access token using the stored refresh_token.
// The refresh_token must be stored in session's client_token field.
// Returns 1 on success, 0 on failure.
int mc_auth_msa_refresh(McAuthSession *session);

#ifdef __cplusplus
}
#endif

#endif
