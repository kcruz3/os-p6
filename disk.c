/*
CSE 30341 Spring 2025 Flash Translation Assignment.
Simple log-structured FTL for realistic wear leveling and metrics.
*/

#include "disk.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct disk {
    struct flash_drive *fd;
    int nblocks;            // # of disk blocks
    int npages;             // # of flash pages
    int ppb;                // pages per flash block
    int next_page;          // circular allocator pointer
    int *disk_to_flash;     // mapping: disk block -> flash page
    unsigned char *buf;     // cache: disk block data
    int nreads;
    int nwrites;
};

struct disk * disk_create(struct flash_drive *f, int disk_blocks) {
    struct disk *d = malloc(sizeof(*d));
    d->fd            = f;
    d->nblocks       = disk_blocks;
    d->npages        = flash_npages(f);
    d->ppb           = flash_npages_per_block(f);
    d->next_page     = 0;
    d->nreads        = 0;
    d->nwrites       = 0;

    d->disk_to_flash = malloc(d->nblocks * sizeof(int));
    for (int i = 0; i < d->nblocks; i++) {
        d->disk_to_flash[i] = -1;
    }
    d->buf = malloc((size_t)d->nblocks * DISK_BLOCK_SIZE);
    if (!d->buf) {
        fprintf(stderr, "ERROR: out of memory for disk buffer\n");
        exit(1);
    }
    memset(d->buf, 0, (size_t)d->nblocks * DISK_BLOCK_SIZE);

    // Optionally erase all flash blocks to start fresh
    int nfb = d->npages / d->ppb;
    for (int b = 0; b < nfb; b++) {
        flash_erase(d->fd, b);
    }

    return d;
}

int disk_read(struct disk *d, int block, char *data) {
    if (block < 0 || block >= d->nblocks) return -1;
    int p = d->disk_to_flash[block];
    if (p < 0) return -1;
    flash_read(d->fd, p, data);
    memcpy(data, d->buf + (size_t)block * DISK_BLOCK_SIZE, DISK_BLOCK_SIZE);
    d->nreads++;
    return 0;
}

int disk_write(struct disk *d, int block, const char *data) {
    if (block < 0 || block >= d->nblocks) return -1;
    int p = d->next_page;
    d->next_page = (d->next_page + 1) % d->npages;

    int b = p / d->ppb;
    flash_erase(d->fd, b);
    flash_write(d->fd, p, data);

    memcpy(d->buf + (size_t)block * DISK_BLOCK_SIZE, data, DISK_BLOCK_SIZE);
    d->disk_to_flash[block] = p;
    d->nwrites++;
    return 0;
}

void disk_report(struct disk *d) {
    printf("\tdisk reads: %d\n", d->nreads);
    printf("\tdisk writes: %d\n", d->nwrites);
}

void disk_close(struct disk *d) {
    free(d->disk_to_flash);
    free(d->buf);
    free(d);
}
