#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"

#include "rp2350_psram.h"
#include "rp2350_psram_layout.h"

#include <sys/mman.h>

static uint8_t *s_mmap_base = NULL;
static size_t s_mmap_used = 0;

static size_t rp2350_align_up(size_t value, size_t alignment)
{
	return (value + alignment - 1u) & ~(alignment - 1u);
}

static bool rp2350_psram_mmap_init(void)
{
	if (!rp2350_psram_is_ready()) {
		if (!rp2350_psram_init(BW_PSRAM_CS)) {
			return false;
		}
	}
	if (s_mmap_base == NULL) {
		s_mmap_base = (uint8_t *)RP2350_PSRAM_BASE + RP2350_MMAP_ARENA_OFFSET;
		s_mmap_used = 0;
	}
	return true;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	size_t alignment;
	size_t start;
	size_t end;
	uint8_t *ptr;

	(void)prot;
	(void)fd;
	(void)offset;

	if (length == 0) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	if (!rp2350_psram_mmap_init()) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	/* Zend MM expects 2MB-aligned chunk mappings for its main heap chunks. */
	alignment = (length >= (1u << 20)) ? (2u << 20) : 4096u;

	if ((flags & MAP_FIXED) != 0) {
		if (addr == NULL) {
			errno = EINVAL;
			return MAP_FAILED;
		}
		ptr = (uint8_t *)addr;
		if (ptr < s_mmap_base || ptr + length > s_mmap_base + RP2350_MMAP_ARENA_SIZE) {
			errno = ENOMEM;
			return MAP_FAILED;
		}
		memset(ptr, 0, length);
		return ptr;
	}

	start = rp2350_align_up((size_t)s_mmap_base + s_mmap_used, alignment);
	end = start + rp2350_align_up(length, 4096u);
	if (end < start || end > (size_t)s_mmap_base + RP2350_MMAP_ARENA_SIZE) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	s_mmap_used = end - (size_t)s_mmap_base;
	memset((void *)start, 0, end - start);
	return (void *)start;
}

int munmap(void *addr, size_t length)
{
	(void)addr;
	(void)length;
	/* Bump allocator: unmap is a no-op for now. */
	return 0;
}

int mprotect(void *addr, size_t len, int prot)
{
	(void)addr;
	(void)len;
	(void)prot;
	return 0;
}
