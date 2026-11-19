/* TLS 1.2 client. See tls.h. */
#include "tls.h"

static const char *terr = "";
const char *tls_error(void) { return terr; }
#define FAIL(m) do { terr = (m); return -1; } while (0)

static void cpy(void *d, const void *s, size_t n) { cu8 *a = d; const cu8 *b = s; while (n--) *a++ = *b++; }
static int  eq(const cu8 *a, const cu8 *b, size_t n) { cu8 x = 0; while (n--) x |= *a++ ^ *b++; return x == 0; }
static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

enum { CT_CCS = 20, CT_ALERT = 21, CT_HS = 22, CT_APP = 23 };
enum { HS_CLIENT_HELLO = 1, HS_SERVER_HELLO = 2, HS_CERT = 11, HS_SKE = 12, HS_CERTREQ = 13, HS_SHD = 14, HS_CKE = 16, HS_FINISHED = 20 };

/* ---- PRF (HMAC-SHA256 or SHA384) ---- */
static void prf(int alg, const cu8 *secret, size_t sl, const char *label, const cu8 *seed1, size_t l1, const cu8 *seed2, size_t l2, cu8 *out, size_t outlen)
{
    cu8 ls[64 + 128];
    size_t ll = slen(label);
    cpy(ls, label, ll);
    cpy(ls + ll, seed1, l1);
    if (l2) cpy(ls + ll + l1, seed2, l2);
    size_t seedlen = ll + l1 + l2;
    cu8 a[64], blk[64];
    int hl = cr_hash_len(alg);
    const cu8 *parts[2]; size_t lens[2];
    parts[0] = ls; lens[0] = seedlen;
    cr_hmac(alg, secret, sl, parts, lens, 1, a);                                   /* A(1) */
    size_t done = 0;
    while (done < outlen) {
        parts[0] = a; lens[0] = (size_t)hl; parts[1] = ls; lens[1] = seedlen;
        cr_hmac(alg, secret, sl, parts, lens, 2, blk);
        size_t take = outlen - done < (size_t)hl ? outlen - done : (size_t)hl;
        cpy(out + done, blk, take);
        done += take;
        parts[0] = a; lens[0] = (size_t)hl;
        cr_hmac(alg, secret, sl, parts, lens, 1, a);
    }
}

/* ---- records ---- */
static int send_record(tls_t *t, int type, const cu8 *data, size_t len)
{
    static cu8 out[TLS_RBUF];
    if (len > 16384) return -1;
    if (!t->enc_out) {
        out[0] = (cu8)type; out[1] = 3; out[2] = 3; out[3] = (cu8)(len >> 8); out[4] = (cu8)len;
        cpy(out + 5, data, len);
        return t->io.send(t->io.ctx, out, 5 + len);
    }
    cu8 nonce[12], aad[13], tag[16];
    cpy(nonce, t->iv_out, 4);
    for (int i = 0; i < 8; i++) { nonce[4 + i] = (cu8)(t->seq_out >> (56 - 8 * i)); aad[i] = nonce[4 + i]; }
    aad[8] = (cu8)type; aad[9] = 3; aad[10] = 3; aad[11] = (cu8)(len >> 8); aad[12] = (cu8)len;
    size_t total = 8 + len + 16;
    out[0] = (cu8)type; out[1] = 3; out[2] = 3; out[3] = (cu8)(total >> 8); out[4] = (cu8)total;
    cpy(out + 5, nonce + 4, 8);
    cr_gcm_seal(&t->gout, nonce, aad, 13, data, len, out + 13, tag);
    cpy(out + 13 + len, tag, 16);
    t->seq_out++;
    return t->io.send(t->io.ctx, out, 5 + total);
}

static int recv_exact(tls_t *t, cu8 *buf, size_t n, cu32 timeout)
{
    size_t got = 0;
    while (got < n) {
        int r = t->io.recv(t->io.ctx, buf + got, n - got, timeout);
        if (r == 0) { terr = "timeout waiting for the server"; return -2; }
        if (r < 0) { terr = "the connection was closed"; return -1; }
        got += (size_t)r;
    }
    return 0;
}

