/* X.509 parsing (DER) and certificate chain validation. */
#include "x509.h"

static const char *err_msg = "";
const char *x509_error(void) { return err_msg; }
#define FAIL(m) do { err_msg = (m); return -1; } while (0)

static int mem_eq(const cu8 *a, const cu8 *b, size_t n) { while (n--) if (*a++ != *b++) return 0; return 1; }

/* ---- DER reader ---- */
typedef struct { const cu8 *p; size_t n; } der;

static int der_next(der *d, int *tag, der *content, const cu8 **whole, size_t *wlen)
{
    if (d->n < 2) return -1;
    const cu8 *s = d->p;
    *tag = s[0];
    size_t hl, l;
    if (s[1] < 0x80) { hl = 2; l = s[1]; }
    else if (s[1] == 0x81 && d->n >= 3) { hl = 3; l = s[2]; }
    else if (s[1] == 0x82 && d->n >= 4) { hl = 4; l = (size_t)s[2] << 8 | s[3]; }
    else if (s[1] == 0x83 && d->n >= 5) { hl = 5; l = (size_t)s[2] << 16 | (size_t)s[3] << 8 | s[4]; }
    else return -1;
    if (hl + l > d->n) return -1;
    content->p = s + hl; content->n = l;
    if (whole) *whole = s;
    if (wlen) *wlen = hl + l;
    d->p += hl + l; d->n -= hl + l;
    return 0;
}

static int der_expect(der *d, int tag, der *content, const cu8 **whole, size_t *wlen)
{
    int t;
    der save = *d;
    if (der_next(d, &t, content, whole, wlen) || t != tag) { *d = save; return -1; }
    return 0;
}

/* ---- OIDs (content bytes) ---- */
#define OID(name, ...) static const cu8 name[] = { __VA_ARGS__ }
OID(O_SHA256_RSA, 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0b);
OID(O_SHA384_RSA, 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0c);
OID(O_SHA512_RSA, 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0d);
OID(O_SHA1_RSA,   0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x05);
OID(O_PSS,        0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0a);
OID(O_ECDSA256,   0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x02);
OID(O_ECDSA384,   0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x03);
OID(O_ECDSA512,   0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x04);
OID(O_RSA,        0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01);
OID(O_EC,         0x2a,0x86,0x48,0xce,0x3d,0x02,0x01);
OID(O_P256,       0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07);
OID(O_P384,       0x2b,0x81,0x04,0x00,0x22);
OID(O_CN,         0x55,0x04,0x03);
OID(E_BC,         0x55,0x1d,0x13);
OID(E_KU,         0x55,0x1d,0x0f);
OID(E_EKU,        0x55,0x1d,0x25);
OID(E_SAN,        0x55,0x1d,0x11);
OID(E_SKI,        0x55,0x1d,0x0e);
OID(E_AKI,        0x55,0x1d,0x23);
OID(E_POL,        0x55,0x1d,0x20);
OID(E_CRLDP,      0x55,0x1d,0x1f);
OID(E_AIA,        0x2b,0x06,0x01,0x05,0x05,0x07,0x01,0x01);
OID(E_SCT,        0x2b,0x06,0x01,0x04,0x01,0xd6,0x79,0x02,0x04,0x02);
OID(E_SIA,        0x2b,0x06,0x01,0x05,0x05,0x07,0x01,0x0b);
OID(E_IAN,        0x55,0x1d,0x12);
OID(E_FCRL,       0x55,0x1d,0x2e);
OID(E_NC,         0x55,0x1d,0x1e);
OID(K_SERVER,     0x2b,0x06,0x01,0x05,0x05,0x07,0x03,0x01);
OID(K_ANY,        0x55,0x1d,0x25,0x00);
#define IS(d, o) ((d).n == sizeof(o) && mem_eq((d).p, o, sizeof(o)))

/* ---- time ---- */
static int dig2(const cu8 *p, int *v)
{
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    *v = (p[0] - '0') * 10 + (p[1] - '0');
    return 0;
}

