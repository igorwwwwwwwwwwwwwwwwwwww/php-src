#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <utime.h>
#include <dirent.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <arpa/inet.h>
#include <fnmatch.h>
#include <sys/select.h>

#include "pico/stdlib.h"
#include "pico/time.h"

char *getenv(const char *name)
{
	(void) name;

	return NULL;
}

char *getcwd(char *buf, size_t size)
{
	(void) size;
	if (buf) {
		buf[0] = '/';
		buf[1] = '\0';
		return buf;
	}
	errno = ENOSYS;
	return NULL;
}

int lstat(const char *path, struct stat *buf)
{
	(void) path;
	(void) buf;
	errno = ENOSYS;
	return -1;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
	(void) path;
	(void) buf;
	(void) bufsiz;
	errno = ENOSYS;
	return -1;
}

int symlink(const char *target, const char *linkpath)
{
	(void) target;
	(void) linkpath;
	errno = ENOSYS;
	return -1;
}

int link(const char *oldpath, const char *newpath)
{
	(void) oldpath;
	(void) newpath;
	errno = ENOSYS;
	return -1;
}

int utime(const char *filename, const struct utimbuf *times)
{
	(void) filename;
	(void) times;
	errno = ENOSYS;
	return -1;
}

int mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;
	errno = ENOSYS;
	return -1;
}

int rmdir(const char *path)
{
	(void) path;
	errno = ENOSYS;
	return -1;
}

int chmod(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;
	errno = ENOSYS;
	return -1;
}

int chdir(const char *path)
{
	(void)path;
	return 0;
}

int chown(const char *path, uid_t owner, gid_t group)
{
	(void) path;
	(void) owner;
	(void) group;
	errno = ENOSYS;
	return -1;
}

mode_t umask(mode_t mask)
{
	(void) mask;
	return 0;
}

int fsync(int fd)
{
	(void) fd;
	errno = ENOSYS;
	return -1;
}

int fdatasync(int fd)
{
	(void) fd;
	errno = ENOSYS;
	return -1;
}

int ftruncate(int fd, off_t length)
{
	(void) fd;
	(void) length;
	errno = ENOSYS;
	return -1;
}

int pclose(FILE *stream)
{
	(void) stream;
	errno = ENOSYS;
	return -1;
}

#if !defined(RP2350_FULL_STANDARD) || !RP2350_FULL_STANDARD
int flock(int fd, int operation)
{
	(void) fd;
	(void) operation;
	errno = ENOSYS;
	return -1;
}
#endif

int scandir(const char *dirp, struct dirent ***namelist,
	int (*filter)(const struct dirent *),
	int (*compar)(const struct dirent **, const struct dirent **))
{
	(void)dirp;
	(void)namelist;
	(void)filter;
	(void)compar;
	errno = ENOSYS;
	return -1;
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
	(void)a;
	(void)b;
	return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
	if (!req) {
		errno = EINVAL;
		return -1;
	}
	if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec > 999999999L) {
		errno = EINVAL;
		return -1;
	}
	sleep_ms((uint32_t)req->tv_sec * 1000u + (uint32_t)(req->tv_nsec / 1000000L));
	if (rem) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	return 0;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	uint64_t us;
	time_t now;

	if (!tp) {
		errno = EINVAL;
		return -1;
	}

	switch (clk_id) {
		case CLOCK_MONOTONIC:
#ifdef CLOCK_MONOTONIC_RAW
		case CLOCK_MONOTONIC_RAW:
#endif
			us = to_us_since_boot(get_absolute_time());
			tp->tv_sec = (time_t)(us / 1000000u);
			tp->tv_nsec = (long)((us % 1000000u) * 1000u);
			return 0;

		case CLOCK_REALTIME:
			now = time(NULL);
			tp->tv_sec = now;
			tp->tv_nsec = 0;
			return 0;

		default:
			errno = EINVAL;
			return -1;
	}
}

int getloadavg(double loadavg[], int nelem)
{
	(void)loadavg;
	(void)nelem;
	errno = ENOSYS;
	return -1;
}

struct servent *getservbyname(const char *name, const char *proto)
{
	(void)name;
	(void)proto;
	return NULL;
}

struct servent *getservbyport(int port, const char *proto)
{
	(void)port;
	(void)proto;
	return NULL;
}

struct protoent *getprotobyname(const char *name)
{
	(void)name;
	return NULL;
}

struct protoent *getprotobynumber(int proto)
{
	(void)proto;
	return NULL;
}

char *strptime(const char *s, const char *format, struct tm *tm)
{
	(void)s;
	(void)format;
	if (tm) {
		memset(tm, 0, sizeof(*tm));
	}
	return NULL;
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
	(void)domain;
	(void)type;
	(void)protocol;
	if (sv) {
		sv[0] = -1;
		sv[1] = -1;
	}
	errno = ENOSYS;
	return -1;
}

int dup(int oldfd)
{
	(void)oldfd;
	errno = ENOSYS;
	return -1;
}

unsigned int sleep(unsigned int seconds)
{
	sleep_ms(seconds * 1000u);
	return 0;
}

int usleep(useconds_t usec)
{
	sleep_us((uint64_t)usec);
	return 0;
}

int lchown(const char *path, uid_t owner, gid_t group)
{
	(void)path;
	(void)owner;
	(void)group;
	errno = ENOSYS;
	return -1;
}

int nice(int inc)
{
	(void)inc;
	errno = ENOSYS;
	return -1;
}

uid_t getuid(void)
{
	return 0;
}

gid_t getgid(void)
{
	return 0;
}

int getgroups(int size, gid_t list[])
{
	(void)size;
	(void)list;
	return 0;
}

int getdtablesize(void)
{
	return 32;
}

pid_t getppid(void)
{
	return 1;
}

int gethostname(char *name, size_t len)
{
	static const char host[] = "rp2350";
	size_t n;

	if (!name || len == 0) {
		errno = EINVAL;
		return -1;
	}
	n = sizeof(host) - 1;
	if (n >= len) {
		n = len - 1;
	}
	memcpy(name, host, n);
	name[n] = '\0';
	return 0;
}

FILE *popen(const char *command, const char *mode)
{
	(void)command;
	(void)mode;
	errno = ENOSYS;
	return NULL;
}

int fnmatch(const char *pattern, const char *string, int flags)
{
	(void)flags;
	return strcmp(pattern, string) == 0 ? 0 : FNM_NOMATCH;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	(void)nfds;
	(void)readfds;
	(void)writefds;
	(void)exceptfds;
	(void)timeout;
	errno = ENOSYS;
	return -1;
}

uint32_t htonl(uint32_t hostlong)
{
	return __builtin_bswap32(hostlong);
}

uint16_t htons(uint16_t hostshort)
{
	return __builtin_bswap16(hostshort);
}

uint32_t ntohl(uint32_t netlong)
{
	return __builtin_bswap32(netlong);
}

uint16_t ntohs(uint16_t netshort)
{
	return __builtin_bswap16(netshort);
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
	(void)af;
	(void)src;
	(void)dst;
	(void)size;
	errno = ENOSYS;
	return NULL;
}

int inet_pton(int af, const char *src, void *dst)
{
	(void)af;
	(void)src;
	(void)dst;
	errno = ENOSYS;
	return -1;
}

struct passwd *getpwnam(const char *name)
{
	(void)name;
	return NULL;
}

struct group *getgrnam(const char *name)
{
	(void)name;
	return NULL;
}

struct passwd *getpwuid(uid_t uid)
{
	(void)uid;
	return NULL;
}
