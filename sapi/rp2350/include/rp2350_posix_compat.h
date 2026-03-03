#ifndef RP2350_POSIX_COMPAT_H
#define RP2350_POSIX_COMPAT_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <dirent.h>

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

#ifndef LOG_ERR
#define LOG_ERR 3
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

#endif /* RP2350_POSIX_COMPAT_H */
