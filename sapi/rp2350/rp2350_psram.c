/*
 * PSRAM initialisation for RP2350.
 *
 * The Badger 2350 carries an APS6404L 8 MB QSPI PSRAM on CS1 (GPIO 8).
 *
 * Key constraint: QMI direct mode pauses XIP (code fetches from flash stall).
 * All functions that touch the QMI direct_csr / direct_tx / direct_rx registers
 * must therefore:
 *   a) reside in SRAM (via __no_inline_not_in_flash_func), and
 *   b) run with interrupts disabled (no interrupt handler can touch flash).
 *
 * The QMI has two independent chip-select windows:
 *   CS0 — flash (QSPI pads, managed by boot ROM / pico-sdk)
 *   CS1 — PSRAM via regular GPIO 8, configured as GPIO_FUNC_XIP_CS1
 *
 * We do NOT use flash_do_cmd / flash_exit_xip; those manipulate CS0.
 * We only assert CS1N in direct mode, leaving CS0 alone.
 *
 * After init, PSRAM is mapped read/write at XIP address 0x11000000.
 * Zend allocator integration is done separately via rp2350_mmap.c.
 *
 * Register references:
 *   RP2350 datasheet §4.10 (QMI), §4.9 (XIP), §2.19.6.1 (GPIO functions).
 *   APS6404L datasheet rev 1.5 §8.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/platform/sections.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip.h"
#include "hardware/regs/qmi.h"
#include "hardware/regs/xip.h"

#include "rp2350_psram.h"
#include "rp2350_transport.h"

/* -------------------------------------------------------------------------
 * APS6404L command bytes (issued in single-SPI mode before QPI entry)
 * ------------------------------------------------------------------------- */
#define PSRAM_CMD_RESET_EN      0x66u
#define PSRAM_CMD_RESET         0x99u
#define PSRAM_CMD_ENTER_QPI     0x35u
#define PSRAM_CMD_READ_ID       0x9Fu   /* SPI read-ID: cmd + 3-byte addr + data */
#define PSRAM_CMD_QUAD_READ     0xEBu   /* QPI quad read  */
#define PSRAM_CMD_QUAD_WRITE    0x38u   /* QPI quad write */

#define PSRAM_KGD_PASS          0x5Du   /* known-good-die byte in ID response */

/* QMI direct-mode CLKDIV for safe SPI initialisation (~31 MHz at 125 MHz sys) */
#define PSRAM_INIT_CLKDIV       4u

/* -------------------------------------------------------------------------
 * Direct-mode low-level helpers
 * All must be in SRAM because XIP is stalled during direct mode.
 *
 * Protocol:
 *   direct_begin()  — enable direct mode + assert CS1
 *   direct_byte()   — clock one byte out (and optionally capture RX)
 *   direct_end()    — deassert CS1 + disable direct mode
 *
 * direct_begin/end disable/restore interrupts so that no ISR can attempt
 * a flash fetch while direct mode is active.
 * ------------------------------------------------------------------------- */

/* We store the saved IRQ state in a file-scope variable so direct_end can
 * access it without needing to pass it through every call. */
static uint32_t s_irq_save;

static void __no_inline_not_in_flash_func(direct_begin)(void)
{
    s_irq_save = save_and_disable_interrupts();
    /* Enable direct mode with a safe clock divider; keep EN and CLKDIV only —
     * do NOT touch ASSERT_CS0N (that would interfere with the flash device). */
    qmi_hw->direct_csr =
          ((uint32_t)PSRAM_INIT_CLKDIV << QMI_DIRECT_CSR_CLKDIV_LSB)
        | QMI_DIRECT_CSR_EN_BITS;
    /* Assert CS1 (PSRAM) */
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
}

static void __no_inline_not_in_flash_func(direct_end)(void)
{
    /* Deassert CS1 */
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    /* Disable direct mode — XIP resumes */
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    restore_interrupts(s_irq_save);
}

/* Clock one byte out (OE=1). Returns the simultaneously captured RX byte.
 * If capture_rx is false the RX byte is discarded (NOPUSH). */
