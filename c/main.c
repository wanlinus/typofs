/*
 * typofs —— Typora 的 S3 兼容图床客户端（C 版, 仅 Windows / MSVC, 只用 Windows SDK）
 *
 * 逻辑：读 config.ini、按年建桶、逐张顺序上传、
 * 对象名=前10字-5位随机+扩展名、输出 "Upload Success:" + 每个 URL。
 *
 * 网络/加密完全用 Windows 原生 API，无任何第三方依赖：
 *   * HTTP/HTTPS 上传        -> WinHTTP (winhttp.lib)
 *   * SHA256 / HMAC 签名      -> CNG/BCrypt (bcrypt.lib)
 * 因此只需 Windows SDK（装了 MSVC 就有），不需要 vcpkg / curl / openssl。
 *
 * 构建（CMake + MSVC，见 CMakeLists.txt），或直接：
 *   cl /std:c11 /O2 main.c /Fe:typofs.exe winhttp.lib bcrypt.lib
 * 产物：typofs.exe
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

#define access _access
#define F_OK 0
#define REGION "us-east-1"
#define UNSIGNED_PAYLOAD "UNSIGNED-PAYLOAD"
#define RESP_MAX 16384

typedef struct { char endpoint[256], username[128], password[128], buckets[128], pre_url[256]; } Config;

/* ---------- config.ini（key=value，# 注释） ---------- */
static int load_config(Config* c) {
    FILE* fp; char path[1024];
    if (access("config.ini", F_OK) != 0) {
        char exe[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
        char* s = n ? strrchr(exe, '\\') : NULL;
        if (s) { *s = 0; snprintf(path, sizeof(path), "%s\\config.ini", exe); fp = fopen(path, "r"); if (fp) goto got; }
    }
    fp = fopen("config.ini", "r");
    if (!fp) { fprintf(stderr, "请配置 config.ini (endpoint/username/password/buckets/preUrl)\n"); return 1; }
got: {
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char* p = line; if (*p == '#') continue;
        while (*p == ' ' || *p == '\t') p++;
        char* eq = p; while (*eq && *eq != '=' && *eq != '\n' && *eq != '\r') eq++;
        if (*eq != '=') continue;
        char* ke = eq; while (ke > p && (ke[-1] == ' ' || ke[-1] == '\t')) ke--; *ke = 0;
        char* v = eq + 1; while (*v == ' ' || *v == '\t') v++;
        size_t vl = strcspn(v, "\r\n"); v[vl] = 0;
        char* ve = v + strlen(v); while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = 0;
        if      (!strcmp(p, "endpoint")) snprintf(c->endpoint, sizeof(c->endpoint), "%s", v);
        else if (!strcmp(p, "username")) snprintf(c->username, sizeof(c->username), "%s", v);
        else if (!strcmp(p, "password")) snprintf(c->password, sizeof(c->password), "%s", v);
        else if (!strcmp(p, "buckets"))  snprintf(c->buckets,  sizeof(c->buckets),  "%s", v);
        else if (!strcmp(p, "preUrl"))   snprintf(c->pre_url,  sizeof(c->pre_url),  "%s", v);
    }
    fclose(fp);
    if (!c->endpoint[0]) { fprintf(stderr, "endpoint 不能为空\n"); return 1; }
    return 0;
}
}

/* endpoint 拆成 host 与 port（无端口时用 def_port） */
static void split_host_port(const char* ep, char* host, size_t hs, int* port, int def_port) {
    const char* c = strrchr(ep, ':');
    if (c && atoi(c + 1) > 0) {
        size_t n = (size_t)(c - ep); if (n >= hs) n = hs - 1;
        memcpy(host, ep, n); host[n] = 0;
        *port = atoi(c + 1);
    } else { snprintf(host, hs, "%s", ep); *port = def_port; }
}

/* ---------- CNG/BCrypt：SHA256 与 HMAC-SHA256 ---------- */
static void sha256(const unsigned char* data, size_t len, unsigned char out[32]) {
    BCRYPT_ALG_HANDLE a; BCRYPT_HASH_HANDLE h;
    BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    BCryptCreateHash(a, &h, NULL, 0, NULL, 0, 0);
    BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0);
    BCryptFinishHash(h, out, 32, 0);
    BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(a, 0);
}
static void hmac(const unsigned char* k, size_t kl, const unsigned char* d, size_t dl, unsigned char out[32]) {
    BCRYPT_ALG_HANDLE a; BCRYPT_HASH_HANDLE h;
    BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    BCryptCreateHash(a, &h, NULL, 0, (PUCHAR)k, (ULONG)kl, 0);
    BCryptHashData(h, (PUCHAR)d, (ULONG)dl, 0);
    BCryptFinishHash(h, out, 32, 0);
    BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(a, 0);
}

