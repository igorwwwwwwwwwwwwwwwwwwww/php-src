#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <reent.h>

#include "rp2350_psram.h"
#include "rp2350_psram_layout.h"

/* PSRAM heap -- used once PSRAM is initialised. */
static uint8_t *s_heap_base = NULL;
static ptrdiff_t s_heap_brk = 0;

static int rp2350_psram_heap_init(void)
{
	if (!rp2350_psram_is_ready()) {
		return -1;
	}
	if (!s_heap_base) {
		s_heap_base = (uint8_t *)RP2350_PSRAM_BASE + RP2350_PSRAM_HEAP_OFFSET;
		s_heap_brk = 0;
	}
	return 0;
}

void *_sbrk(ptrdiff_t incr)
{
	ptrdiff_t prev;
	ptrdiff_t next;

	if (rp2350_psram_heap_init() != 0) {
		errno = ENOMEM;
		return (void *)-1;
	}

	prev = s_heap_brk;
	next = s_heap_brk + (ptrdiff_t)incr;

	if (next < 0 || (size_t)next > RP2350_PSRAM_HEAP_SIZE) {
		errno = ENOMEM;
		return (void *)-1;
	}

	s_heap_brk = next;
	return (void *)(s_heap_base + prev);
}

void *_sbrk_r(struct _reent *r, ptrdiff_t incr)
{
	void *rc = _sbrk(incr);
	if (rc == (void *)-1 && r) {
		r->_errno = errno;
	}
	return rc;
}
