#ifndef RP2350_POSIX_COMPAT_H
#define RP2350_POSIX_COMPAT_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <dirent.h>
#include <time.h>

/* Force-disable host feature probes that are not valid for this RP2350 build. */
#ifdef HAVE_STRPTIME
#undef HAVE_STRPTIME
#endif
#ifdef HAVE_ARPA_NAMESER_H
#undef HAVE_ARPA_NAMESER_H
#endif
#ifdef HAVE_RESOLV_H
#undef HAVE_RESOLV_H
#endif
#ifdef HAVE_DNS_H
#undef HAVE_DNS_H
#endif
#ifdef HAVE_DNS_SEARCH
#undef HAVE_DNS_SEARCH
#endif
#ifdef HAVE_DNS_SEARCH_FUNC
#undef HAVE_DNS_SEARCH_FUNC
#endif

char *getcwd(char *buf, size_t size);
int lstat(const char *path, struct stat *buf);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int symlink(const char *target, const char *linkpath);
int link(const char *oldpath, const char *newpath);
int utime(const char *filename, const struct utimbuf *times);
int scandir(const char *dirp, struct dirent ***namelist,
	int (*filter)(const struct dirent *),
	int (*compar)(const struct dirent **, const struct dirent **));
int alphasort(const struct dirent **a, const struct dirent **b);
int nanosleep(const struct timespec *req, struct timespec *rem);
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int getloadavg(double loadavg[], int nelem);
char *strptime(const char *s, const char *format, struct tm *tm);

struct servent {
	char *s_name;
	char **s_aliases;
	int s_port;
	char *s_proto;
};

struct protoent {
	char *p_name;
	char **p_aliases;
	int p_proto;
};

struct servent *getservbyname(const char *name, const char *proto);
struct servent *getservbyport(int port, const char *proto);
struct protoent *getprotobyname(const char *name);
struct protoent *getprotobynumber(int proto);
int socketpair(int domain, int type, int protocol, int sv[2]);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
pid_t getppid(void);
int gethostname(char *name, size_t len);

#ifndef LOG_ERR
#define LOG_ERR 3
#endif
#ifndef LOG_EMERG
#define LOG_EMERG 0
#endif
#ifndef LOG_ALERT
#define LOG_ALERT 1
#endif
#ifndef LOG_CRIT
#define LOG_CRIT 2
#endif
#ifndef LOG_WARNING
#define LOG_WARNING 4
#endif
#ifndef LOG_NOTICE
#define LOG_NOTICE 5
#endif
#ifndef LOG_INFO
#define LOG_INFO 6
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG 7
#endif
#ifndef LOG_KERN
#define LOG_KERN (0 << 3)
#endif
#ifndef LOG_USER
#define LOG_USER (1 << 3)
#endif
#ifndef LOG_MAIL
#define LOG_MAIL (2 << 3)
#endif
#ifndef LOG_DAEMON
#define LOG_DAEMON (3 << 3)
#endif
#ifndef LOG_AUTH
#define LOG_AUTH (4 << 3)
#endif
#ifndef LOG_SYSLOG
#define LOG_SYSLOG (5 << 3)
#endif
#ifndef LOG_LPR
#define LOG_LPR (6 << 3)
#endif
#ifndef LOG_LOCAL0
#define LOG_LOCAL0 (16 << 3)
#endif
#ifndef LOG_LOCAL1
#define LOG_LOCAL1 (17 << 3)
#endif
#ifndef LOG_LOCAL2
#define LOG_LOCAL2 (18 << 3)
#endif
#ifndef LOG_LOCAL3
#define LOG_LOCAL3 (19 << 3)
#endif
#ifndef LOG_LOCAL4
#define LOG_LOCAL4 (20 << 3)
#endif
#ifndef LOG_LOCAL5
#define LOG_LOCAL5 (21 << 3)
#endif
#ifndef LOG_LOCAL6
#define LOG_LOCAL6 (22 << 3)
#endif
#ifndef LOG_LOCAL7
#define LOG_LOCAL7 (23 << 3)
#endif
#ifndef LOG_PID
#define LOG_PID 0x01
#endif
#ifndef LOG_CONS
#define LOG_CONS 0x02
#endif
#ifndef LOG_ODELAY
#define LOG_ODELAY 0x04
#endif
#ifndef LOG_NDELAY
#define LOG_NDELAY 0x08
#endif
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#endif /* RP2350_POSIX_COMPAT_H */
