#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "rp2350_vfs.h"

#define RP2350_VFS_FD_BASE 64
#define RP2350_VFS_FD_MAX 96

typedef struct {
	bool used;
	const rp2350_vfs_file_t *file;
	size_t pos;
} rp2350_vfs_fd_t;

static rp2350_vfs_fd_t s_fd_table[RP2350_VFS_FD_MAX - RP2350_VFS_FD_BASE];

static const rp2350_vfs_file_t *rp2350_vfs_find_path(const char *path)
{
	size_t i;

	if (!path) {
		return NULL;
	}

	if (strncmp(path, "file://", 7) == 0) {
		path += 7;
	}
	if (path[0] != '/') {
		return NULL;
	}

	for (i = 0; i < rp2350_vfs_files_count; i++) {
		if (strcmp(rp2350_vfs_files[i].path, path) == 0) {
			return &rp2350_vfs_files[i];
		}
	}
	return NULL;
}

static int rp2350_vfs_alloc_fd(const rp2350_vfs_file_t *file)
{
	int i;
	for (i = 0; i < (int)(RP2350_VFS_FD_MAX - RP2350_VFS_FD_BASE); i++) {
		if (!s_fd_table[i].used) {
			s_fd_table[i].used = true;
			s_fd_table[i].file = file;
			s_fd_table[i].pos = 0;
			return RP2350_VFS_FD_BASE + i;
		}
	}
	return -1;
}

static rp2350_vfs_fd_t *rp2350_vfs_fd_from_int(int fd)
{
	int idx = fd - RP2350_VFS_FD_BASE;
	if (idx < 0 || idx >= (int)(RP2350_VFS_FD_MAX - RP2350_VFS_FD_BASE)) {
		return NULL;
	}
	if (!s_fd_table[idx].used) {
		return NULL;
	}
	return &s_fd_table[idx];
}

int _open(const char *path, int oflag, ...)
{
	const rp2350_vfs_file_t *file;
	int access_mode = oflag & O_ACCMODE;

	if (access_mode != O_RDONLY) {
		errno = EROFS;
		return -1;
	}

	file = rp2350_vfs_find_path(path);
	if (!file) {
		errno = ENOENT;
		return -1;
	}

	{
		int fd = rp2350_vfs_alloc_fd(file);
		if (fd < 0) {
			errno = EMFILE;
			return -1;
		}
		return fd;
	}
}

int _close(int fd)
{
	rp2350_vfs_fd_t *entry = rp2350_vfs_fd_from_int(fd);
	if (!entry) {
		errno = EBADF;
		return -1;
	}
	entry->used = false;
	entry->file = NULL;
	entry->pos = 0;
	return 0;
}

_off_t _lseek(int fd, _off_t offset, int whence)
{
	rp2350_vfs_fd_t *entry = rp2350_vfs_fd_from_int(fd);
	size_t base = 0;
	size_t next_pos;

	if (!entry) {
		errno = EBADF;
		return -1;
	}

	switch (whence) {
		case SEEK_SET:
			base = 0;
			break;
		case SEEK_CUR:
			base = entry->pos;
			break;
		case SEEK_END:
			base = entry->file->len;
			break;
		default:
			errno = EINVAL;
			return -1;
	}

	if (offset < 0 && (size_t)(-offset) > base) {
		errno = EINVAL;
		return -1;
	}
	next_pos = (offset < 0) ? (base - (size_t)(-offset)) : (base + (size_t)offset);
	if (next_pos > entry->file->len) {
		errno = EINVAL;
		return -1;
	}
	entry->pos = next_pos;
	return (_off_t)entry->pos;
}

_ssize_t _read(int fd, void *buf, size_t cnt)
{
	rp2350_vfs_fd_t *entry = rp2350_vfs_fd_from_int(fd);
	size_t remaining;
	size_t n;

	if (!entry) {
		errno = EBADF;
		return -1;
	}
	if (entry->pos >= entry->file->len) {
		return 0;
	}

	remaining = entry->file->len - entry->pos;
	n = cnt < remaining ? cnt : remaining;
	memcpy(buf, entry->file->source + entry->pos, n);
	entry->pos += n;
	return (_ssize_t)n;
}

int _fstat(int fd, struct stat *st)
{
	rp2350_vfs_fd_t *entry = rp2350_vfs_fd_from_int(fd);
	if (!entry || !st) {
		errno = EBADF;
		return -1;
	}

	memset(st, 0, sizeof(*st));
	st->st_mode = S_IFREG | 0444;
	st->st_nlink = 1;
	st->st_size = (off_t)entry->file->len;
	st->st_blksize = 512;
	st->st_blocks = (blkcnt_t)((entry->file->len + 511u) / 512u);
	return 0;
}

int _stat(const char *path, struct stat *st)
{
	const rp2350_vfs_file_t *file = rp2350_vfs_find_path(path);
	if (!file || !st) {
		errno = ENOENT;
		return -1;
	}

	memset(st, 0, sizeof(*st));
	st->st_mode = S_IFREG | 0444;
	st->st_nlink = 1;
	st->st_size = (off_t)file->len;
	st->st_blksize = 512;
	st->st_blocks = (blkcnt_t)((file->len + 511u) / 512u);
	return 0;
}
