#ifndef RP2350_PSRAM_LAYOUT_H
#define RP2350_PSRAM_LAYOUT_H

#include "rp2350_psram.h"

/*
 * PSRAM layout:
 * - small guard at start
 * - newlib heap window for malloc/free/realloc via _sbrk
 * - Zend mmap arena for large/chunk allocations
 */
#define RP2350_PSRAM_GUARD_OFFSET   (64u * 1024u)

/* newlib malloc/free heap -- used by C++ STL, litehtml, PHP small allocs */
#define RP2350_PSRAM_HEAP_OFFSET    (RP2350_PSRAM_GUARD_OFFSET)
#define RP2350_PSRAM_HEAP_SIZE      (2u * 1024u * 1024u)

/* Zend mmap arena -- must fit at least one 2MB-aligned chunk + padding.
 * Keep at least 4MB here; Zend allocates 2MB chunks aligned to 2MB. */
#define RP2350_MMAP_ARENA_OFFSET    (RP2350_PSRAM_HEAP_OFFSET + RP2350_PSRAM_HEAP_SIZE)
#define RP2350_MMAP_ARENA_SIZE      (RP2350_PSRAM_SIZE - RP2350_MMAP_ARENA_OFFSET)

/* litehtml render scratch arena -- separate from newlib heap so litehtml
 * document trees don't evict PHP allocations during a render pass.
 * Placed at the top of PSRAM, carved out of the mmap region end. */
#define RP2350_LITEHTML_ARENA_SIZE  (3u * 1024u * 1024u)
#define RP2350_LITEHTML_ARENA_OFFSET (RP2350_PSRAM_SIZE - RP2350_LITEHTML_ARENA_SIZE)

#endif /* RP2350_PSRAM_LAYOUT_H */
