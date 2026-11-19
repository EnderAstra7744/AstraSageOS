/* Host test client for sys/tls.c: tls_host HOST PORT ROOTS.BIN VERIFY(0/1) [CONNECT_ADDR]
 * Connects, does the TLS 1.2 handshake, sends GET / and prints "OK <suite> <bytes>" or "ERR <reason>". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../tls.h"

static int fd;
static int io_send(void *c, const cu8 *d, size_t n) { (void)c; while (n) { ssize_t w = write(fd, d, n); if (w <= 0) return -1; d += w; n -= (size_t)w; } return 0; }
static int io_recv(void *c, cu8 *b, size_t m, cu32 to) { (void)c; struct pollfd p = { fd, POLLIN, 0 }; if (poll(&p, 1, (int)to) <= 0) return 0; ssize_t r = read(fd, b, m); return r > 0 ? (int)r : -1; }
void tls_random(cu8 *o, size_t n) { FILE *f = fopen("/dev/urandom", "rb"); if (fread(o, 1, n, f) != n) abort(); fclose(f); }

static tls_t T;
int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    FILE *rf = fopen(argv[3], "rb");
    static cu8 roots[200000]; size_t rl = fread(roots, 1, sizeof roots, rf); fclose(rf);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)atoi(argv[2]));
    inet_pton(AF_INET, argc > 5 ? argv[5] : "127.0.0.1", &a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) { printf("ERR connect\n"); return 1; }
    tls_io io = { io_send, io_recv, 0 };
    if (tls_connect(&T, &io, argv[1], atoi(argv[4]), (cu32)time(0), roots, rl)) { printf("ERR %s\n", tls_error()); return 1; }
    char req[256];
    snprintf(req, sizeof req, "GET / HTTP/1.0\r\nHost: %s\r\n\r\n", argv[1]);
    tls_write(&T, (const cu8 *)req, strlen(req));
    static cu8 buf[70000]; size_t got = 0;
    for (;;) { int r = tls_read(&T, buf + got, sizeof buf - got - 1, 3000); if (r <= 0) { if (r == -2) printf("READERR %s\n", tls_error()); break; } got += (size_t)r; }
    buf[got] = 0;
    printf("OK %s verified=%d bytes=%zu first=%.15s\n", tls_suite_name(&T), T.verified, got, (char *)buf);
    return 0;
}
