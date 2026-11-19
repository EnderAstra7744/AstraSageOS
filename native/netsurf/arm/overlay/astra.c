/*
 * http / https fetcher for AstraSageOS: the kernel does the network (TCP + TLS); this fetcher asks it for a whole
 * page through the native program interface (api->http_fetch) and hands the answer to NetSurf in one go.
 * GET and POST (urlencoded and multipart forms), cookies, basic authentication, redirects by NetSurf itself.
 * The whole answer is received before it is handed to the core (no streaming).
 */
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdarg.h>
#include <libwapcaplet/libwapcaplet.h>
#include "netsurf/inttypes.h"
#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "utils/log.h"
#include "utils/utils.h"
#include "utils/ring.h"
#include "netsurf/fetch.h"

#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/fetchers/astra.h"
#include "content/urldb.h"
#include "utils/useragent.h"
#include "utils/nsoption.h"

#include "as_api.h"
extern const as_api *__api;

#define ASTRA_FETCH_CAP_MAX (128u << 20)

struct astra_ctx {
	struct astra_ctx *r_next, *r_prev;
	struct fetch *fetchh;
	nsurl *url;
	char *xhdr;              /* extra request headers, "Name: value\r\n" lines */
	char *post_urlenc;
	struct fetch_multipart_data *multipart;   /* owned by the core until the fetch is freed */
	bool only_2xx;
	bool aborted;
	bool locked;
};

static struct astra_ctx *ring = NULL;
static unsigned char *fetch_buf = NULL;

static bool send_msg(struct astra_ctx *c, const fetch_msg *m)
{
	c->locked = true;
	fetch_send_callback(m, c->fetchh);
	c->locked = false;
	return c->aborted;
}

static bool send_error(struct astra_ctx *c, const char *text)
{
	fetch_msg m;
	m.type = FETCH_ERROR;
	m.data.error = text;
	return send_msg(c, &m);
}

static const char *strcasestr_(const char *h, const char *n)
{
	size_t nl = strlen(n);
	for (; *h; h++) if (strncasecmp(h, n, nl) == 0) return h;
	return NULL;
}

static bool astra_initialise(lwc_string *scheme) { (void)scheme; return true; }
static bool astra_acceptable(const nsurl *url) { (void)url; return __api != NULL && __api->http_fetch != NULL; }

static void *astra_setup(struct fetch *fetchh, nsurl *url, bool only_2xx, bool downgrade_tls, const char *post_urlenc,
			 const struct fetch_multipart_data *post_multipart, const char **headers)
{
	struct astra_ctx *c = calloc(1, sizeof *c);
	size_t need = 1;
	int i;
	(void)downgrade_tls;
	if (c == NULL) return NULL;
	c->url = nsurl_ref(url);
	c->fetchh = fetchh;
	c->only_2xx = only_2xx;
	if (post_urlenc != NULL) c->post_urlenc = strdup(post_urlenc);
	c->multipart = (struct fetch_multipart_data *)post_multipart;
	for (i = 0; headers != NULL && headers[i] != NULL; i++) need += strlen(headers[i]) + 2;
	c->xhdr = malloc(need);
	if (c->xhdr != NULL) {
		size_t n = 0;
		for (i = 0; headers != NULL && headers[i] != NULL; i++) {
			const char *h = headers[i];
			/* the kernel adds Host / Connection / Content-Length itself; we cannot decode compressed answers */
			if (strncasecmp(h, "Host:", 5) == 0 || strncasecmp(h, "Connection:", 11) == 0 ||
			    strncasecmp(h, "Accept-Encoding:", 16) == 0 || strncasecmp(h, "Content-Length:", 15) == 0) continue;
			n += (size_t)sprintf(c->xhdr + n, "%s\r\n", h);
		}
		c->xhdr[n] = 0;
	}
	RING_INSERT(ring, c);
	return c;
}

static bool astra_start(void *fetch) { (void)fetch; return true; }
static void astra_abort(void *fetch) { ((struct astra_ctx *)fetch)->aborted = true; }
static void astra_free(void *fetch)
{
	struct astra_ctx *c = fetch;
	if (c->url != NULL) nsurl_unref(c->url);
	free(c->xhdr);
	free(c->post_urlenc);
	free(c);
}

