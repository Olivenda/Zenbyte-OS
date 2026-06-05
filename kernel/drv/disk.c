/* Generic block-device table. The ATA driver registers concrete disks here;
 * the FS layer talks to disks via numeric IDs (0=primary master, 1=primary
 * slave, 2=secondary master, 3=secondary slave). */
#include "disk.h"
#include "string.h"
#include "kio.h"

#define DISK_RETRY_MAX 3

static struct disk disks[DISK_MAX];

void disk_init(void) {
    for (int i = 0; i < DISK_MAX; i++) disks[i].present = 0;
}

struct disk *disk_get(int id) {
    if (id < 0 || id >= DISK_MAX) return NULL;
    return &disks[id];
}

int disk_read(int id, u32 lba, u32 count, void *buf) {
    struct disk *d = disk_get(id);
    if (!d || !d->present || !d->read) return -1;
    for (int attempt = 0; attempt < DISK_RETRY_MAX; attempt++) {
        if (d->read(d, lba, count, buf) == 0) return 0;
        if (attempt < DISK_RETRY_MAX - 1)
            kprintf("disk: retry read %s lba=%u (attempt %d)\n",
                    d->name, lba, attempt + 2);
    }
    return -1;
}

int disk_write(int id, u32 lba, u32 count, const void *buf) {
    struct disk *d = disk_get(id);
    if (!d || !d->present || !d->write) return -1;
    for (int attempt = 0; attempt < DISK_RETRY_MAX; attempt++) {
        if (d->write(d, lba, count, buf) == 0) return 0;
        if (attempt < DISK_RETRY_MAX - 1)
            kprintf("disk: retry write %s lba=%u (attempt %d)\n",
                    d->name, lba, attempt + 2);
    }
    return -1;
}
