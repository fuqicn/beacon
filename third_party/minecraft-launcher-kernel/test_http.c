/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mclauncher.h"
#include <stdio.h>
int main() {
    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 10000);
    printf("Fetching...\n");
    McHttpResponse *resp = mc_http_get(&client, "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
    if (resp) {
        printf("Success: %d\n", resp->success);
        printf("Status: %ld\n", resp->status_code);
        printf("Error: %s\n", resp->error);
        if (resp->data) printf("Data len: %zu, first 100: %.100s\n", resp->data_len, resp->data);
        mc_http_response_free(resp);
    } else {
        printf("Response is NULL\n");
    }
    return 0;
}
