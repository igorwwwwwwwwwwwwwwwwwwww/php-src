#ifndef RP2350_PSRAM_H
#define RP2350_PSRAM_H

#include <stddef.h>
#include <stdbool.h>

/*
 * PSRAM base address in the RP2350 XIP address space.
 * QMI window 1 (CS1) maps to 0x11000000.
 */
#define RP2350_PSRAM_BASE   ((void *)0x11000000u)
#define RP2350_PSRAM_SIZE   (8u * 1024u * 1024u)   /* 8 MB on Badger 2350 */

/*
 * Initialise PSRAM and install it as the Zend MM backing store.
 *
 * Must be called after stdio_init_all() and before rp2350_eval_execute().
 * Returns true on success, false if the chip does not respond or Zend MM
 * registration fails.
 */
bool rp2350_psram_init(uint cs_gpio);
bool rp2350_psram_install_zend_mm(void);

#endif /* RP2350_PSRAM_H */