/* one header line ("Name: value", no line end) to the core, unless it describes an encoding we already removed */
static bool send_header(struct astra_ctx *c, const char *line, size_t len)
{
	fetch_msg m;
	char tmp[1024];
	if (len == 0 || len >= sizeof tmp) return false;
	if (len > 18 && strncasecmp(line, "transfer-encoding:", 18) == 0) return false;
	if (len > 17 && strncasecmp(line, "content-encoding:", 17) == 0) return false;
	if (len > 15 && strncasecmp(line, "content-length:", 15) == 0) return false;
	memcpy(tmp, line, len);
	tmp[len] = 0;
	m.type = FETCH_HEADER;
	m.data.header_or_data.buf = (const uint8_t *)tmp;
	m.data.header_or_data.len = len;
	return send_msg(c, &m);
}

static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* appends "Name: value\r\n" to a growing string */
static bool sadd(char **buf, size_t *len, size_t *cap, const char *p, size_t n)
{
	if (*len + n + 1 > *cap) {
		size_t nc = (*cap ? *cap : 256) * 2;
		char *nb;
		while (nc < *len + n + 1) nc *= 2;
		nb = realloc(*buf, nc);
		if (nb == NULL) return false;
		*buf = nb; *cap = nc;
	}
	memcpy(*buf + *len, p, n);
	*len += n;
	(*buf)[*len] = 0;
	return true;
}
#define SADD(b, l, c, str) sadd(b, l, c, str, strlen(str))

/* multipart/form-data body for a form (files are read from disk) */
static char *build_multipart(struct fetch_multipart_data *m, const char *boundary, size_t *outlen)
{
	char *b = NULL;
	size_t len = 0, cap = 0;
	char tmp[512];
	for (; m != NULL; m = m->next) {
		snprintf(tmp, sizeof tmp, "--%s\r\nContent-Disposition: form-data; name=\"%s\"", boundary, m->name ? m->name : "");
		SADD(&b, &len, &cap, tmp);
		if (m->file && m->rawfile != NULL) {
			FILE *f = fopen(m->rawfile, "rb");
			const char *base = strrchr(m->rawfile, '/');
			base = base ? base + 1 : m->rawfile;
			snprintf(tmp, sizeof tmp, "; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n", m->value && *m->value ? m->value : base);
			SADD(&b, &len, &cap, tmp);
			if (f != NULL) {
				char chunk[4096];
				size_t n;
				while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) sadd(&b, &len, &cap, chunk, n);
				fclose(f);
			}
		} else {
			SADD(&b, &len, &cap, "\r\n\r\n");
			if (m->value) SADD(&b, &len, &cap, m->value);
		}
		SADD(&b, &len, &cap, "\r\n");
	}
	snprintf(tmp, sizeof tmp, "--%s--\r\n", boundary);
	SADD(&b, &len, &cap, tmp);
	*outlen = len;
	return b;
}