static int parse_time(int tag, der t, cu32 *out)
{
    int y, mo, d, h, mi, s = 0, c;
    const cu8 *p = t.p;
    if (tag == 0x17) {                                          /* UTCTime YYMMDDHHMM[SS]Z */
        if (t.n != 13 && t.n != 11) return -1;
        if (dig2(p, &y)) return -1;
        y += y >= 50 ? 1900 : 2000; p += 2;
    } else if (tag == 0x18) {                                   /* GeneralizedTime YYYYMMDDHH[MM[SS]]Z */
        if (t.n != 15) return -1;
        if (dig2(p, &c) || dig2(p + 2, &y)) return -1;
        y += c * 100; p += 4;
    } else return -1;
    if (dig2(p, &mo) || dig2(p + 2, &d) || dig2(p + 4, &h) || dig2(p + 6, &mi)) return -1;
    p += 8;
    if (p[0] != 'Z') { if (dig2(p, &s)) return -1; p += 2; }
    if (p[0] != 'Z' || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return -1;
    if (y >= 2106) { *out = 0xFFFFFFFFu; return 0; }
    /* days from civil (Hinnant), 32-bit */
    int yy = y - (mo <= 2);
    int era = yy / 400;
    int yoe = yy - era * 400;
    int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int days = era * 146097 + doe - 719468;
    if (days < 0) { *out = 0; return 0; }
    *out = (cu32)days * 86400u + (cu32)h * 3600u + (cu32)mi * 60u + (cu32)s;
    return 0;
}

/* ---- keys ---- */
int x509_parse_spki(x509_key *k, const cu8 *spki, size_t len)
{
    der d = { spki, len }, seq, alg, algoid, bits;
    int t;
    if (der_expect(&d, 0x30, &seq, 0, 0)) FAIL("bad public key info");
    if (der_expect(&seq, 0x30, &alg, 0, 0)) FAIL("bad public key algorithm");
    if (der_expect(&alg, 0x06, &algoid, 0, 0)) FAIL("bad public key algorithm");
    if (der_expect(&seq, 0x03, &bits, 0, 0) || bits.n < 2 || bits.p[0] != 0) FAIL("bad public key bits");
    bits.p++; bits.n--;
    if (IS(algoid, O_RSA)) {
        der rk, n, e;
        der in = bits;
        if (der_expect(&in, 0x30, &rk, 0, 0)) FAIL("bad RSA key");
        if (der_expect(&rk, 0x02, &n, 0, 0) || der_expect(&rk, 0x02, &e, 0, 0)) FAIL("bad RSA key");
        while (n.n > 1 && n.p[0] == 0) { n.p++; n.n--; }
        while (e.n > 1 && e.p[0] == 0) { e.p++; e.n--; }
        k->type = 1; k->n = n.p; k->n_len = n.n; k->e = e.p; k->e_len = e.n;
        return 0;
    }
    if (IS(algoid, O_EC)) {
        der curve;
        if (der_expect(&alg, 0x06, &curve, 0, 0)) FAIL("EC key without a named curve");
        if (IS(curve, O_P256)) k->curve = CR_P256;
        else if (IS(curve, O_P384)) k->curve = CR_P384;
        else FAIL("unsupported elliptic curve (only P-256 and P-384)");
        k->type = 2; k->pub = bits.p; k->pub_len = bits.n;
        return 0;
    }
    (void)t;
    FAIL("unsupported public key type");
}

/* ---- extensions ---- */
static int parse_extensions(x509 *c, der exts)
{
    der list, ext;
    if (der_expect(&exts, 0x30, &list, 0, 0)) FAIL("bad extensions");
    while (list.n) {
        der oid, crit = { 0, 0 }, val;
        int critical = 0;
        if (der_expect(&list, 0x30, &ext, 0, 0)) FAIL("bad extension");
        if (der_expect(&ext, 0x06, &oid, 0, 0)) FAIL("bad extension");
        if (der_expect(&ext, 0x01, &crit, 0, 0)) { critical = 0; } else critical = crit.n == 1 && crit.p[0] != 0;
        if (der_expect(&ext, 0x04, &val, 0, 0)) FAIL("bad extension value");
        if (IS(oid, E_BC)) {
            der s, b;
            if (der_expect(&val, 0x30, &s, 0, 0)) FAIL("bad basicConstraints");
            c->has_bc = 1; c->is_ca = 0; c->path_len = -1;
            if (!der_expect(&s, 0x01, &b, 0, 0)) c->is_ca = b.n == 1 && b.p[0] != 0;
            if (!der_expect(&s, 0x02, &b, 0, 0) && b.n >= 1 && b.n <= 2) { int v = 0; for (size_t i = 0; i < b.n; i++) v = v << 8 | b.p[i]; c->path_len = v; }
        } else if (IS(oid, E_KU)) {
            der b;
            if (der_expect(&val, 0x03, &b, 0, 0) || b.n < 2) FAIL("bad keyUsage");
            c->has_ku = 1; c->ku = 0;
            for (size_t i = 1; i < b.n && i < 3; i++) for (int bit = 0; bit < 8; bit++) if (b.p[i] & (0x80 >> bit)) c->ku |= 1u << ((i - 1) * 8 + (cu32)bit);
        } else if (IS(oid, E_EKU)) {
            der s, o;
            if (der_expect(&val, 0x30, &s, 0, 0)) FAIL("bad extendedKeyUsage");
            c->has_eku = 1;
            while (s.n) { if (der_expect(&s, 0x06, &o, 0, 0)) FAIL("bad extendedKeyUsage"); if (IS(o, K_SERVER) || IS(o, K_ANY)) c->eku_server = 1; }
        } else if (IS(oid, E_SAN)) {
            der s;
            if (der_expect(&val, 0x30, &s, 0, 0)) FAIL("bad subjectAltName");
            c->san = s.p; c->san_len = s.n;
        } else if (IS(oid, E_SKI) || IS(oid, E_AKI) || IS(oid, E_POL) || IS(oid, E_CRLDP) || IS(oid, E_AIA) || IS(oid, E_SCT) ||
                   IS(oid, E_SIA) || IS(oid, E_IAN) || IS(oid, E_FCRL)) {
            /* informational for this client */
        } else if (critical) {
            if (IS(oid, E_NC)) FAIL("a critical nameConstraints extension is not supported");
            FAIL("unsupported critical certificate extension");
        }
    }
    return 0;
}

static int parse_alg(const x509 *unused, der alg, int *type, int *hash)
{
    der oid;
    (void)unused;
    if (der_expect(&alg, 0x06, &oid, 0, 0)) FAIL("bad signature algorithm");
    if (IS(oid, O_SHA256_RSA)) { *type = 1; *hash = CR_SHA256; }
    else if (IS(oid, O_SHA384_RSA)) { *type = 1; *hash = CR_SHA384; }
    else if (IS(oid, O_SHA512_RSA)) { *type = 1; *hash = CR_SHA512; }
    else if (IS(oid, O_ECDSA256)) { *type = 2; *hash = CR_SHA256; }
    else if (IS(oid, O_ECDSA384)) { *type = 2; *hash = CR_SHA384; }
    else if (IS(oid, O_ECDSA512)) { *type = 2; *hash = CR_SHA512; }
    else if (IS(oid, O_SHA1_RSA)) FAIL("certificate signed with SHA-1 (refused)");
    else if (IS(oid, O_PSS)) FAIL("RSA-PSS certificate signatures are not supported");
    else FAIL("unsupported certificate signature algorithm");
    return 0;
}

int x509_parse(x509 *c, const cu8 *buf, size_t len)
{
    der d = { buf, len }, cert, tbs, alg, sigbits, tmp;
    const cu8 *whole; size_t wl;
    for (size_t i = 0; i < sizeof *c; i++) ((cu8 *)c)[i] = 0;
    c->path_len = -1;
    if (der_expect(&d, 0x30, &cert, 0, 0)) FAIL("certificate is not valid DER");
    if (der_expect(&cert, 0x30, &tbs, &whole, &wl)) FAIL("certificate is not valid DER");
    c->tbs = whole; c->tbs_len = wl;
    der outer_alg;
    if (der_expect(&cert, 0x30, &outer_alg, 0, 0)) FAIL("certificate has no signature algorithm");
    if (der_expect(&cert, 0x03, &sigbits, 0, 0) || sigbits.n < 2 || sigbits.p[0] != 0) FAIL("certificate has no signature");
    c->sig = sigbits.p + 1; c->sig_len = sigbits.n - 1;
    if (parse_alg(c, outer_alg, &c->sig_type, &c->sig_hash)) return -1;
    /* TBSCertificate */
    if (!der_expect(&tbs, 0xa0, &tmp, 0, 0)) { /* version present; accept v1..v3 */ }
    if (der_expect(&tbs, 0x02, &tmp, 0, 0)) FAIL("bad certificate serial number");
    if (der_expect(&tbs, 0x30, &alg, 0, 0)) FAIL("bad certificate");
    int t2, h2;
    if (parse_alg(c, alg, &t2, &h2)) return -1;
    if (t2 != c->sig_type || h2 != c->sig_hash) FAIL("certificate signature algorithms do not match");
    if (der_expect(&tbs, 0x30, &tmp, &whole, &wl)) FAIL("bad certificate issuer");
    c->issuer = whole; c->issuer_len = wl;
    der val, t1, t3;
    if (der_expect(&tbs, 0x30, &val, 0, 0)) FAIL("bad certificate validity");
    int tag;
    if (der_next(&val, &tag, &t1, 0, 0) || parse_time(tag, t1, &c->not_before)) FAIL("bad certificate date");
    if (der_next(&val, &tag, &t3, 0, 0) || parse_time(tag, t3, &c->not_after)) FAIL("bad certificate date");
    der subj;
    if (der_expect(&tbs, 0x30, &subj, &whole, &wl)) FAIL("bad certificate subject");
    c->subject = whole; c->subject_len = wl;
    /* commonName: last CN in the subject (used only when there is no subjectAltName) */
    {
        der rdns = subj;
        while (rdns.n) {
            der set;
            if (der_expect(&rdns, 0x31, &set, 0, 0)) break;
            while (set.n) {
                der atv, o, v; int vt;
                if (der_expect(&set, 0x30, &atv, 0, 0)) break;
                if (der_expect(&atv, 0x06, &o, 0, 0)) break;
                if (der_next(&atv, &vt, &v, 0, 0)) break;
                if (IS(o, O_CN)) { c->cn = v.p; c->cn_len = v.n; }
            }
        }
    }
    if (der_expect(&tbs, 0x30, &tmp, &whole, &wl)) FAIL("bad certificate public key");
    c->spki = whole; c->spki_len = wl;
    if (x509_parse_spki(&c->key, c->spki, c->spki_len)) return -1;
    while (tbs.n) {
        int tg; der body;
        if (der_next(&tbs, &tg, &body, 0, 0)) FAIL("bad certificate");
        if (tg == 0xa3) {
            if (parse_extensions(c, body)) return -1;
        }
    }
    return 0;
}

int x509_verify_sig(const x509 *c, const x509_key *k)
{
    cu8 h[64];
    cr_hash_once(c->sig_hash, c->tbs, c->tbs_len, h);
    int hl = cr_hash_len(c->sig_hash);
    if (c->sig_type == 1) {
        if (k->type != 1) FAIL("issuer key type does not match the signature");
        if (cr_rsa_verify(k->n, k->n_len, k->e, k->e_len, c->sig_hash, h, c->sig, c->sig_len)) FAIL("RSA signature is wrong");
        return 0;
    }
    if (k->type != 2) FAIL("issuer key type does not match the signature");
    if (cr_ecdsa_verify(k->curve, k->pub, k->pub_len, h, (size_t)hl, c->sig, c->sig_len)) FAIL("ECDSA signature is wrong");
    return 0;
}

/* ---- host name ---- */
static cu8 lc(cu8 c) { return c >= 'A' && c <= 'Z' ? (cu8)(c + 32) : c; }

static int name_match(const cu8 *pat, size_t pl, const char *host, size_t hl)
{
    if (pl > 2 && pat[0] == '*' && pat[1] == '.') {              /* wildcard: exactly one leftmost label */
        size_t i = 0;
        while (i < hl && host[i] != '.') i++;
        if (i == 0 || i >= hl) return 0;
        pat += 1; pl -= 1;                                         /* ".example.com" */
        host += i; hl -= i;
    }
    if (pl != hl) return 0;
    for (size_t i = 0; i < pl; i++) if (lc(pat[i]) != lc((cu8)host[i])) return 0;
    return 1;
}

static int parse_ipv4(const char *s, cu8 out[4])
{
    int part = 0, val = 0, digits = 0;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); digits++; if (val > 255 || digits > 3) return 0; }
        else if (*s == '.' || *s == 0) {
            if (!digits || part > 3) return 0;
            out[part++] = (cu8)val; val = 0; digits = 0;
            if (!*s) break;
        } else return 0;
    }
    return part == 4;
}

