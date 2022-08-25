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

#ifndef SLOTMAP_H
#define SLOTMAP_H

#include <sys/types.h>  // size_t, ssize_t

#ifdef __cplusplus
extern "C" {
#endif

/* slot map, a simple structure that manages a bitmap of used/free slots */
typedef struct slot_map {
    size_t total_slots;
    size_t used_slots;
    ssize_t first_used_slot;
    ssize_t last_used_slot;
} slot_map;

/* The slot usage bitmap immediately follows the structure in memory.
 * The usage bitmap can be thought of as an uint8_t array, where
 * each uint8_t represents 8 slots.
 *   uint8_t use_bitmap[total_slots/8]
 */

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
                       size_t region_sz);

/**
 * Clear the given slot_map. Marks all slots free.
 *
 * @param smap valid slot_map pointer
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int slotmap_clear(slot_map* smap);

/**
 * Reserve consecutive slots in the slot_map.
 *
 * @param smap valid slot_map pointer
 * @param num_slots number of slots to reserve
 *
 * @return starting slot index of reservation, or -1 on error
 */
ssize_t slotmap_reserve(slot_map* smap,
                        size_t num_slots);

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
                    size_t num_slots);

/**
 * Print the slot_map (for debugging).
 *
 * @param smap valid slot_map pointer
 */
void slotmap_print(slot_map* smap);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_BITMAP_H