static uint8_t __no_inline_not_in_flash_func(direct_byte)(uint8_t tx, bool capture_rx)
{
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_TXFULL_BITS) { /* wait for TX room */ }

    uint32_t word = (uint32_t)tx
                  | (1u << 19);  /* OE — drive MOSI */
    if (!capture_rx) {
        word |= (1u << 20);      /* NOPUSH — discard RX */
    }
    qmi_hw->direct_tx = word;

    /* Wait for the byte to finish clocking */
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { /* wait */ }

    if (!capture_rx) {
        return 0;
    }
    /* Drain the RX FIFO */
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_RXEMPTY_BITS) { /* wait */ }
    return (uint8_t)(qmi_hw->direct_rx & 0xFFu);
}

/* Clock a dummy byte with OE=0 to capture a RX byte */
static uint8_t __no_inline_not_in_flash_func(direct_rx_byte)(void)
{
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_TXFULL_BITS) { /* wait */ }
    /* OE=0, NOPUSH=0 */
    qmi_hw->direct_tx = 0u;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { /* wait */ }
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_RXEMPTY_BITS) { /* wait */ }
    return (uint8_t)(qmi_hw->direct_rx & 0xFFu);
}

/* Run an entire direct-mode command while executing from SRAM.
 * Important: once direct mode is enabled, code fetches from flash may stall.
 */
static void __no_inline_not_in_flash_func(psram_cmd_only)(uint8_t cmd)
{
    direct_begin();
    direct_byte(cmd, false);
    direct_end();
}

static void __no_inline_not_in_flash_func(psram_read_id)(uint8_t *mfid, uint8_t *kgd)
{
    direct_begin();
    direct_byte(PSRAM_CMD_READ_ID, false);
    direct_byte(0x00, false);   /* addr[23:16] */
    direct_byte(0x00, false);   /* addr[15:8]  */
    direct_byte(0x00, false);   /* addr[7:0]   */
    *mfid = direct_rx_byte();
    *kgd  = direct_rx_byte();
    direct_end();
}

static bool s_psram_ready = false;

/* -------------------------------------------------------------------------
 * Public init — called once from main() after stdio_init_all()
 * ------------------------------------------------------------------------- */

