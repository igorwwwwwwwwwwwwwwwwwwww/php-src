#ifndef RP2350_POSIX_COMPAT_H
#define RP2350_POSIX_COMPAT_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>

char *getcwd(char *buf, size_t size);
int lstat(const char *path, struct stat *buf);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int symlink(const char *target, const char *linkpath);
int link(const char *oldpath, const char *newpath);
int utime(const char *filename, const struct utimbuf *times);

#endif /* RP2350_POSIX_COMPAT_H */