/* reads one record; decrypts it when encryption is on. 0 = ok (*type, payload in t->rbuf+5.. of length *len), -1 closed, -2 error */
static int read_record(tls_t *t, int *type, cu8 **payload, size_t *len, cu32 timeout)
{
    int r = recv_exact(t, t->rbuf, 5, timeout);
    if (r) return r;
    size_t l = (size_t)t->rbuf[3] << 8 | t->rbuf[4];
    if (t->rbuf[1] != 3 || l > TLS_RBUF - 5) { terr = "bad TLS record"; return -2; }
    r = recv_exact(t, t->rbuf + 5, l, 15000);
    if (r) return r;
    *type = t->rbuf[0];
    if (!t->enc_in || *type == CT_CCS) { *payload = t->rbuf + 5; *len = l; return 0; }
    if (l < 24) { terr = "short encrypted record"; return -2; }
    cu8 nonce[12], aad[13];
    cpy(nonce, t->iv_in, 4);
    cpy(nonce + 4, t->rbuf + 5, 8);
    size_t pl = l - 24;
    for (int i = 0; i < 8; i++) aad[i] = (cu8)(t->seq_in >> (56 - 8 * i));
    aad[8] = (cu8)*type; aad[9] = 3; aad[10] = 3; aad[11] = (cu8)(pl >> 8); aad[12] = (cu8)pl;
    if (cr_gcm_open(&t->gin, nonce, aad, 13, t->rbuf + 13, pl, t->rbuf + 13, t->rbuf + 13 + pl)) { terr = "record authentication failed (bad MAC)"; return -2; }
    t->seq_in++;
    *payload = t->rbuf + 13; *len = pl;
    return 0;
}

static const char *alert_name(int d)
{
    switch (d) {
    case 0: return "close_notify"; case 10: return "unexpected_message"; case 20: return "bad_record_mac"; case 40: return "handshake_failure";
    case 42: return "bad_certificate"; case 43: return "unsupported_certificate"; case 44: return "certificate_revoked"; case 45: return "certificate_expired";
    case 46: return "certificate_unknown"; case 47: return "illegal_parameter"; case 48: return "unknown_ca"; case 50: return "decode_error";
    case 51: return "decrypt_error"; case 70: return "protocol_version"; case 71: return "insufficient_security"; case 80: return "internal_error";
    case 112: return "unrecognized_name"; default: return "alert";
    }
}

static void transcript(tls_t *t, const cu8 *d, size_t n) { cr_hash_update(&t->th256, d, n); cr_hash_update(&t->th384, d, n); }

static int send_hs(tls_t *t, int type, const cu8 *body, size_t len)
{
    static cu8 m[16384];
    if (len + 4 > sizeof m) return -1;
    m[0] = (cu8)type; m[1] = (cu8)(len >> 16); m[2] = (cu8)(len >> 8); m[3] = (cu8)len;
    cpy(m + 4, body, len);
    transcript(t, m, len + 4);
    return send_record(t, CT_HS, m, len + 4);
}

/* next complete handshake message in t->hs (starting at t->hs[0]); fetches records until there is one.
 * Returns 0 and sets *type, *body, *blen (message stays at the front until hs_pop). */
static int hs_next(tls_t *t, int *type, const cu8 **body, size_t *blen, int *got_ccs)
{
    for (;;) {
        if (t->hs_len >= 4) {
            size_t l = (size_t)t->hs[1] << 16 | (size_t)t->hs[2] << 8 | t->hs[3];
            if (l + 4 > TLS_HSBUF) FAIL("handshake message too large");
            if (t->hs_len >= l + 4) { *type = t->hs[0]; *body = t->hs + 4; *blen = l; return 0; }
        }
        int rt; cu8 *p; size_t pl;
        int r = read_record(t, &rt, &p, &pl, 15000);
        if (r) return -1;
        if (rt == CT_ALERT) {
            if (pl >= 2) { static char m[48]; const char *a = alert_name(p[1]); size_t i = 0; const char *pre = "the server sent alert: "; while (pre[i]) { m[i] = pre[i]; i++; } for (size_t j = 0; a[j] && i < 46; j++) m[i++] = a[j]; m[i] = 0; terr = m; }
            return -1;
        }
        if (rt == CT_CCS) { if (got_ccs) { *got_ccs = 1; if (t->hs_len == 0) { *type = -1; *body = 0; *blen = 0; return 0; } } continue; }
        if (rt != CT_HS) FAIL("unexpected record during the handshake");
        if (t->hs_len + pl > TLS_HSBUF) FAIL("handshake data too large");
        cpy(t->hs + t->hs_len, p, pl);
        t->hs_len += pl;
    }
}

