#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <utime.h>

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

int flock(int fd, int operation)
{
	(void) fd;
	(void) operation;
	errno = ENOSYS;
	return -1;
}
