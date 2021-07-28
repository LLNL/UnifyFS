/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include "unifyfs_const.h"
#include "slotmap.h"

#include <assert.h>
#include <stdbool.h> // bool
#include <stdint.h>  // uint8_t
#include <stdio.h>
#include <stdlib.h>  // NULL
#include <string.h>  // memset()


/* Bit-twiddling convenience macros */
#define SLOT_BYTE(slot) ((slot) >> 3)
#define SLOT_BIT(slot) ((slot) & 0x7)
#define BYTE_BIT_TO_SLOT(byte, bit) (((byte) * 8) + (bit))
#define BYTE_VAL_BIT(byte_val, bit) ((byte_val) & (uint8_t)(1 << (bit)))

/* Set given byte-bit in use map */
static inline
void set_usemap_byte_bit(uint8_t* usemap, size_t byte, int bit)
{
    uint8_t byte_val = usemap[byte];
    byte_val |= (uint8_t)(1 << bit);
    usemap[byte] = byte_val;
}

/* Clear given byte-bit in use map */
static inline
void clear_usemap_byte_bit(uint8_t* usemap, size_t byte, int bit)
{
    uint8_t byte_val = usemap[byte];
    uint8_t byte_mask = (uint8_t)0xFF - (uint8_t)(1 << bit);
    byte_val &= byte_mask;
    usemap[byte] = byte_val;
}

/* Check use map for slot used */
static inline
int check_slot(uint8_t* usemap, size_t slot)
{
    size_t byte = SLOT_BYTE(slot);
    int bit = SLOT_BIT(slot);
    uint8_t byte_val = usemap[byte];
    if (BYTE_VAL_BIT(byte_val, bit)) {
        return 1;
    }
    return 0;
}

/* Update use map to use slot */
static inline
void use_slot(uint8_t* usemap, size_t slot)
{
    size_t byte = SLOT_BYTE(slot);
    int bit = SLOT_BIT(slot);
    set_usemap_byte_bit(usemap, byte, bit);
}

/* Update use map to release slot */
static inline
void release_slot(uint8_t* usemap, size_t slot)
{
    size_t byte = SLOT_BYTE(slot);
    int bit = SLOT_BIT(slot);
    clear_usemap_byte_bit(usemap, byte, bit);
}

/* Return bytes necessary to hold use map for given number of slots */
static inline
size_t slot_map_bytes(size_t total_slots)
{
    size_t map_bytes = SLOT_BYTE(total_slots);
    if (SLOT_BIT(total_slots)) {
        map_bytes++;
    }
    return map_bytes;
}

/* Slot usage bitmap immediately follows the structure in memory.
 * The usage bitmap can be thought of as an uint8_t array, where
 * each uint8_t represents 8 slots.
 *   uint8_t use_bitmap[total_slots/8]
 */
static inline
uint8_t* get_use_map(slot_map* smap)
{
    uint8_t* usemap = (uint8_t*)((char*)smap + sizeof(slot_map));
    return usemap;
}

/* Return number of free slots */
static inline
size_t get_free_slots(slot_map* smap)
{
    return smap->total_slots - smap->used_slots;
}

/**
 * Initialize a slot map within the given memory region, and return a pointer
 * to the slot_map structure. Returns NULL if the provided memory region is not
 * large enough to track the requested number of slots.
 *
 * @param num_slots number of slots to track
 * @param region_addr address of the memory region
 * @param region_sz size of the memory region
 *
 * @return valid slot_map pointer, or NULL on error
 */
slot_map* slotmap_init(size_t num_slots,
                       void* region_addr,
                       size_t region_sz)
{
    if (NULL == region_addr) {
        return NULL;
    }

    size_t avail_use_bytes = region_sz - sizeof(slot_map);
    size_t needed_use_bytes = slot_map_bytes(num_slots);
    if (needed_use_bytes > avail_use_bytes) {
        /* not enough space for use map */
        return NULL;
    }

    slot_map* smap = (slot_map*) region_addr;
    smap->total_slots = num_slots;
    slotmap_clear(smap);

    return smap;
}