static void hs_pop(tls_t *t, size_t blen)
{
    size_t tot = blen + 4;
    transcript(t, t->hs, tot);
    for (size_t i = 0; i + tot < t->hs_len; i++) t->hs[i] = t->hs[i + tot];
    t->hs_len -= tot;
}

static void put16(cu8 *p, unsigned v) { p[0] = (cu8)(v >> 8); p[1] = (cu8)v; }

static int is_ipv4(const char *s)
{
    int dots = 0;
    for (; *s; s++) { if (*s == '.') dots++; else if (*s < '0' || *s > '9') return 0; }
    return dots == 3;
}

const char *tls_suite_name(const tls_t *t)
{
    if (!t->up) return "";
    return t->keylen == 16 ? "TLS 1.2 ECDHE + AES-128-GCM" : "TLS 1.2 ECDHE + AES-256-GCM";
}

int tls_connect(tls_t *t, const tls_io *io, const char *host, int verify, cu32 now, const cu8 *roots, size_t roots_len)
{
    t->io = *io;
    t->up = t->closed = t->verified = 0; t->enc_in = t->enc_out = 0; t->seq_in = t->seq_out = 0;
    t->hs_len = 0; t->app_len = t->app_off = 0; t->ems = 0;
    cr_hash_init(&t->th256, CR_SHA256); cr_hash_init(&t->th384, CR_SHA384);
    tls_random(t->crandom, 32);

    /* ---- ClientHello ---- */
    cu8 ch[512]; size_t n = 0;
    ch[n++] = 3; ch[n++] = 3;
    cpy(ch + n, t->crandom, 32); n += 32;
    ch[n++] = 0;                                                                   /* session id */
    static const cu8 suites[] = { 0xc0, 0x2b, 0xc0, 0x2f, 0xc0, 0x2c, 0xc0, 0x30, 0x00, 0xff };
    put16(ch + n, sizeof suites); n += 2; cpy(ch + n, suites, sizeof suites); n += sizeof suites;
    ch[n++] = 1; ch[n++] = 0;                                                      /* compression: none */
    size_t ext_at = n; n += 2;
    size_t hl = slen(host);
    if (host[0] && !is_ipv4(host) && hl < 200) {
        put16(ch + n, 0); put16(ch + n + 2, (unsigned)hl + 5); put16(ch + n + 4, (unsigned)hl + 3); ch[n + 6] = 0; put16(ch + n + 7, (unsigned)hl);
        cpy(ch + n + 9, host, hl); n += 9 + hl;
    }
    put16(ch + n, 0x0017); put16(ch + n + 2, 0); n += 4;                           /* extended master secret */
    put16(ch + n, 0x000a); put16(ch + n + 2, 6); put16(ch + n + 4, 4); put16(ch + n + 6, 0x001d); put16(ch + n + 8, 0x0017); n += 10;
    put16(ch + n, 0x000b); put16(ch + n + 2, 2); ch[n + 4] = 1; ch[n + 5] = 0; n += 6;
    put16(ch + n, 0x000d); put16(ch + n + 2, 12); put16(ch + n + 4, 10);
    put16(ch + n + 6, 0x0403); put16(ch + n + 8, 0x0503); put16(ch + n + 10, 0x0401); put16(ch + n + 12, 0x0501); put16(ch + n + 14, 0x0601); n += 16;
    put16(ch + ext_at, (unsigned)(n - ext_at - 2));
    if (send_hs(t, HS_CLIENT_HELLO, ch, n)) FAIL("could not send the ClientHello");

    /* ---- ServerHello ---- */
    int type, got_ccs = 0; const cu8 *b; size_t bl;
    if (hs_next(t, &type, &b, &bl, 0)) return -1;
    if (type != HS_SERVER_HELLO) FAIL("expected a ServerHello");
    if (bl < 38 || b[0] != 3 || b[1] != 3) FAIL("the server does not speak TLS 1.2");
    cpy(t->srandom, b + 2, 32);
    size_t p = 34, sid = b[p++];
    p += sid;
    if (p + 3 > bl) FAIL("bad ServerHello");
    unsigned suite = (unsigned)b[p] << 8 | b[p + 1]; p += 2;
    if (b[p++] != 0) FAIL("the server wants compression");
    if (suite != 0xc02b && suite != 0xc02f && suite != 0xc02c && suite != 0xc030) FAIL("the server chose a cipher suite this client does not offer");
    int ecdsa = suite == 0xc02b || suite == 0xc02c;
    t->keylen = (suite == 0xc02b || suite == 0xc02f) ? 16 : 32;
    t->hash_alg = t->keylen == 16 ? CR_SHA256 : CR_SHA384;
    if (p + 2 <= bl) {
        size_t el = (size_t)b[p] << 8 | b[p + 1]; p += 2;
        if (p + el > bl) FAIL("bad ServerHello extensions");
        size_t q = p;
        while (q + 4 <= p + el) {
            unsigned et = (unsigned)b[q] << 8 | b[q + 1]; size_t ell = (size_t)b[q + 2] << 8 | b[q + 3];
            if (et == 0x0017) t->ems = 1;
            q += 4 + ell;
        }
    }
    hs_pop(t, bl);

    /* ---- Certificate ---- */
    if (hs_next(t, &type, &b, &bl, 0)) return -1;
    if (type != HS_CERT) FAIL("expected the server certificate");
    static const cu8 *cptr[10]; static size_t clen[10];
    int ncert = 0;
    {
        if (bl < 3) FAIL("bad Certificate message");
        size_t total = (size_t)b[0] << 16 | (size_t)b[1] << 8 | b[2], q = 3;
        if (total + 3 != bl) FAIL("bad Certificate message");
        while (q + 3 <= bl) {
            size_t cl = (size_t)b[q] << 16 | (size_t)b[q + 1] << 8 | b[q + 2]; q += 3;
            if (q + cl > bl) FAIL("bad Certificate message");
            if (ncert < 10) { cptr[ncert] = b + q; clen[ncert] = cl; ncert++; }
            q += cl;
        }
    }
    if (ncert < 1) FAIL("the server sent no certificate");
    x509 leaf;
    if (x509_parse(&leaf, cptr[0], clen[0])) { terr = x509_error(); return -1; }
    if (verify) {
        if (x509_verify_chain(cptr, clen, ncert, host, now, roots, roots_len)) { terr = x509_error(); return -1; }
        t->verified = 1;
    }
    x509_key lk = leaf.key;
    int leaf_is_ec = lk.type == 2;
    if (ecdsa != leaf_is_ec) FAIL("the certificate key does not fit the cipher suite");
    /* the key must survive hs_pop (message data moves): copy what is needed later */
    cu8 lkbuf[600]; size_t lkn = 0, lkmod = 0, lkexp = 0;
    cu8 ecpub[133]; size_t ecn = 0; int ccurve = lk.curve;
    if (leaf_is_ec) { if (lk.pub_len > sizeof ecpub) FAIL("EC key too long"); cpy(ecpub, lk.pub, lk.pub_len); ecn = lk.pub_len; }
    else { if (lk.n_len > 512 || lk.e_len > 8) FAIL("RSA key too long"); cpy(lkbuf, lk.n, lk.n_len); lkmod = lk.n_len; cpy(lkbuf + 520, lk.e, lk.e_len); lkexp = lk.e_len; }
    (void)lkn;
    hs_pop(t, bl);

    /* ---- ServerKeyExchange ---- */
    if (hs_next(t, &type, &b, &bl, 0)) return -1;
    if (type != HS_SKE) FAIL("expected a ServerKeyExchange");
    if (bl < 4 || b[0] != 3) FAIL("unsupported key exchange");
    unsigned group = (unsigned)b[1] << 8 | b[2];
    size_t pklen = b[3];
    if (4 + pklen + 4 > bl) FAIL("bad ServerKeyExchange");
    cu8 peer[80];
    if (pklen > sizeof peer) FAIL("bad server key share");
    cpy(peer, b + 4, pklen);
    unsigned sigalg = (unsigned)b[4 + pklen] << 8 | b[5 + pklen];
    size_t siglen = (size_t)b[6 + pklen] << 8 | b[7 + pklen];
    if (8 + pklen + siglen != bl) FAIL("bad ServerKeyExchange signature");
    if (group != 0x001d && group != 0x0017) FAIL("the server chose an unsupported curve");
    if ((group == 0x001d && pklen != 32) || (group == 0x0017 && pklen != 65)) FAIL("bad server key share");
    if (verify) {
        int ha, rsa;
        switch (sigalg) {
        case 0x0401: ha = CR_SHA256; rsa = 1; break;
        case 0x0501: ha = CR_SHA384; rsa = 1; break;
        case 0x0601: ha = CR_SHA512; rsa = 1; break;
        case 0x0403: ha = CR_SHA256; rsa = 0; break;
        case 0x0503: ha = CR_SHA384; rsa = 0; break;
        case 0x0603: ha = CR_SHA512; rsa = 0; break;
        default: FAIL("the server signed with an algorithm this client did not offer");
        }
        if (rsa == leaf_is_ec) FAIL("the signature type does not fit the certificate");
        cr_hash h; cu8 dg[64];
        cr_hash_init(&h, ha);
        cr_hash_update(&h, t->crandom, 32); cr_hash_update(&h, t->srandom, 32); cr_hash_update(&h, b, 4 + pklen);
        cr_hash_final(&h, dg);
        int bad = rsa ? cr_rsa_verify(lkbuf, lkmod, lkbuf + 520, lkexp, ha, dg, b + 8 + pklen, siglen)
                      : cr_ecdsa_verify(ccurve, ecpub, ecn, dg, (size_t)cr_hash_len(ha), b + 8 + pklen, siglen);
        if (bad) FAIL("the server's key exchange signature is wrong");
    }
    hs_pop(t, bl);

    /* ---- [CertificateRequest] ServerHelloDone ---- */
    int cert_requested = 0;
    if (hs_next(t, &type, &b, &bl, 0)) return -1;
    if (type == HS_CERTREQ) { cert_requested = 1; hs_pop(t, bl); if (hs_next(t, &type, &b, &bl, 0)) return -1; }
    if (type != HS_SHD) FAIL("expected ServerHelloDone");
    hs_pop(t, bl);

    /* ---- our key share ---- */
    cu8 rnd[32], priv[32], pub[65], shared[32];
    size_t publen;
    tls_random(rnd, 32);
    if (group == 0x001d) {
        for (int i = 0; i < 32; i++) priv[i] = rnd[i];
        cr_x25519_base(pub, priv); publen = 32;
        if (cr_x25519(shared, priv, peer)) FAIL("bad server key share (all-zero secret)");
    } else {
        if (cr_p256_keygen(rnd, priv, pub)) FAIL("key generation failed");
        publen = 65;
        if (cr_p256_shared(priv, peer, pklen, shared)) FAIL("the server's key share is not a valid curve point");
    }
    if (cert_requested) { cu8 empty[3] = { 0, 0, 0 }; if (send_hs(t, HS_CERT, empty, 3)) FAIL("send failed"); }
    cu8 cke[80];
    cke[0] = (cu8)publen; cpy(cke + 1, pub, publen);
    if (send_hs(t, HS_CKE, cke, publen + 1)) FAIL("could not send the key exchange");

    /* ---- master secret and keys ---- */
    if (t->ems) {
        cr_hash c = t->hash_alg == CR_SHA256 ? t->th256 : t->th384;
        cu8 sh[64];
        cr_hash_final(&c, sh);
        prf(t->hash_alg, shared, 32, "extended master secret", sh, (size_t)cr_hash_len(t->hash_alg), 0, 0, t->master, 48);
    } else {
        prf(t->hash_alg, shared, 32, "master secret", t->crandom, 32, t->srandom, 32, t->master, 48);
    }
    cu8 kb[2 * 32 + 2 * 4];
    prf(t->hash_alg, t->master, 48, "key expansion", t->srandom, 32, t->crandom, 32, kb, (size_t)(2 * t->keylen + 8));
    cr_gcm_init(&t->gout, kb, t->keylen);
    cr_gcm_init(&t->gin, kb + t->keylen, t->keylen);
    cpy(t->iv_out, kb + 2 * t->keylen, 4);
    cpy(t->iv_in, kb + 2 * t->keylen + 4, 4);
    for (size_t i = 0; i < sizeof priv; i++) priv[i] = 0;
    for (size_t i = 0; i < sizeof shared; i++) shared[i] = 0;

    /* ---- ChangeCipherSpec + Finished ---- */
    { cu8 one = 1; if (send_record(t, CT_CCS, &one, 1)) FAIL("send failed"); }
    t->enc_out = 1; t->seq_out = 0;
    {
        cr_hash c = t->hash_alg == CR_SHA256 ? t->th256 : t->th384;
        cu8 sh[64], vd[12];
        cr_hash_final(&c, sh);
        prf(t->hash_alg, t->master, 48, "client finished", sh, (size_t)cr_hash_len(t->hash_alg), 0, 0, vd, 12);
        if (send_hs(t, HS_FINISHED, vd, 12)) FAIL("could not send Finished");
    }

    /* ---- server ChangeCipherSpec + Finished ---- */
    {
        /* expected verify_data covers everything up to and including our Finished (already in the transcript) */
        cr_hash c = t->hash_alg == CR_SHA256 ? t->th256 : t->th384;
        cu8 sh[64], vd[12];
        cr_hash_final(&c, sh);
        prf(t->hash_alg, t->master, 48, "server finished", sh, (size_t)cr_hash_len(t->hash_alg), 0, 0, vd, 12);
        for (;;) {
            int rt; cu8 *pp; size_t pl;
            int r = read_record(t, &rt, &pp, &pl, 15000);
            if (r) return -1;
            if (rt == CT_ALERT) { terr = pl >= 2 ? alert_name(pp[1]) : "alert"; if (pl >= 2) { static char m[48]; const char *pre = "the server sent alert: "; size_t i = 0; while (pre[i]) { m[i] = pre[i]; i++; } const char *a = alert_name(pp[1]); for (size_t j = 0; a[j] && i < 46; j++) m[i++] = a[j]; m[i] = 0; terr = m; } return -1; }
            if (rt == CT_CCS) { t->enc_in = 1; t->seq_in = 0; got_ccs = 1; continue; }
            if (rt != CT_HS || !got_ccs) FAIL("unexpected record while waiting for Finished");
            if (pl != 16 || pp[0] != HS_FINISHED || pp[3] != 12) FAIL("bad server Finished");
            if (!eq(pp + 4, vd, 12)) FAIL("the server's Finished does not match (handshake was tampered with)");
            break;
        }
    }
    t->up = 1;
    return 0;
}

