#ifndef RP2350_EVAL_H
#define RP2350_EVAL_H

#include <stddef.h>

int rp2350_eval_startup(void);
int rp2350_eval_execute(const char *code, size_t len);

#endif /* RP2350_EVAL_H */