static void hex(const unsigned char* d, size_t n, char* o) { for (size_t i = 0; i < n; i++) sprintf(o + i * 2, "%02x", d[i]); o[n * 2] = 0; }
static void sha256h(const char* d, size_t n, char* o) { unsigned char dig[32]; sha256((const unsigned char*)d, n, dig); hex(dig, 32, o); }

/* ---------- SigV4 签名 ---------- */
static void derive(const char* sec, const char* date, unsigned char k[32]) {
    char init[128]; int n = snprintf(init, sizeof(init), "AWS4%s", sec);
    unsigned char a[32], b[32], c[32];
    hmac((const unsigned char*)init, (size_t)n, (const unsigned char*)date, strlen(date), a);
    hmac(a, 32, (const unsigned char*)REGION, strlen(REGION), b);
    hmac(b, 32, (const unsigned char*)"s3", 2, c);
    hmac(c, 32, (const unsigned char*)"aws4_request", 12, k);
}
static void sign(const unsigned char k[32], const char* host, const char* uri,
                 const char* amz, const char* date, const char* ak, char* out, size_t os) {
    char ch[1024], cr[4096], crh[65], sc[128], st[512];
    snprintf(ch, sizeof(ch), "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n", host, UNSIGNED_PAYLOAD, amz);
    snprintf(cr, sizeof(cr), "PUT\n%s\n\n%s\nhost;x-amz-content-sha256;x-amz-date\n%s", uri, ch, UNSIGNED_PAYLOAD);
    sha256h(cr, strlen(cr), crh);
    snprintf(sc, sizeof(sc), "%s/%s/s3/aws4_request", date, REGION);
    snprintf(st, sizeof(st), "AWS4-HMAC-SHA256\n%s\n%s\n%s", amz, sc, crh);
    unsigned char m[32]; char sg[65];
    hmac(k, 32, (const unsigned char*)st, strlen(st), m); hex(m, 32, sg);
    snprintf(out, os, "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=%s", ak, sc, sg);
}

