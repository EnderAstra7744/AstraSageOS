#ifndef _AS_SYS_SOCKET_H
#define _AS_SYS_SOCKET_H
#include <sys/types.h>
typedef unsigned socklen_t;
typedef unsigned short sa_family_t;
#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define EAFNOSUPPORT 97
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_storage { sa_family_t ss_family; char pad[126]; };
int socket(int d, int t, int p);
int connect(int s, const struct sockaddr *a, socklen_t l);
#endif
