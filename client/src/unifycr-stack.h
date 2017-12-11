/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of UnifyCR.
 * For details, see https://github.com/llnl/unifycr
 * Please read https://github.com/llnl/unifycr/LICENSE for full license text.
 */

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of UNIFYCR.
 * For details, see https://github.com/hpc/unifycr
 * Please also read this file LICENSE.UNIFYCR
 */

#ifndef UNIFYCR_STACK_H
#define UNIFYCR_STACK_H

/* implements a fixed-size stack which stores integer values in range
 * of 0 to size-1, entire structure stored in an int array of size+2
 *   int size
 *   int last
 *   int entries[size] 
 * last records index within entries that points to item one past
 * the item at the top of the stack
 *
 * used to record which entries in a fixed-size array are free */

#include <stddef.h>

typedef struct {
  int size;
  int last;
} unifycr_stack;

/* returns number of bytes needed to represent stack data structure */
size_t unifycr_stack_bytes(int size);
  
/* intializes stack to record all entries as being free */
void unifycr_stack_init(void* start, int size);

/* pops one entry from stack and returns its value */
int unifycr_stack_pop(void* start);

/* pushes item onto free stack */
void unifycr_stack_push(void* start, int value);

#endif /* UNIFYCR_STACK_H */
