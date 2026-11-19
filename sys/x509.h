/* X.509 certificate parsing and chain validation for the TLS client (see x509.c). Plain C, no OS dependency. */
#ifndef X509_H
#define X509_H
#include "crypto.h"

typedef struct {
    int type;                        /* 1 = RSA, 2 = EC */
    const cu8 *n, *e; size_t n_len, e_len;
    int curve;                       /* CR_P256 / CR_P384 */
    const cu8 *pub; size_t pub_len;  /* uncompressed EC point */
} x509_key;

typedef struct {
    const cu8 *tbs; size_t tbs_len;
    const cu8 *issuer; size_t issuer_len;
    const cu8 *subject; size_t subject_len;
    const cu8 *spki; size_t spki_len;
    x509_key key;
    int sig_type, sig_hash;          /* 1 = RSA PKCS#1, 2 = ECDSA; CR_SHA* */
    const cu8 *sig; size_t sig_len;
    cu32 not_before, not_after;      /* seconds since 1970 (saturated at 2106) */
    int has_bc, is_ca, path_len;     /* path_len -1 = none */
    int has_ku; cu32 ku;             /* KeyUsage bits (bit 0 = digitalSignature ... keyCertSign = 5) */
    int has_eku, eku_server;
    const cu8 *san; size_t san_len;  /* GeneralNames contents */
    const cu8 *cn; size_t cn_len;    /* commonName of the subject (fallback when there is no SAN) */
} x509;

const char *x509_error(void);
int  x509_parse(x509 *c, const cu8 *der, size_t len);                  /* 0 = ok */
int  x509_parse_spki(x509_key *k, const cu8 *spki, size_t len);        /* 0 = ok */
int  x509_verify_sig(const x509 *c, const x509_key *issuer);           /* 0 = the signature of c is valid for this key */
int  x509_host_match(const x509 *c, const char *host);                 /* 1 = the certificate is valid for the host name / IPv4 */

/*
 * Checks a server's certificate chain (certs[0] = the server certificate): host name, validity dates at time `now`
 * (seconds since 1970), signatures up to one of the trust anchors. roots = list of [u16 subject_len][u16 spki_len]
 * [subject DER][SPKI DER]. Returns 0 when valid, otherwise -1 and x509_error() says why. Not checked: revocation,
 * name constraints, certificate transparency, policies.
 */
int  x509_verify_chain(const cu8 *const *certs, const size_t *lens, int n, const char *host, cu32 now, const cu8 *roots, size_t roots_len);

#endif
