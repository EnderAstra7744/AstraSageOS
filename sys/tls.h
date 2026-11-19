/*
 * TLS 1.2 client (RFC 5246) for AstraSageOS: ECDHE (X25519 / P-256) key exchange, AES-128/256-GCM, RSA or ECDSA server
 * certificates checked against a built-in root list. Extended master secret (RFC 7627) is used when the server offers it.
 * Not implemented: TLS 1.3, session resumption, client certificates, renegotiation, compression, CHACHA20, SHA-1/MD5.
 * Plain C, no OS dependency: the OS gives it a transport (tls_io) and a random source (tls_random).
 */
#ifndef TLS_H
#define TLS_H
#include "crypto.h"
#include "x509.h"

typedef struct {
    int (*send)(void *ctx, const cu8 *data, size_t len);                 /* 0 = ok, -1 = failed */
    int (*recv)(void *ctx, cu8 *buf, size_t max, cu32 timeout_ms);       /* >0 = bytes, 0 = timeout, -1 = closed or reset */
    void *ctx;
} tls_io;

#define TLS_RBUF (5 + 16384 + 2048)
#define TLS_HSBUF 32768

typedef struct {
    tls_io io;
    int up, closed, verified;
    int hash_alg, keylen, ems;
    cr_gcm gin, gout;
    cu8 iv_in[4], iv_out[4];
    cu64 seq_in, seq_out;
    int enc_in, enc_out;
    cr_hash th256, th384;
    cu8 master[48], crandom[32], srandom[32];
    cu8 rbuf[TLS_RBUF];
    cu8 hs[TLS_HSBUF]; size_t hs_len;
    cu8 app[16384]; size_t app_len, app_off;
    cu8 leaf_spki[1200]; size_t leaf_spki_len;
} tls_t;

/* host: server name for SNI and certificate check. verify = 0 skips ALL authentication (encrypted but unauthenticated). */
int  tls_connect(tls_t *t, const tls_io *io, const char *host, int verify, cu32 now_epoch, const cu8 *roots, size_t roots_len);
int  tls_write(tls_t *t, const cu8 *data, size_t len);                   /* 0 = ok */
int  tls_read(tls_t *t, cu8 *buf, size_t max, cu32 timeout_ms);          /* >0 bytes, 0 timeout, -1 closed by the server, -2 error */
void tls_close(tls_t *t);                                                /* sends close_notify */
const char *tls_error(void);
const char *tls_suite_name(const tls_t *t);

void tls_random(cu8 *out, size_t n);                                     /* provided by the OS (or the test program) */
#endif