bool rp2350_psram_init(uint cs_gpio)
{
    if (s_psram_ready) {
        return true;
    }

    /* 1. Route GPIO to XIP CS1 so QMI can drive it as chip-select */
    gpio_set_function(cs_gpio, GPIO_FUNC_XIP_CS1);
    sleep_us(10);

    /* 2. Reset the PSRAM (single-SPI, CS1) */
    psram_cmd_only(PSRAM_CMD_RESET_EN);
    sleep_us(10);

    psram_cmd_only(PSRAM_CMD_RESET);
    sleep_us(100);   /* tRST ≤ 100 µs */

    /* 3. Read ID — cmd(9F) + 3-byte addr(0) → mfid, kgd, ... */
    uint8_t mfid = 0;
    uint8_t kgd = 0;
    psram_read_id(&mfid, &kgd);

    if (kgd != PSRAM_KGD_PASS) {
        char msg[64];
        int n = snprintf(msg, sizeof(msg),
            "[psram] ID check WARN mfid=0x%02x kgd=0x%02x (want 0x%02x)\r\n",
            (unsigned)mfid, (unsigned)kgd, (unsigned)PSRAM_KGD_PASS);
        if (n > 0) rp2350_platform_write(msg, (size_t)n);
        /* Continue bring-up: some parts/revisions report different KGD bytes. */
    }
    if (kgd == PSRAM_KGD_PASS) {
        char msg[48];
        int n = snprintf(msg, sizeof(msg),
            "[psram] APS6404L mfid=0x%02x OK\r\n", (unsigned)mfid);
        if (n > 0) rp2350_platform_write(msg, (size_t)n);
    }

    /* 4. Enter QPI mode (0x35 → device switches all 4 data lines for cmd/addr/data) */
    psram_cmd_only(PSRAM_CMD_ENTER_QPI);
    sleep_us(1);

    /* 5. Program QMI M1 for QPI operation.
     *
     * Timing: CLKDIV=4 (sys_clk/4 ≈ 31.25 MHz), RXDELAY=1 half-cycle,
     *         COOLDOWN=1 (one idle cycle between CS assertions).
     *
     * Read format (EBh):
     *   prefix=8b quad, addr=24b quad, dummy=6 clocks, data=quad.
     * APS6404L expects a strict 6-cycle read latency here.
     *
     * Write format (38h):
     *   prefix=8b quad, addr=24b quad, no dummy, data=quad.
     */
    qmi_hw->m[1].timing =
          (1u << QMI_M1_TIMING_COOLDOWN_LSB)
        /* APS6404L array accesses can wrap on long continuous bursts.
         * Force CS breaks every 1024 bytes to keep linear XIP semantics. */
        | (QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB)
        | (1u << QMI_M1_TIMING_RXDELAY_LSB)
        | (4u << QMI_M1_TIMING_CLKDIV_LSB);

    qmi_hw->m[1].rfmt =
          (QMI_M1_RFMT_PREFIX_LEN_VALUE_8   << QMI_M1_RFMT_PREFIX_LEN_LSB)
        | (6u                              << QMI_M1_RFMT_DUMMY_LEN_LSB)
        | (QMI_M1_RFMT_DATA_WIDTH_VALUE_Q   << QMI_M1_RFMT_DATA_WIDTH_LSB)
        | (QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M1_RFMT_DUMMY_WIDTH_LSB)
        | (QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q   << QMI_M1_RFMT_ADDR_WIDTH_LSB)
        | (QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB);

    qmi_hw->m[1].rcmd = PSRAM_CMD_QUAD_READ;

    qmi_hw->m[1].wfmt =
          (QMI_M1_WFMT_PREFIX_LEN_VALUE_8   << QMI_M1_WFMT_PREFIX_LEN_LSB)
        | (QMI_M1_WFMT_DATA_WIDTH_VALUE_Q   << QMI_M1_WFMT_DATA_WIDTH_LSB)
        | (QMI_M1_WFMT_ADDR_WIDTH_VALUE_Q   << QMI_M1_WFMT_ADDR_WIDTH_LSB)
        | (QMI_M1_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_PREFIX_WIDTH_LSB);

    qmi_hw->m[1].wcmd = PSRAM_CMD_QUAD_WRITE;

    /* 6. Enable write-through for XIP window 1 */
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);

    /* 7. Sanity check multiple regions of the 8 MB window. */
    {
        static const uint32_t k_offsets[] = {
            0u, (2u * 1024u * 1024u), (4u * 1024u * 1024u), (6u * 1024u * 1024u)
        };
        static const uint32_t k_patterns[] = {
            0xDEADBEEFu, 0xA5A55A5Au, 0x01234567u, 0x89ABCDEFu
        };
        size_t i;
        for (i = 0; i < sizeof(k_offsets) / sizeof(k_offsets[0]); i++) {
            volatile uint32_t *p = (volatile uint32_t *)((uintptr_t)RP2350_PSRAM_BASE + k_offsets[i]);
            *p = k_patterns[i];
            __compiler_memory_barrier();
            if (*p != k_patterns[i]) {
                char msg[96];
                int n = snprintf(msg, sizeof(msg),
                    "[psram] XIP sanity FAIL @+0x%08lx wrote=0x%08lx read=0x%08lx\r\n",
                    (unsigned long)k_offsets[i],
                    (unsigned long)k_patterns[i],
                    (unsigned long)(*p));
                if (n > 0) rp2350_platform_write(msg, (size_t)n);
                return false;
            }
        }
    }
    rp2350_platform_write("[psram] XIP OK\r\n",
                          sizeof("[psram] XIP OK\r\n") - 1);

    s_psram_ready = true;
    return true;
}

bool rp2350_psram_is_ready(void)
{
    return s_psram_ready;
}
