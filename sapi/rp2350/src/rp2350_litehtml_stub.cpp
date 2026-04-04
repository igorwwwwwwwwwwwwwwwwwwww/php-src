#include "rp2350_vfs.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" int rp2350_litehtml_stub_probe(void) {
    static const char *kPath = "/phpnet/www.php.net/index.html";
    for (size_t i = 0; i < rp2350_vfs_files_count; i++) {
        if (strcmp(rp2350_vfs_files[i].path, kPath) == 0) {
            return (int) rp2350_vfs_files[i].len;
        }
    }
    return -1;
}