/* ---------- 对象名：前10字-5位随机+扩展名 ---------- */
static void build_name(const char* f, char* out, size_t os) {
    const char* b = strrchr(f, '/'); const char* w = strrchr(f, '\\');
    if (w && (!b || w > b)) b = w;
    b = b ? b + 1 : f;
    const char* d = strrchr(b, '.'); size_t sl = d ? (size_t)(d - b) : strlen(b); if (sl > 10) sl = 10;
    char e[16]; e[0] = 0; if (d) snprintf(e, sizeof(e), "%s", d);
    static const char L[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char r[6]; for (int i = 0; i < 5; i++) r[i] = L[(unsigned)rand() % 52]; r[5] = 0;
    snprintf(out, os, "%.*s-%s%s", (int)sl, b, r, e);
}

/* ---------- WinHTTP：单次 PUT ---------- */
static void widen(const char* a, wchar_t* w, size_t osz) {
    size_t n = strlen(a); if (n >= osz) n = osz - 1;
    for (size_t i = 0; i < n; i++) w[i] = (unsigned char)a[i];
    w[n] = 0;
}
static int http_put(const char* host, int port, int secure, const char* uri, const char* hdrs,
                    const unsigned char* body, long len, long* status, char* resp, size_t resp_sz) {
    wchar_t whost[256], wuri[1024], whdrs[4096];
    widen(host, whost, 256); widen(uri, wuri, 1024); widen(hdrs, whdrs, 4096);
    HINTERNET hS = WinHttpOpen(L"typofs/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return 1;
    HINTERNET hC = WinHttpConnect(hS, whost, (INTERNET_PORT)port, 0);
    if (!hC) { WinHttpCloseHandle(hS); return 1; }
    HINTERNET hR = WinHttpOpenRequest(hC, L"PUT", wuri, NULL, NULL, NULL, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return 1; }
    WinHttpSetTimeouts(hR, 10000, 10000, 60000, 120000);
    if (resp && resp_sz) resp[0] = 0;
    BOOL ok = WinHttpSendRequest(hR, whdrs, (DWORD)-1, (LPVOID)body, (DWORD)len, (DWORD)len, 0);
    if (ok) ok = WinHttpReceiveResponse(hR, NULL);
    *status = 0;
    if (ok) {
        DWORD sc = 0, sl = sizeof(sc);
        WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sl, WINHTTP_NO_HEADER_INDEX);
        *status = (long)sc;
        if (resp && resp_sz) {  /* 读响应体，错误时含 S3 错误 XML */
            DWORD avail = 0, got = 0; size_t used = 0;
            while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
                if (used + avail >= resp_sz) avail = (DWORD)(resp_sz - used - 1);
                if (!avail) break;
                if (!WinHttpReadData(hR, resp + used, avail, &got)) break;
                if (!got) break;
                used += got;
            }
            resp[used] = 0;
        }
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(65001);  // 让中文错误信息在 Windows 控制台正常显示
    if (argc < 2) { fprintf(stderr, "请传入图片路径\n"); return 1; }

    Config cfg; memset(&cfg, 0, sizeof(cfg));
    if (load_config(&cfg)) return 1;

    /* 识别 http/https（默认 http，适配 MinIO/RustFS） */
    const char* ep = cfg.endpoint;
    int secure = 0;
    if (strncmp(ep, "https://", 8) == 0) { secure = 1; ep += 8; }
    else if (strncmp(ep, "http://", 7) == 0) { secure = 0; ep += 7; }
    char host[256]; int port;
    split_host_port(ep, host, sizeof(host), &port, secure ? 443 : 80);
    char signed_host[280];
    int default_port = secure ? 443 : 80;
    snprintf(signed_host, sizeof(signed_host), port == default_port ? "%s" : "%s:%d", host, port);

    time_t t = time(NULL); struct tm tm; gmtime_s(&tm, &t);
    char year[8], bucket[256]; strftime(year, sizeof(year), "%Y", &tm);
    snprintf(bucket, sizeof(bucket), "%s-%s", cfg.buckets, year);
    srand((unsigned)time(NULL));

    char amz[17], date[9]; strftime(amz, sizeof(amz), "%Y%m%dT%H%M%SZ", &tm); strftime(date, sizeof(date), "%Y%m%d", &tm);
    unsigned char ks[32]; derive(cfg.password, date, ks);

    /* 确保桶存在：PUT /bucket（空 body） */
    {
        char uri[512], auth[2048], hdrs[1024], resp[2048];
        snprintf(uri, sizeof(uri), "/%s", bucket);
        sign(ks, signed_host, uri, amz, date, cfg.username, auth, sizeof(auth));
        snprintf(hdrs, sizeof(hdrs), "x-amz-content-sha256: %s\r\nx-amz-date: %s\r\nAuthorization: %s\r\n", UNSIGNED_PAYLOAD, amz, auth);
        long st;
        if (http_put(host, port, secure, uri, hdrs, NULL, 0, &st, resp, sizeof(resp)) != 0 || (st != 200 && st != 409)) {
            fprintf(stderr, "建桶失败 Http %ld: %s\n", st, resp); return 1;
        }
    }

    int failed = 0;
    printf("Upload Success:\n");
    /* 逐张顺序上传 */
    for (int i = 1; i < argc; i++) {
        if (access(argv[i], F_OK) != 0) { fprintf(stderr, "文件不存在: %s\n", argv[i]); failed++; continue; }
        FILE* fp = fopen(argv[i], "rb"); if (!fp) { failed++; continue; }
        fseek(fp, 0, SEEK_END); long sz = ftell(fp); rewind(fp);
        unsigned char* data = (unsigned char*)malloc((size_t)(sz > 0 ? sz : 1));
        if (!data) { fclose(fp); failed++; continue; }
        fread(data, 1, (size_t)sz, fp); fclose(fp);

        char obj[512], uri[512], auth[2048], hdrs[1024], resp[2048];
        build_name(argv[i], obj, sizeof(obj));
        snprintf(uri, sizeof(uri), "/%s/%s", bucket, obj);
        sign(ks, signed_host, uri, amz, date, cfg.username, auth, sizeof(auth));
        snprintf(hdrs, sizeof(hdrs), "x-amz-content-sha256: %s\r\nx-amz-date: %s\r\nAuthorization: %s\r\nContent-Type: application/octet-stream\r\n", UNSIGNED_PAYLOAD, amz, auth);

        long st;
        if (http_put(host, port, secure, uri, hdrs, data, sz, &st, resp, sizeof(resp)) == 0 && (st == 200 || st == 201)) {
            printf("%s/%s/%s\n", cfg.pre_url[0] ? cfg.pre_url : cfg.endpoint, bucket, obj);
        } else {
            fprintf(stderr, "上传失败(%ld): %s\n%s\n", st, obj, resp); failed++;
        }
        free(data);
    }
    return failed ? 1 : 0;
}
