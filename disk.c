/*
CSE 30341 Spring 2025 Flash Translation Assignment.
Kathryn Cruz && Manuela Roca Zapata
*/

#include "disk.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h> 

struct disk {
    struct flash_drive *fd;

    //feature counters
    int disk_blocks;        //# of disk blocks
    int nblocks;            // # of flash blocks
    int npages;             // # of flash pages
    int ppb;                // pages per flash block
    
    //mapping datasets
    int *disk_to_flash;     //  disk block -> flash page
    int *flash_to_disk;

    //state of blocks and pages
    int *erase_count;
    int *page_state;

    //performance measure
    int nreads;
    int nwrites;
};

struct disk * disk_create(struct flash_drive *f, int disk_blocks) {
    struct disk *d = malloc(sizeof(*d)); //allocating space

    //initializing struct
    d->fd            = f;
    d->disk_blocks   = disk_blocks;
    d->npages        = flash_npages(f);
    d->ppb           = flash_npages_per_block(f);
    d->nblocks       = d->npages / d->ppb;
    d->nreads        = 0;
    d->nwrites       = 0;

    //allocating space for some of the variables
    d->disk_to_flash = malloc(sizeof(int) * d->disk_blocks); 
    d->flash_to_disk = malloc(sizeof(int) * d->npages); 
    d->erase_count   = malloc(sizeof(int) * d->nblocks); 
    d->page_state   = malloc(sizeof(int) * d->npages); 
    
    //initializing the struct variables 
    for (int i = 0; i < d->disk_blocks; i++) {
        d->disk_to_flash[i] = -1;
    }
    for (int i = 0; i < d->npages; i++) {
        d->flash_to_disk[i] = -1; 
        d->page_state[i] = 0; // all are start free_page
    }
    for (int i = 0; i < d->nblocks; i++) {
        d->erase_count[i] = 0; // all start with 0 wear 
    }

    return d;
}

int disk_read(struct disk *d, int block, char *data) {
    d->nreads++;
    
    //MIGHT NEED TO DO THE PRINT STATEMENT HERE

    int flash_page = d->disk_to_flash[block]; //flash page mapping to disk block
    if (flash_page == -1) { //check is block unmapped
        fprintf(stderr, "Error: block %d is unmapped.\n", block);
        exit(1);
    }

    flash_read(d->fd, flash_page, data); //read from flash page
    return 0;
}

//helper functions to find new page and to clean
int find_free_page(struct disk *d) {
    for (int i = 0; i < d->npages; i++) {
        if (d->page_state[i] == 0) {
            return i; //return free_page page
        }
    }
    return -1; //no free page was found
}

void clean(struct disk *d) {

    // initializing variables
    int block = -1;                // block to be cleaned
    int max_stale = -1;            // track max # of stale pages found in a block
    int min_erase = __INT_MAX__;   // break ties using erase count
    int base;

    // loop through blocks to find the one with the most stale pages
    for (int i = 0; i < d->nblocks; i++) {
        base = i * d->ppb;  // base index for the block's first page
        int stale_count = 0;

        // count how many stale pages are in the block
        for (int j = 0; j < d->ppb; j++) {
            if (d->page_state[base + j] == 2) {
                stale_count++;
            }
        }

        // update if this block has more stale pages OR equal but fewer erasures
        if (stale_count > 0 &&
            (stale_count > max_stale ||
            (stale_count == max_stale && d->erase_count[i] < min_erase))) {
            block = i;
            max_stale = stale_count;
            min_erase = d->erase_count[i];
        }
    }

    if (block == -1) { // if no block with stale page exit
        fprintf(stderr, "clean Error: No block with stale pages.\n");
        exit(1);
    }

    printf("CLEANING: Erasing block %d...\n", block);
    base = block * d->ppb;  // block's first page

    // struct to hold valid data before erase
    struct {
        int disk_block;                // disk block that the valid page maps to
        char data[FLASH_PAGE_SIZE];    // valid page content 
    } *buffer = malloc(sizeof(*buffer) * d->ppb);

    int val = 0;  // valid pages in block

    // copy valid pages to buffer and mark them stale
    for (int i = 0; i < d->ppb; i++) {
        int page = base + i; 
        if (d->page_state[page] == 1) {  
            buffer[val].disk_block = d->flash_to_disk[page];  
            flash_read(d->fd, page, buffer[val].data);  // read data into buffer
            d->page_state[page] = 2;  // original page now stale
            d->flash_to_disk[page] = -1;  // invalidate reverse mapping
            val++;  
        }
    }

    // erase entire block and increment wear count
    flash_erase(d->fd, block);
    d->erase_count[block]++;   

    // mark pages in this block as free and unmapped
    for (int i = 0; i < d->ppb; i++) {
        int page = base + i;
        d->page_state[page] = 0;  
        d->flash_to_disk[page] = -1; 
    }

    // reassign valid pages from buffer to new free locations
    for (int i = 0; i < val; i++) {
        int new = find_free_page(d);  // find a new free page

        if (new == -1) {  // if no free page found after cleaning, exit
            fprintf(stderr, "clean Error: No free pages after erase.\n");
            free(buffer);
            exit(1);
        }

        // write valid data back into a new page
        flash_write(d->fd, new, buffer[i].data);
        int b = buffer[i].disk_block;  // get original disk block

        // update FTL mappings
        d->disk_to_flash[b] = new;      // forward mapping
        d->flash_to_disk[new] = b;      // reverse mapping
        d->page_state[new] = 1;         // mark new page as valid
    }

    free(buffer);  // free temporary buffer
}

int disk_write(struct disk *d, int disk_block, const char *data) {

    int free_page = find_free_page(d); //find a free page

    if (free_page == -1) {
        clean(d); //clean 
        free_page = find_free_page(d); //try again to find new page
        if (free_page == -1) {
            fprintf(stderr, "disk_write Error: no free flash pages after cleaning.\n");
            //SHOULD I JUST DO RANDOM IF NO PAGE WAS FOUND?
            exit(1);
        }
    }

    // Invalidate previous page if mapped
    int old_page = d->disk_to_flash[disk_block];
    if (old_page != -1) {
        d->page_state[old_page] = 2;  // mark as stale
        d->flash_to_disk[old_page] = -1; // remove reverse map
    }

    // Write new data and update mappings
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
    printf("IS THIS EVEN CALLED");
    free(d->disk_to_flash);
    free(d->flash_to_disk);
    free(d->erase_count);
    free(d->page_state);
    free(d);
}