/**
 * Clear the given slot_map. Marks all slots free.
 *
 * @param smap valid slot_map pointer
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int slotmap_clear(slot_map* smap)
{
    if (NULL == smap) {
        return EINVAL;
    }

    /* set used to zero */
    smap->used_slots = 0;
    smap->first_used_slot = -1;
    smap->last_used_slot = -1;

    /* zero-out use map */
    uint8_t* usemap = get_use_map(smap);
    memset((void*)usemap, 0, slot_map_bytes(smap->total_slots));

    return UNIFYFS_SUCCESS;
}

static inline
size_t find_consecutive_zero_bits(uint8_t byte_val,
                                  size_t num_bits,
                                  int* start_bit)
{
    size_t max_free = 0;
    int free_start = 0;
    for (int bit = 0; bit < 8; bit++) {
        /* starting at current bit, count consecutive zero bits */
        int run_start = bit;
        size_t run_max = 0;
        while (!BYTE_VAL_BIT(byte_val, bit)) {
            run_max++;
            bit++;
            if (bit == 8) {
                break;
            }
        }
        if (run_max > max_free) {
            max_free = run_max;
            free_start = run_start;
        }
    }
    if (NULL != start_bit) {
        if (max_free >= num_bits) {
            *start_bit = free_start;
        } else {
            *start_bit = -1; /* did not find enough bits */
        }
    }
    return max_free;
}

/**
 * Reserve consecutive slots in the slot_map.
 *
 * @param smap valid slot_map pointer
 * @param num_slots number of slots to reserve
 *
 * @return starting slot index of reservation, or -1 on error
 */
ssize_t slotmap_reserve(slot_map* smap,
                        size_t num_slots)
{
    if ((NULL == smap) || (0 == num_slots)) {
        return (ssize_t)-1;
    }

    size_t free = get_free_slots(smap);
    if (free < num_slots) {
        /* not enough free slots available */
        return (ssize_t)-1;
    }

    /* need this many usemap bytes for requested slots */
    size_t slot_bytes = slot_map_bytes(num_slots);

    /* these will be set if we find a spot for the reservation */
    size_t start_slot;
    int found_start = 0;

    /* search for contiguous free slots */
    size_t search_start = 0;
    if ((smap->last_used_slot != -1) && (slot_bytes > 1)) {
        /* skip past likely-used slots */
        search_start = SLOT_BYTE(smap->last_used_slot);
    }
    uint8_t* usemap = get_use_map(smap);
    size_t map_bytes = slot_map_bytes(smap->total_slots);
    for (size_t byte_ndx = search_start; byte_ndx < map_bytes; byte_ndx++) {
        uint8_t byte_val = usemap[byte_ndx];
        if (byte_val == UINT8_MAX) {
            /* current byte is completely occupied */
            continue;
        } else if ((slot_bytes > 1) &&
                   ((byte_ndx + slot_bytes) <= map_bytes)) {
            /* look for slot_bytes consecutive zero bytes */
            size_t run_start = byte_ndx;
            size_t run_count = 0;
            while (0 == usemap[byte_ndx]) {
                run_count++;
                byte_ndx++;
                if (run_count == slot_bytes) {
                    /* success */
                    start_slot = BYTE_BIT_TO_SLOT(run_start, 0);
                    found_start = 1;
                    break;
                }
            }
        } else {
            /* need at most 8 bits, spanning at most two bytes */
            int bit_in_byte;
            size_t free_bits = find_consecutive_zero_bits(byte_val, num_slots,
                                                          &bit_in_byte);
            if (free_bits >= num_slots) {
                /* success, can reserve all slots in this byte of use map */
                assert(bit_in_byte != -1);
                start_slot = BYTE_BIT_TO_SLOT(byte_ndx, bit_in_byte);
                found_start = 1;
            } else if ((byte_ndx + 1) < map_bytes) {
                /* check if free bits are at the end of the byte */
                find_consecutive_zero_bits(byte_val, free_bits, &bit_in_byte);
                assert(bit_in_byte != -1);
                if (bit_in_byte == (8 - free_bits)) {
                    /* free bits are at tail end of byte,
                     * check next byte for remaining needed slots */
                    int bit_in_byte2;
                    size_t have_bits = free_bits;
                    size_t need_bits = num_slots - have_bits;
                    byte_val = usemap[byte_ndx + 1];
                    free_bits = find_consecutive_zero_bits(byte_val, need_bits,
                                                           &bit_in_byte2);
                    if ((free_bits >= need_bits) && (bit_in_byte2 == 0)) {
                        /* success, has enough free bits at start of byte */
                        start_slot = BYTE_BIT_TO_SLOT(byte_ndx, bit_in_byte);
                        found_start = 1;
                    }
                }
            }
        }
        if (found_start) {
            break;
        }
    }

    if (found_start) {
        /* success, reserve bits in consecutive slots */
        size_t end_slot = start_slot + num_slots - 1;
        for (size_t i = start_slot; i <= end_slot; i++) {
            use_slot(usemap, i);
        }
        if ((smap->first_used_slot == -1) ||
            (start_slot < smap->first_used_slot)) {
            smap->first_used_slot = start_slot;
        }
        if ((smap->last_used_slot == -1) ||
            (end_slot > smap->last_used_slot)) {
            smap->last_used_slot = end_slot;
        }
        smap->used_slots += num_slots;
        return (ssize_t)start_slot;
    }

    /* did not find enough consecutive free slots */
    return (ssize_t)-1;
}