static void do_fetch(struct astra_ctx *c)
{
	unsigned hdr = 0, boff = 0, blen = 0;
	int status = 0;
	const char *url = nsurl_access(c->url);
	char location[1024];
	bool have_location = false;
	char realm[256] = "";
	char *xh = NULL, *body = NULL;
	size_t xl = 0, xc = 0, bl = 0;
	const char *method = "GET";
	bool ok;
	static unsigned cap = 0;

	if (fetch_buf == NULL) {
		cap = __api->heap_size / 3;
		if (cap > ASTRA_FETCH_CAP_MAX) cap = ASTRA_FETCH_CAP_MAX;
		if (cap < (1u << 20)) cap = 1u << 20;
		fetch_buf = malloc(cap);
		while (fetch_buf == NULL && cap > (1u << 20)) { cap /= 2; fetch_buf = malloc(cap); }
	}
	if (fetch_buf == NULL) { send_error(c, "AstraSageOS: out of memory"); return; }

	/* request headers: ours first (user agent, language, cookies, login), then the ones the core asked for */
	{
		const char *ua = user_agent_string();
		const char *lang = nsoption_charp(accept_language);
		const char *cookie = urldb_get_cookie(c->url, true);
		const char *auth = urldb_get_auth_details(c->url, NULL);
		char tmp[1024];
		snprintf(tmp, sizeof tmp, "User-Agent: %s\r\n", ua ? ua : "NetSurf");
		SADD(&xh, &xl, &xc, tmp);
		if (c->xhdr == NULL || strncasecmp(c->xhdr, "Accept:", 7) != 0) {
			if (c->xhdr == NULL || strcasestr_(c->xhdr, "\r\nAccept:") == NULL) SADD(&xh, &xl, &xc, "Accept: */*\r\n");
		}
		if (lang != NULL && *lang) { snprintf(tmp, sizeof tmp, "Accept-Language: %s, *;q=0.1\r\n", lang); SADD(&xh, &xl, &xc, tmp); }
		if (cookie != NULL && *cookie) {
			SADD(&xh, &xl, &xc, "Cookie: "); SADD(&xh, &xl, &xc, cookie); SADD(&xh, &xl, &xc, "\r\n");
		}
		if (auth != NULL && *auth) {                                    /* HTTP basic */
			size_t n = strlen(auth), i;
			char *enc = malloc(n * 4 / 3 + 8), *o = enc;
			if (enc != NULL) {
				for (i = 0; i + 2 < n + 2; i += 3) {
					unsigned v = ((unsigned char)auth[i] << 16) | (i + 1 < n ? (unsigned char)auth[i + 1] << 8 : 0) | (i + 2 < n ? (unsigned char)auth[i + 2] : 0);
					*o++ = b64tab[(v >> 18) & 63]; *o++ = b64tab[(v >> 12) & 63];
					*o++ = i + 1 < n ? b64tab[(v >> 6) & 63] : '=';
					*o++ = i + 2 < n ? b64tab[v & 63] : '=';
				}
				*o = 0;
				SADD(&xh, &xl, &xc, "Authorization: Basic "); SADD(&xh, &xl, &xc, enc); SADD(&xh, &xl, &xc, "\r\n");
				free(enc);
			}
		}
		if (c->xhdr != NULL) SADD(&xh, &xl, &xc, c->xhdr);
	}
	if (c->post_urlenc != NULL) {
		method = "POST";
		body = c->post_urlenc; bl = strlen(body);
		SADD(&xh, &xl, &xc, "Content-Type: application/x-www-form-urlencoded\r\n");
	} else if (c->multipart != NULL) {
		static const char boundary[] = "----AstraSageOSFormBoundary7d9c2e41";
		char tmp[160];
		method = "POST";
		body = build_multipart(c->multipart, boundary, &bl);
		snprintf(tmp, sizeof tmp, "Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
		SADD(&xh, &xl, &xc, tmp);
	}

	if (__api->version >= 6 && __api->http_request != NULL)
		ok = __api->http_request(method, url, xh, body, (unsigned)bl, fetch_buf, cap, &hdr, &boff, &blen, &status) != 0;
	else
		ok = __api->http_fetch(url, fetch_buf, cap, &hdr, &boff, &blen, &status) != 0;
	free(xh);
	if (c->multipart != NULL && c->post_urlenc == NULL) free(body);
	if (!ok || status <= 0) {
		send_error(c, "Could not get the page (no network, the address does not exist, or the page is larger than the receive buffer)");
		return;
	}

	fetch_set_http_code(c->fetchh, status);

	/* headers: skip the status line, one callback per line */
	{
		const char *p = (const char *)fetch_buf, *e = p + hdr;
		bool first = true;
		location[0] = 0;
		while (p < e) {
			const char *nl = memchr(p, '\n', (size_t)(e - p));
			size_t ll = nl ? (size_t)(nl - p) : (size_t)(e - p);
			const char *next = nl ? nl + 1 : e;
			while (ll && (p[ll - 1] == '\r')) ll--;
			if (first) first = false;
			else if (ll) {
				if (ll > 17 && strncasecmp(p, "www-authenticate:", 17) == 0) {
					const char *r = p, *e = p + ll;
					realm[0] = 0;
					while (r + 6 < e && strncasecmp(r, "realm=", 6) != 0) r++;
					if (r + 6 < e) {
						size_t k = 0;
						r += 6;
						if (*r == '"') r++;
						while (r < e && *r != '"' && k < sizeof realm - 1) realm[k++] = *r++;
						realm[k] = 0;
					}
				}
				if (ll > 11 && strncasecmp(p, "set-cookie:", 11) == 0) {
					char cv[2048];
					const char *v = p + 11; size_t vl = ll - 11;
					while (vl && (*v == ' ' || *v == '\t')) { v++; vl--; }
					if (vl < sizeof cv) { memcpy(cv, v, vl); cv[vl] = 0; fetch_set_cookie(c->fetchh, cv); }
					p = next; continue;
				}
				if (ll > 9 && strncasecmp(p, "location:", 9) == 0) {
					const char *v = p + 9; size_t vl = ll - 9;
					while (vl && (*v == ' ' || *v == '\t')) { v++; vl--; }
					if (vl < sizeof location) { memcpy(location, v, vl); location[vl] = 0; have_location = true; }
				}
				if (send_header(c, p, ll)) return;
			}
			p = next;
		}
	}

	if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && have_location) {
		nsurl *joined = NULL;
		fetch_msg m;
		if (nsurl_join(c->url, location, &joined) == NSERROR_OK) {
			m.type = FETCH_REDIRECT;
			m.data.redirect = nsurl_access(joined);
			send_msg(c, &m);
			nsurl_unref(joined);
			return;
		}
	}

	if (status == 401) {
		fetch_msg m;
		m.type = FETCH_AUTH;
		m.data.auth.realm = realm;
		send_msg(c, &m);
		return;
	}
	if (c->only_2xx && (status < 200 || status > 299)) {
		send_error(c, "The server answered with an error status");
		return;
	}

	{
		fetch_msg m;
		size_t off = 0;
		/* the core wants a "HTTP status" through a header named by the fetch machinery: it reads the code from the
		 * first FETCH_HEADER of the form "HTTP/1.1 NNN" ... so send it first, before the other headers */
		(void)off;
		m.type = FETCH_DATA;
		off = 0;
		while (off < blen) {
			size_t n = blen - off;
			if (n > 16384) n = 16384;
			m.data.header_or_data.buf = fetch_buf + boff + off;
			m.data.header_or_data.len = n;
			if (send_msg(c, &m)) return;
			off += n;
		}
		m.type = FETCH_FINISHED;
		send_msg(c, &m);
	}
}

