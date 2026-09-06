/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_str.h"
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdarg>

char *mc_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = static_cast<char *>(malloc(len));
    if (p) memcpy(p, s, len);
    return p;
}

char *mc_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t slen = strlen(s);
    if (slen > n) slen = n;
    char *p = static_cast<char *>(malloc(slen + 1));
    if (p) { memcpy(p, s, slen); p[slen] = '\0'; }
    return p;
}

char *mc_strtrim(char *s) {
    if (!s) return NULL;
    char *end;
    while (isspace(static_cast<unsigned char>(*s))) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace(static_cast<unsigned char>(*end))) end--;
    *(end + 1) = '\0';
    return s;
}

char *mc_strtrim_copy(const char *s) {
    if (!s) return NULL;
    while (isspace(static_cast<unsigned char>(*s))) s++;
    if (*s == '\0') return mc_strdup("");
    const char *end = s + strlen(s) - 1;
    while (end > s && isspace(static_cast<unsigned char>(*end))) end--;
    return mc_strndup(s, end - s + 1);
}

int mc_strsplit(const char *s, char delim, char ***out) {
    if (!s || !out) return 0;
    int count = 1;
    for (const char *p = s; *p; p++)
        if (*p == delim) count++;
    *out = static_cast<char **>(malloc(sizeof(char *) * (count + 1)));
    if (!*out) return 0;
    int idx = 0;
    const char *start = s;
    for (const char *p = s; ; p++) {
        if (*p == delim || *p == '\0') {
            (*out)[idx++] = mc_strndup(start, p - start);
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    (*out)[idx] = NULL;
    return idx;
}

void mc_strsplit_free(char **arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) free(arr[i]);
    free(arr);
}

char *mc_strjoin(const char **parts, int count, const char *sep) {
    if (!parts || count <= 0) return mc_strdup("");
    size_t total = 0;
    size_t sep_len = strlen(sep);
    for (int i = 0; i < count; i++)
        total += strlen(parts[i]);
    total += sep_len * (count - 1) + 1;
    char *result = static_cast<char *>(malloc(total));
    if (!result) return NULL;
    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0) { memcpy(result + pos, sep, sep_len); pos += sep_len; }
        size_t len = strlen(parts[i]);
        memcpy(result + pos, parts[i], len);
        pos += len;
    }
    result[pos] = '\0';
    return result;
}

char *mc_strreplace(const char *s, const char *from, const char *to) {
    if (!s || !from || !to) return NULL;
    size_t slen = strlen(s), flen = strlen(from), tlen = strlen(to);
    if (flen == 0) return mc_strdup(s);
    int count = 0;
    const char *p = s;
    while ((p = strstr(p, from)) != NULL) { count++; p += flen; }
    if (count == 0) return mc_strdup(s);
    size_t total = slen + count * (tlen - flen) + 1;
    char *result = static_cast<char *>(malloc(total));
    if (!result) return NULL;
    char *r = result;
    p = s;
    const char *last = s;
    while ((p = strstr(last, from)) != NULL) {
        size_t seg = p - last;
        memcpy(r, last, seg); r += seg;
        memcpy(r, to, tlen); r += tlen;
        last = p + flen;
    }
    size_t rem = strlen(last);
    memcpy(r, last, rem); r += rem;
    *r = '\0';
    return result;
}

int mc_strstartswith(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix)
        if (*s++ != *prefix++) return 0;
    return 1;
}

int mc_strendswith(const char *s, const char *suffix) {
    if (!s || !suffix) return 0;
    size_t slen = strlen(s), suflen = strlen(suffix);
    if (suflen > slen) return 0;
    return memcmp(s + slen - suflen, suffix, suflen) == 0;
}

int mc_strcontains(const char *s, const char *sub) {
    return s && sub && strstr(s, sub) != NULL;
}

char *mc_strlower(char *s) {
    if (!s) return NULL;
    for (char *p = s; *p; p++) *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
    return s;
}

char *mc_strlower_copy(const char *s) {
    if (!s) return NULL;
    char *copy = mc_strdup(s);
    return copy ? mc_strlower(copy) : NULL;
}

int mc_stricmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        int ca = tolower(static_cast<unsigned char>(*a));
        int cb = tolower(static_cast<unsigned char>(*b));
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower(static_cast<unsigned char>(*a)) - tolower(static_cast<unsigned char>(*b));
}

int mc_strnicmp(const char *a, const char *b, size_t n) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (size_t i = 0; i < n; i++) {
        if (a[i] == '\0' && b[i] == '\0') return 0;
        int ca = tolower(static_cast<unsigned char>(a[i]));
        int cb = tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca - cb;
    }
    return 0;
}

long mc_strtol(const char *s, int default_val) {
    if (!s || *s == '\0') return default_val;
    char *end;
    long val = strtol(s, &end, 10);
    if (*end != '\0') return default_val;
    return val;
}

int mc_strwildcard(const char *s, const char *pattern) {
    if (!s || !pattern) return 0;
    const char *p = pattern, *str = s, *star = NULL, *ss = s;
    while (*str) {
        if (*p == '?') { p++; str++; continue; }
        if (*p == '*') { star = p++; ss = str; continue; }
        if (tolower(static_cast<unsigned char>(*p)) == tolower(static_cast<unsigned char>(*str))) {
            p++; str++; continue;
        }
        if (star) { p = star + 1; str = ++ss; continue; }
        return 0;
    }
    while (*p == '*') p++;
    return *p == '\0';
}