/**
 * Release consecutive slots in the slot_map.
 *
 * @param smap valid slot_map pointer
 * @param start_index starting slot index
 * @param num_slots number of slots to release
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int slotmap_release(slot_map* smap,
                    size_t start_index,
                    size_t num_slots)
{
    if (NULL == smap) {
        return EINVAL;
    }

    uint8_t* usemap = get_use_map(smap);

    /* make sure first bit at start slot index is actually set */
    if (!check_slot(usemap, start_index)) {
        return EINVAL;
    }

    /* release the slots */
    size_t end_slot = start_index + num_slots - 1;
    for (size_t i = start_index; i <= end_slot; i++) {
        release_slot(usemap, i);
    }
    smap->used_slots -= num_slots;

    if (smap->used_slots == 0) {
        smap->first_used_slot = -1;
        smap->last_used_slot = -1;
        return UNIFYFS_SUCCESS;
    }

    /* find new first-used slot if necessary */
    if (start_index == smap->first_used_slot) {
        ssize_t first_slot = end_slot + 1;
        while ((first_slot < smap->total_slots) &&
               (!check_slot(usemap, (size_t)first_slot))) {
            first_slot++;
        }
        if (first_slot == smap->total_slots) {
            first_slot = -1;
        }
        smap->last_used_slot = first_slot;
    }

    /* find new last-used slot if necessary */
    if (end_slot == smap->last_used_slot) {
        ssize_t last_slot = start_index - 1;
        while ((last_slot >= 0) &&
               (!check_slot(usemap, (size_t)last_slot))) {
            last_slot--;
        }
        smap->last_used_slot = last_slot;
    }

    return UNIFYFS_SUCCESS;
}

/**
 * Print the slot_map to stderr (for debugging).
 *
 * @param smap valid slot_map pointer
 */
void slotmap_print(slot_map* smap)
{
    if (NULL == smap) {
        return;
    }

    uint8_t* usemap = get_use_map(smap);

    /* the '#' at the beginning of the lines is for compatibility with TAP */
    fprintf(stderr, "# Slot Map:\n");
    fprintf(stderr, "#   total slots - %zu\n", smap->total_slots);
    fprintf(stderr, "#    used slots - %zu\n", smap->used_slots);

    for (size_t i = 0; i < smap->total_slots; i++) {
        if (i % 64 == 0) {
            fprintf(stderr, "\n# %8zu : ", i);
        } else if (i % 8 == 0) {
            fprintf(stderr, "  ");
        }
        int bitval = check_slot(usemap, i);
        fprintf(stderr, "%d", bitval);
    }
    fprintf(stderr, "\n#\n");
}

