/*
CSE 30341 Spring 2025 Flash Translation Assignment.
Kathryn Cruz and Manuela Roca Zapata
*/

#include "disk.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h> 

struct disk {
    struct flash_drive *fd;

    // Configuration and state
    int disk_blocks;
    int nblocks;
    int npages;
    int ppb;

    // Mappings
    int *disk_to_flash;
    int *flash_to_disk;

    // Block/page metadata
    int *erase_count;
    int *page_state;

    // Performance counters
    int nreads;
    int nwrites;
};

struct disk *disk_create(struct flash_drive *f, int disk_blocks) {
    struct disk *d = malloc(sizeof(*d));

    d->fd            = f;
    d->disk_blocks   = disk_blocks;
    d->npages        = flash_npages(f);
    d->ppb           = flash_npages_per_block(f);
    d->nblocks       = d->npages / d->ppb;
    d->nreads        = 0;
    d->nwrites       = 0;

    d->disk_to_flash = malloc(sizeof(int) * d->disk_blocks);
    d->flash_to_disk = malloc(sizeof(int) * d->npages);
    d->erase_count   = malloc(sizeof(int) * d->nblocks);
    d->page_state    = malloc(sizeof(int) * d->npages);

    for (int i = 0; i < d->disk_blocks; i++) {
        d->disk_to_flash[i] = -1;
    }
    for (int i = 0; i < d->npages; i++) {
        d->flash_to_disk[i] = -1;
        d->page_state[i] = 0; // free
    }
    for (int i = 0; i < d->nblocks; i++) {
        d->erase_count[i] = 0;
    }

    return d;
}

int disk_read(struct disk *d, int block, char *data) {
    d->nreads++;

    int flash_page = d->disk_to_flash[block];
    if (flash_page == -1) {
        fprintf(stderr, "Error: block %d is unmapped.\n", block);
        exit(1);
    }

    flash_read(d->fd, flash_page, data);
    return 0;
}

int find_free_page(struct disk *d) {
    for (int i = 0; i < d->npages; i++) {
        if (d->page_state[i] == 0) {
            return i;
        }
    }
    return -1;
}

void clean(struct disk *d) {
    int block = -1;
    int max_stale = -1;
    int min_erase = __INT_MAX__;
    int base;

    for (int i = 0; i < d->nblocks; i++) {
        base = i * d->ppb;
        int stale_count = 0;

        for (int j = 0; j < d->ppb; j++) {
            if (d->page_state[base + j] == 2) {
                stale_count++;
            }
        }

        if (stale_count > 0 &&
            (stale_count > max_stale ||
             (stale_count == max_stale && d->erase_count[i] < min_erase))) {
            block = i;
            max_stale = stale_count;
            min_erase = d->erase_count[i];
        }
    }

    if (block == -1) {
        fprintf(stderr, "Error: No block with stale pages to clean.\n");
        exit(1);
    }

    printf("CLEANING: Erasing block %d...\n", block);
    base = block * d->ppb;

    struct {
        int disk_block;
        char data[FLASH_PAGE_SIZE];
    } *buffer = malloc(sizeof(*buffer) * d->ppb);

    int val = 0;

    for (int i = 0; i < d->ppb; i++) {
        int page = base + i;
        if (d->page_state[page] == 1) {
            buffer[val].disk_block = d->flash_to_disk[page];
            flash_read(d->fd, page, buffer[val].data);
            d->page_state[page] = 2;
            d->flash_to_disk[page] = -1;
            val++;
        }
    }

    flash_erase(d->fd, block);
    d->erase_count[block]++;

    for (int i = 0; i < d->ppb; i++) {
        int page = base + i;
        d->page_state[page] = 0;
        d->flash_to_disk[page] = -1;
    }

    for (int i = 0; i < val; i++) {
        int new = find_free_page(d);
        if (new == -1) {
            fprintf(stderr, "Error: No free pages after cleaning.\n");
            free(buffer);
            exit(1);
        }

        flash_write(d->fd, new, buffer[i].data);
        int b = buffer[i].disk_block;

        d->disk_to_flash[b] = new;
        d->flash_to_disk[new] = b;
        d->page_state[new] = 1;
    }

    free(buffer);
}

int disk_write(struct disk *d, int disk_block, const char *data) {
    int free_page = find_free_page(d);

    if (free_page == -1) {
        clean(d);
        free_page = find_free_page(d);
        if (free_page == -1) {
            fprintf(stderr, "Error: No free flash pages after cleaning.\n");
            exit(1);
        }
    }

    int old_page = d->disk_to_flash[disk_block];
    if (old_page != -1) {
        d->page_state[old_page] = 2;
        d->flash_to_disk[old_page] = -1;
    }

    flash_write(d->fd, free_page, data);
    d->disk_to_flash[disk_block] = free_page;
    d->flash_to_disk[free_page] = disk_block;
    d->page_state[free_page] = 1;

    d->nwrites++;
    return 0;
}

void disk_report(struct disk *d) {
    printf("\tdisk reads: %d\n", d->nreads);
    printf("\tdisk writes: %d\n", d->nwrites);
}

void disk_close(struct disk *d) {
    free(d->disk_to_flash);
    free(d->flash_to_disk);
    free(d->erase_count);
    free(d->page_state);
    free(d);
}