int tls_write(tls_t *t, const cu8 *data, size_t len)
{
    if (!t->up || t->closed) FAIL("the TLS connection is not open");
    while (len) {
        size_t take = len > 16000 ? 16000 : len;
        if (send_record(t, CT_APP, data, take)) FAIL("sending data failed");
        data += take; len -= take;
    }
    return 0;
}

int tls_read(tls_t *t, cu8 *buf, size_t max, cu32 timeout)
{
    for (;;) {
        if (t->app_off < t->app_len) {
            size_t take = t->app_len - t->app_off < max ? t->app_len - t->app_off : max;
            cpy(buf, t->app + t->app_off, take);
            t->app_off += take;
            return (int)take;
        }
        if (t->closed) return -1;
        int rt; cu8 *p; size_t pl;
        int r = read_record(t, &rt, &p, &pl, timeout);
        if (r == -2 && terr[0] == 't' && terr[1] == 'i') return 0;                      /* timeout */
        if (r == -1) { t->closed = 1; return -1; }
        if (r) return -2;
        if (rt == CT_ALERT) {
            if (pl >= 2 && p[1] == 0) { t->closed = 1; return -1; }                      /* close_notify */
            terr = pl >= 2 ? alert_name(p[1]) : "alert";
            t->closed = 1;
            return -2;
        }
        if (rt == CT_APP) { if (pl > sizeof t->app) { terr = "record too large"; return -2; } cpy(t->app, p, pl); t->app_len = pl; t->app_off = 0; continue; }
        /* handshake records after the handshake (HelloRequest etc.) are ignored */
    }
}

void tls_close(tls_t *t)
{
    if (t->up && !t->closed) { cu8 a[2] = { 1, 0 }; send_record(t, CT_ALERT, a, 2); }
    t->closed = 1; t->up = 0;
}
