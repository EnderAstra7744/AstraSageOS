#ifndef _AS_ARPA_INET_H
#define _AS_ARPA_INET_H
#include <netinet/in.h>
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_aton(const char *cp, struct in_addr *inp);
#endif
