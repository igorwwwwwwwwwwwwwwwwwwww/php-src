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
#define RP2350_PSRAM_HEAP_OFFSET    (RP2350_PSRAM_GUARD_OFFSET)
#define RP2350_PSRAM_HEAP_SIZE      (2u * 1024u * 1024u)
#define RP2350_MMAP_ARENA_OFFSET    (RP2350_PSRAM_HEAP_OFFSET + RP2350_PSRAM_HEAP_SIZE)
#define RP2350_MMAP_ARENA_SIZE      (RP2350_PSRAM_SIZE - RP2350_MMAP_ARENA_OFFSET)

#endif /* RP2350_PSRAM_LAYOUT_H */
