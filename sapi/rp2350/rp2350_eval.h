#ifndef RP2350_EVAL_H
#define RP2350_EVAL_H

#include <stddef.h>

int rp2350_eval_startup(void);
int rp2350_eval_execute(const char *code, size_t len);
int rp2350_eval_execute_file(const char *path);
const char *rp2350_eval_last_error(void);

#endif /* RP2350_EVAL_H */