static void astra_poll(lwc_string *scheme)
{
	struct astra_ctx *c, *save_ring = NULL;
	(void)scheme;
	while (ring != NULL) {
		c = ring;
		RING_REMOVE(ring, c);
		if (c->locked) { RING_INSERT(save_ring, c); continue; }
		if (!c->aborted) do_fetch(c);
		fetch_remove_from_queues(c->fetchh);
		fetch_free(c->fetchh);
	}
	ring = save_ring;
}

static void astra_finalise(lwc_string *scheme) { (void)scheme; }

nserror fetch_astra_register(void)
{
	static const char *const schemes[] = { "http", "https" };
	size_t i;
	const struct fetcher_operation_table fetcher_ops = {
		.initialise = astra_initialise,
		.acceptable = astra_acceptable,
		.setup = astra_setup,
		.start = astra_start,
		.abort = astra_abort,
		.free = astra_free,
		.poll = astra_poll,
		.finalise = astra_finalise
	};
	for (i = 0; i < 2; i++) {
		lwc_string *scheme;
		nserror e;
		if (lwc_intern_string(schemes[i], strlen(schemes[i]), &scheme) != lwc_error_ok) return NSERROR_NOMEM;
		e = fetcher_add(scheme, &fetcher_ops);
		if (e != NSERROR_OK) return e;
	}
	return NSERROR_OK;
}
