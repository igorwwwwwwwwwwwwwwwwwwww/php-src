#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "hardware/regs/m33.h"
#include "hardware/structs/scb.h"
#include "pico/platform.h"

#include "rp2350_transport.h"

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} rp2350_fault_frame_t;

typedef struct {
    uint32_t fault_id;
    uint32_t exc_lr;
    uint32_t stacked_pc;
    uint32_t stacked_lr;
    uint32_t stacked_xpsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t shcsr;
} rp2350_fault_snapshot_t;

volatile rp2350_fault_snapshot_t rp2350_last_fault;

static void rp2350_fault_write(const char *tag, const rp2350_fault_frame_t *f, uint32_t exc_lr)
{
    char buf[224];
    int n = snprintf(
        buf,
        sizeof(buf),
        "\r\n[fault:%s] exc_lr=%08" PRIx32 " pc=%08" PRIx32 " lr=%08" PRIx32 " sp_r0=%08" PRIx32 "\r\n",
        tag,
        exc_lr,
        f ? f->pc : 0u,
        f ? f->lr : 0u,
        f ? f->r0 : 0u
    );
    if (n > 0) {
        rp2350_platform_write(buf, (size_t)n);
    }

    n = snprintf(
        buf,
        sizeof(buf),
        "[fault:%s] cfsr=%08" PRIx32 " hfsr=%08" PRIx32 " mmfar=%08" PRIx32 " bfar=%08" PRIx32 " shcsr=%08" PRIx32 "\r\n",
        tag,
        scb_hw->cfsr,
        scb_hw->hfsr,
        scb_hw->mmfar,
        scb_hw->bfar,
        scb_hw->shcsr
    );
    if (n > 0) {
        rp2350_platform_write(buf, (size_t)n);
    }
    rp2350_platform_flush();
}

static const char *rp2350_fault_tag_from_id(uint32_t fault_id)
{
	switch (fault_id) {
	case 1: return "hard";
	case 2: return "bus";
	case 3: return "mem";
	case 4: return "usage";
	default: return "unknown";
	}
}

static void __attribute__((noinline)) rp2350_fault_common(uint32_t fault_id, uint32_t *sp, uint32_t exc_lr)
{
	rp2350_fault_frame_t *frame = (rp2350_fault_frame_t *)sp;
	rp2350_last_fault.fault_id = fault_id;
	rp2350_last_fault.exc_lr = exc_lr;
	rp2350_last_fault.stacked_pc = frame ? frame->pc : 0u;
	rp2350_last_fault.stacked_lr = frame ? frame->lr : 0u;
	rp2350_last_fault.stacked_xpsr = frame ? frame->xpsr : 0u;
	rp2350_last_fault.cfsr = scb_hw->cfsr;
	rp2350_last_fault.hfsr = scb_hw->hfsr;
	rp2350_last_fault.mmfar = scb_hw->mmfar;
	rp2350_last_fault.bfar = scb_hw->bfar;
	rp2350_last_fault.shcsr = scb_hw->shcsr;
	rp2350_fault_write(rp2350_fault_tag_from_id(fault_id), frame, exc_lr);
	while (true) {
		__breakpoint();
	}
}

void __attribute__((naked)) isr_hardfault(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "movs r2, #1\n"
        "b _rp2350_fault_dispatch\n"
    );
}

void __attribute__((naked)) isr_busfault(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "movs r2, #2\n"
        "b _rp2350_fault_dispatch\n"
    );
}

void __attribute__((naked)) isr_memmanage(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "movs r2, #3\n"
        "b _rp2350_fault_dispatch\n"
    );
}

void __attribute__((naked)) isr_usagefault(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "movs r2, #4\n"
        "b _rp2350_fault_dispatch\n"
    );
}

void __attribute__((naked)) _rp2350_fault_dispatch(void)
{
    __asm volatile(
        "mov r3, r2\n" /* fault_id */
        "mov r2, r1\n" /* exc_lr */
        "mov r1, r0\n" /* sp */
        "mov r0, r3\n" /* fault_id */
        "b _rp2350_fault_dispatch_c\n"
    );
}

void __attribute__((noinline)) _rp2350_fault_dispatch_c(uint32_t fault_id, uint32_t *sp, uint32_t exc_lr)
{
    rp2350_fault_common(fault_id, sp, exc_lr);
}
