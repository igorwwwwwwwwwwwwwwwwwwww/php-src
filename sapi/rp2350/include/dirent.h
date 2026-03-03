#ifndef RP2350_FAKE_DIRENT_H
#define RP2350_FAKE_DIRENT_H

#include <errno.h>

typedef struct {
	int unused;
} DIR;

struct dirent {
	char d_name[1];
	unsigned char d_type;
};

static inline DIR *opendir(const char *name)
{
	(void) name;
	errno = ENOSYS;
	return (DIR *) 0;
}

static inline int closedir(DIR *dirp)
{
	(void) dirp;
	return 0;
}

static inline struct dirent *readdir(DIR *dirp)
{
	(void) dirp;
	return (struct dirent *) 0;
}

#endif /* RP2350_FAKE_DIRENT_H */
