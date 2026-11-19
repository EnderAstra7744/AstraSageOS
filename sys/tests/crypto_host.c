/* Host test for sys/crypto.c: reads the vector file written by crypto_vectors.py (reference results from hashlib /
 * hmac / the cryptography package) and checks every primitive against it. Build: see sys/tests/run_crypto_test.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../crypto.h"

static int fails, total;
static unsigned char *unhex(const char *s, size_t *len)
{
    if (!strcmp(s, "-")) { *len = 0; return calloc(1, 1); }
    size_t n = strlen(s) / 2;
    unsigned char *b = malloc(n + 1);
    for (size_t i = 0; i < n; i++) { unsigned v; sscanf(s + 2 * i, "%2x", &v); b[i] = (unsigned char)v; }
    *len = n;
    return b;
}
static void check(int ok, const char *what, int line)
{
    total++;
    if (!ok) { fails++; printf("FAIL line %d: %s\n", line, what); }
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "r");
    static char line[400000];
    int ln = 0;
    clock_t t0 = clock();
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *tok[8]; int nt = 0;
        for (char *p = strtok(line, " \n"); p && nt < 8; p = strtok(0, " \n")) tok[nt++] = p;
        if (!nt) continue;
        size_t l1, l2, l3, l4, l5;
        if (!strcmp(tok[0], "HASH")) {
            unsigned char *d = unhex(tok[1], &l1), o[64], e[64];
            for (int a = 1; a <= 3; a++) {
                cr_hash_once(a, d, l1, o);
                unsigned char *ex = unhex(tok[1 + a], &l2);
                check(!memcmp(o, ex, l2) && (int)l2 == cr_hash_len(a), "hash", ln);
                free(ex);
            }
            /* incremental: odd-sized chunks */
            cr_hash c; cr_hash_init(&c, 2);
            for (size_t i = 0; i < l1; i += 37) cr_hash_update(&c, d + i, l1 - i < 37 ? l1 - i : 37);
            cr_hash_final(&c, o);
            unsigned char *ex = unhex(tok[3], &l2); check(!memcmp(o, ex, l2), "sha384 chunks", ln); free(ex); free(d); (void)e;
        } else if (!strcmp(tok[0], "HMAC")) {
            unsigned char *k = unhex(tok[1], &l1), *d = unhex(tok[2], &l2), o[64];
            for (int a = 1; a <= 3; a++) {
                const unsigned char *parts[2] = { d, d + 10 }; size_t lens[2] = { 10, l2 - 10 };
                cr_hmac(a, k, l1, parts, lens, 2, o);
                unsigned char *ex = unhex(tok[2 + a], &l3);
                check(!memcmp(o, ex, l3), "hmac", ln); free(ex);
            }
            free(k); free(d);
        } else if (!strcmp(tok[0], "GCM")) {
            unsigned char *k = unhex(tok[1], &l1), *iv = unhex(tok[2], &l2), *aad = unhex(tok[3], &l3), *pt = unhex(tok[4], &l4), *ct = unhex(tok[5], &l5);
            cr_gcm g; cr_gcm_init(&g, k, (int)l1);
            unsigned char *o = malloc(l4 + 1), tag[16], *o2 = malloc(l4 + 1);
            cr_gcm_seal(&g, iv, aad, l3, pt, l4, o, tag);
            check(!memcmp(o, ct, l4) && !memcmp(tag, ct + l4, 16), "gcm seal", ln);
            check(cr_gcm_open(&g, iv, aad, l3, ct, l4, o2, ct + l4) == 0 && !memcmp(o2, pt, l4), "gcm open", ln);
            unsigned char bad[16]; memcpy(bad, ct + l4, 16); bad[5] ^= 1;
            check(cr_gcm_open(&g, iv, aad, l3, ct, l4, o2, bad) != 0, "gcm bad tag refused", ln);
            free(k); free(iv); free(aad); free(pt); free(ct); free(o); free(o2);
        } else if (!strcmp(tok[0], "X25519")) {
            unsigned char *sk = unhex(tok[1], &l1), *pk = unhex(tok[2], &l2), *ex = unhex(tok[3], &l3), *ap = unhex(tok[4], &l4), o[32];
            check(cr_x25519(o, sk, pk) == 0 && !memcmp(o, ex, 32), "x25519 shared", ln);
            cr_x25519_base(o, sk);
            check(!memcmp(o, ap, 32), "x25519 public key", ln);
            free(sk); free(pk); free(ex); free(ap);
        } else if (!strcmp(tok[0], "P256")) {
            unsigned char *sk = unhex(tok[1], &l1), *pb = unhex(tok[2], &l2), *ex = unhex(tok[3], &l3), *pa = unhex(tok[4], &l4), o[32], priv[32], pub[65];
            check(cr_p256_shared(sk, pb, l2, o) == 0 && !memcmp(o, ex, 32), "p256 shared", ln);
            check(cr_p256_keygen(sk, priv, pub) == 0 && !memcmp(pub, pa, 65), "p256 keygen", ln);
            pb[40] ^= 1;
            check(cr_p256_shared(sk, pb, l2, o) != 0, "p256 off-curve point refused", ln);
            free(sk); free(pb); free(ex); free(pa);
        } else if (!strcmp(tok[0], "ECDSA")) {
            int cid = atoi(tok[1]);
            unsigned char *pub = unhex(tok[2], &l1), *dg = unhex(tok[3], &l2), *sig = unhex(tok[4], &l3);
            int want = atoi(tok[5]);
            check((cr_ecdsa_verify(cid, pub, l1, dg, l2, sig, l3) == 0) == want, want ? "ecdsa valid accepted" : "ecdsa bad refused", ln);
            free(pub); free(dg); free(sig);
        } else if (!strcmp(tok[0], "RSA")) {
            unsigned char *n = unhex(tok[1], &l1), *e = unhex(tok[2], &l2), *dg = unhex(tok[4], &l3), *sig = unhex(tok[5], &l4);
            int want = atoi(tok[6]);
            check((cr_rsa_verify(n, l1, e, l2, atoi(tok[3]), dg, sig, l4) == 0) == want, want ? "rsa valid accepted" : "rsa bad refused", ln);
            free(n); free(e); free(dg); free(sig);
        }
    }
    printf("%d/%d crypto checks passed (%.2fs)\n", total - fails, total, (double)(clock() - t0) / CLOCKS_PER_SEC);
    return fails != 0;
}
