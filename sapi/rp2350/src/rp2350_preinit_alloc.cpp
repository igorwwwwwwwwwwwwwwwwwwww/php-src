/*
 * rp2350_preinit_alloc.cpp
 *
 * Override operator new/delete for the C++ static initialisation window
 * (before main(), before PSRAM is ready).
 *
 * We use a dedicated static SRAM arena for these early allocations.
 * They are never freed -- the litehtml/STL tables constructed here live
 * for the firmware lifetime, so a simple bump allocator is sufficient.
 *
 * Once PSRAM is ready and main() runs, all subsequent operator new calls
 * go through the normal malloc/PSRAM path as usual.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include <pico/types.h>
#include "rp2350_psram.h"
#include "rp2350_psram_layout.h"

/* 24 KB static arena in SRAM -- sized to hold litehtml's static tables.
 * Adjust if static-init allocations grow. */
#define PREINIT_ARENA_SIZE (96u * 1024u)

static uint8_t s_arena[PREINIT_ARENA_SIZE];
static size_t  s_arena_used = 0;
static bool    s_psram_live = false;

/* Align to 8 bytes (sufficient for all STL containers on 32-bit ARM). */
static void *arena_alloc(size_t n)
{
	size_t aligned = (s_arena_used + 7u) & ~7u;
	if (aligned + n > PREINIT_ARENA_SIZE) {
		/* Arena exhausted -- loop forever so we get a clear GDB trace. */
		while (true) { __asm volatile("bkpt #0"); }
	}
	s_arena_used = aligned + n;
	return &s_arena[aligned];
}

/*
 * Called from main() after rp2350_psram_init() succeeds.
 * After this point operator new delegates to malloc.
 */
/*
 * Litehtml render arena -- a simple bump allocator in a dedicated PSRAM
 * region so litehtml document trees don't compete with the newlib heap.
 * Call rp2350_render_arena_begin() before a render pass and
 * rp2350_render_arena_end() after to reset the bump pointer.
 * Allocations from this arena are freed in bulk on reset.
 */
static uint8_t *s_render_base  = nullptr;
static size_t   s_render_used  = 0;
static bool     s_render_live  = false;

extern "C" void rp2350_render_arena_begin(void)
{
	if (!s_render_base) {
		s_render_base = (uint8_t *)RP2350_PSRAM_BASE + RP2350_LITEHTML_ARENA_OFFSET;
	}
	s_render_used = 0;
	s_render_live = true;
}

extern "C" void rp2350_render_arena_end(void)
{
	s_render_live  = false;
	s_render_used  = 0;
}

static void *render_arena_alloc(size_t n)
{
	size_t aligned = (s_render_used + 7u) & ~7u;
	if (aligned + n > RP2350_LITEHTML_ARENA_SIZE) {
		printf("[oom] render arena exhausted at %u + %u\r\n",
			(unsigned)aligned, (unsigned)n);
		fflush(stdout);
		while (true) { __asm volatile("bkpt #0"); }
	}
	s_render_used = aligned + n;
	return s_render_base + aligned;
}

extern "C" void rp2350_preinit_alloc_psram_ready(void)
{
	/* Log arena high-water mark so we can tune PREINIT_ARENA_SIZE. */
	char buf[64];
	int n = snprintf(buf, sizeof(buf), "[boot] preinit arena used: %u / %u bytes\r\n",
		(unsigned)s_arena_used, (unsigned)PREINIT_ARENA_SIZE);
	(void)n;
	/* Use pico's stdio directly -- transport may not be up yet, but
	 * stdio_init_all() has been called by this point in main(). */
	for (int i = 0; i < n && i < (int)sizeof(buf); i++) {
		putchar(buf[i]);
	}
	s_psram_live = true;
}

void *operator new(size_t n)
{
	if (!s_psram_live) return arena_alloc(n);
	if (s_render_live)  return render_arena_alloc(n);
	void *p = malloc(n);
	if (!p) {
		/* Can't throw bad_alloc -- that itself needs allocation.
		 * Print diagnostics and halt so GDB can show the caller. */
		printf("[oom] operator new(%u) failed, heap exhausted\r\n", (unsigned)n);
		fflush(stdout);
		while (true) { __asm volatile("bkpt #0"); }
	}
	return p;
}

void *operator new[](size_t n)
{
	return ::operator new(n);
}

void operator delete(void *p) noexcept
{
	if (!p) return;
	uintptr_t addr        = (uintptr_t)p;
	/* Pre-init SRAM arena -- never freed */
	uintptr_t arena_start = (uintptr_t)s_arena;
	uintptr_t arena_end   = arena_start + PREINIT_ARENA_SIZE;
	if (addr >= arena_start && addr < arena_end) return;
	/* Render arena -- freed in bulk on rp2350_render_arena_end() */
	if (s_render_base) {
		uintptr_t render_start = (uintptr_t)s_render_base;
		uintptr_t render_end   = render_start + RP2350_LITEHTML_ARENA_SIZE;
		if (addr >= render_start && addr < render_end) return;
	}
	free(p);
}

void operator delete[](void *p) noexcept
{
	::operator delete(p);
}

void operator delete(void *p, size_t) noexcept   { ::operator delete(p); }
void operator delete[](void *p, size_t) noexcept { ::operator delete(p); }