int x509_host_match(const x509 *c, const char *host)
{
    size_t hl = 0;
    while (host[hl]) hl++;
    cu8 ip[4];
    int is_ip = parse_ipv4(host, ip);
    int saw_dns = 0;
    der s = { c->san, c->san_len };
    while (s.n) {
        int tag; der v;
        if (der_next(&s, &tag, &v, 0, 0)) break;
        if (tag == 0x82) { saw_dns = 1; if (!is_ip && name_match(v.p, v.n, host, hl)) return 1; }
        else if (tag == 0x87 && is_ip && v.n == 4 && mem_eq(v.p, ip, 4)) return 1;
    }
    if (!saw_dns && !is_ip && c->cn && name_match(c->cn, c->cn_len, host, hl)) return 1;
    return 0;
}

/* ---- chain ---- */
#define MAXCH 10

static int is_ca_ok(const x509 *c)
{
    if (!c->has_bc || !c->is_ca) return 0;
    if (c->has_ku && !(c->ku & (1u << 5))) return 0;                   /* keyCertSign */
    return 1;
}

static int valid_at(const x509 *c, cu32 now) { return now >= c->not_before && now <= c->not_after; }

int x509_verify_chain(const cu8 *const *certs, const size_t *lens, int n, const char *host, cu32 now, const cu8 *roots, size_t roots_len)
{
    static x509 cc[MAXCH];
    if (n < 1) FAIL("the server sent no certificate");
    if (n > MAXCH) n = MAXCH;
    for (int i = 0; i < n; i++) if (x509_parse(&cc[i], certs[i], lens[i])) return -1;
    if (now < 1700000000u) FAIL("the clock is not set (certificate dates cannot be checked)");
    if (host && host[0] && !x509_host_match(&cc[0], host)) FAIL("the certificate is not valid for this host name");
    if (cc[0].has_eku && !cc[0].eku_server) FAIL("the certificate is not meant for TLS servers");
    if (cc[0].has_ku && cc[0].ku == 0) FAIL("bad certificate key usage");
    int cur = 0, below = 0;                                              /* CA certificates below the next issuer */
    int used[MAXCH] = { 0 };
    used[0] = 1;
    for (int depth = 0; depth <= n; depth++) {
        if (!valid_at(&cc[cur], now)) FAIL(now < cc[cur].not_before ? "a certificate of the chain is not valid yet" : "a certificate of the chain has expired");
        /* a trust anchor that issued cur? */
        for (size_t off = 0; roots && off + 4 <= roots_len;) {
            size_t sl = (size_t)roots[off] << 8 | roots[off + 1], pl = (size_t)roots[off + 2] << 8 | roots[off + 3];
            const cu8 *subj = roots + off + 4, *spki = subj + sl;
            if (off + 4 + sl + pl > roots_len) break;
            off += 4 + sl + pl;
            if (sl != cc[cur].issuer_len || !mem_eq(subj, cc[cur].issuer, sl)) continue;
            x509_key k;
            if (x509_parse_spki(&k, spki, pl)) continue;
            if (x509_verify_sig(&cc[cur], &k) == 0) { err_msg = ""; return 0; }
        }
        /* otherwise the next certificate the server sent */
        int next = -1;
        for (int j = 1; j < n && next < 0; j++) {
            if (used[j] || cc[j].subject_len != cc[cur].issuer_len || !mem_eq(cc[j].subject, cc[cur].issuer, cc[j].subject_len)) continue;
            if (x509_verify_sig(&cc[cur], &cc[j].key) == 0) next = j;
        }
        if (next < 0) FAIL("the certificate chain does not lead to a trusted root (unknown issuer or wrong signature)");
        if (!is_ca_ok(&cc[next])) FAIL("an intermediate certificate is not allowed to sign certificates");
        int between = below + (cur != 0);                                /* CA certificates between the server certificate and next */
        if (cc[next].path_len >= 0 && between > cc[next].path_len) FAIL("certificate path is longer than the issuer allows");
        used[next] = 1;
        below = between;
        cur = next;
    }
    FAIL("certificate chain too long");
}
