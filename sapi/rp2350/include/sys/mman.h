#ifndef RP2350_FAKE_SYS_MMAN_H
#define RP2350_FAKE_SYS_MMAN_H

#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_PRIVATE 0x02
#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FIXED 0x10
#define MAP_FAILED ((void *) -1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t len, int prot);

#endif /* RP2350_FAKE_SYS_MMAN_H */
