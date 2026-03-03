#ifndef RP2350_FAKE_SYS_SOCKET_H
#define RP2350_FAKE_SYS_SOCKET_H

#include <sys/types.h>

typedef unsigned int socklen_t;

struct sockaddr {
	unsigned short sa_family;
	char sa_data[14];
};

struct sockaddr_storage {
	unsigned short ss_family;
	char __ss_padding[126];
};

struct msghdr {
	void *msg_name;
	socklen_t msg_namelen;
	void *msg_iov;
	size_t msg_iovlen;
	void *msg_control;
	size_t msg_controllen;
	int msg_flags;
};

struct cmsghdr {
	size_t cmsg_len;
	int cmsg_level;
	int cmsg_type;
};

#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 10

#define SOCK_STREAM 1
#define SOCK_DGRAM 2

#define SOL_SOCKET 1
#define SO_ERROR 4
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9

#define MSG_DONTWAIT 0x40
#define MSG_PEEK 0x02

int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
int shutdown(int sockfd, int how);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

#endif /* RP2350_FAKE_SYS_SOCKET_H */
