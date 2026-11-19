/*
 * Cryptographic primitives for the TLS client (written from scratch, no libraries): SHA-256/384/512, HMAC, AES-GCM,
 * X25519, ECDH P-256, ECDSA P-256/P-384 verification, RSA PKCS#1 v1.5 verification.
 * Plain C99 without any OS dependency (it also builds as a normal host program for the tests); no 64-bit division.
 */
#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  cu8;
typedef uint32_t cu32;
typedef uint64_t cu64;

enum { CR_SHA256 = 1, CR_SHA384 = 2, CR_SHA512 = 3 };

typedef struct { cu32 h[8]; cu64 len; cu8 buf[64]; cu32 n; } cr_sha256_ctx;
typedef struct { cu64 h[8]; cu64 len; cu8 buf[128]; cu32 n; int is384; } cr_sha512_ctx;
typedef struct { int alg; union { cr_sha256_ctx a; cr_sha512_ctx b; } u; } cr_hash;

void cr_hash_init(cr_hash *c, int alg);
void cr_hash_update(cr_hash *c, const void *data, size_t len);
void cr_hash_final(cr_hash *c, cu8 *out);                 /* c may be copied before final to take a snapshot */
int  cr_hash_len(int alg);
int  cr_hash_block(int alg);
void cr_hash_once(int alg, const void *data, size_t len, cu8 *out);

/* HMAC over up to 4 pieces */
void cr_hmac(int alg, const cu8 *key, size_t klen, const cu8 *const *parts, const size_t *lens, int np, cu8 *out);

/* AES-128/256-GCM */
typedef struct { cu32 rk[60]; int rounds; cu64 HL[16], HH[16]; } cr_gcm;
int  cr_gcm_init(cr_gcm *g, const cu8 *key, int keylen);             /* keylen 16 or 32; 0 = ok */
void cr_gcm_seal(cr_gcm *g, const cu8 iv[12], const cu8 *aad, size_t alen, const cu8 *in, size_t len, cu8 *out, cu8 tag[16]);
int  cr_gcm_open(cr_gcm *g, const cu8 iv[12], const cu8 *aad, size_t alen, const cu8 *in, size_t len, cu8 *out, const cu8 tag[16]); /* 0 = ok */

/* key exchange */
int  cr_x25519(cu8 out[32], const cu8 scalar[32], const cu8 point[32]);   /* 0 = ok, -1 = all-zero result */
void cr_x25519_base(cu8 out[32], const cu8 scalar[32]);
int  cr_p256_keygen(const cu8 rnd[32], cu8 priv[32], cu8 pub[65]);          /* rnd: 32 random bytes; 0 = ok */
int  cr_p256_shared(const cu8 priv[32], const cu8 *peer, size_t peerlen, cu8 out[32]);   /* 0 = ok, -1 = bad point */

/* signatures */
enum { CR_P256 = 1, CR_P384 = 2 };
int  cr_ecdsa_verify(int curve, const cu8 *pub, size_t publen, const cu8 *hash, size_t hlen, const cu8 *sig, size_t siglen);  /* 0 = valid */
int  cr_rsa_verify(const cu8 *n, size_t nlen, const cu8 *e, size_t elen, int hashalg, const cu8 *hash, const cu8 *sig, size_t siglen); /* 0 = valid */

#endif
