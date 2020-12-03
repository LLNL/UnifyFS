#include "slotmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "t/lib/tap.h"
#include "t/lib/testutil.h"

struct reservation {
    size_t slot;
    size_t count;
};

int main(int argc, char** argv)
{
    int rc;

    /* process test args */
    size_t num_slots = 4096;
    if (argc > 1) {
        num_slots = (size_t) atoi(argv[1]);
    }

    size_t num_inserts = 100;
    if (argc > 2) {
        num_inserts = (size_t) atoi(argv[2]);
    }

    int page_multiple = 1;
    if (argc > 3) {
        page_multiple = atoi(argv[3]);
    }

    unsigned int rand_seed = 12345678;
    if (argc > 4) {
        rand_seed = (unsigned int) atoi(argv[4]);
    }
    srand(rand_seed);

    plan(NO_PLAN);

    /* allocate an array of reservations to remove */
    size_t num_removes = num_inserts / 2;
    struct reservation* to_remove = (struct reservation*)
        calloc(num_removes, sizeof(struct reservation));
    if (NULL == to_remove) {
        BAIL_OUT("calloc() for reservation array failed!");
    }

    /* allocate buffer to hold a slot map */
    size_t page_sz = sysconf(_SC_PAGESIZE);
    printf("# NOTE: page size is %zu bytes\n", page_sz);
    size_t buf_sz = page_sz * page_multiple;
    void* buf = malloc(buf_sz);
    if (NULL == buf) {
        BAIL_OUT("ERROR: malloc(%zu) for slot map buffer failed!\n", buf_sz);
    }

    slot_map* smap = slotmap_init(num_slots, buf, buf_sz);
    ok(NULL != smap, "create slot map with %zu slots", num_slots);
    if (NULL == smap) {
        done_testing(); // will exit program
    }

    size_t remove_ndx = 0;
    size_t success_count = 0;
    for (size_t i = 0; i < num_inserts; i++) {
        size_t cnt = (size_t)rand() % 18;
        if (0 == cnt) {
            cnt++;
        }

        ssize_t slot = slotmap_reserve(smap, cnt);
        if (-1 != slot) {
            success_count++;
            printf("# - reserved %2zu slots (start = %zu)\n",
                   cnt, (size_t)slot);

            /* pick some successful reservations to test removal */
            if ((cnt > 4) && (remove_ndx < num_removes)) {
                struct reservation* rsvp = to_remove + remove_ndx;
                rsvp->slot = (size_t)slot;
                rsvp->count = cnt;
                remove_ndx++;
            }
        } else {
            printf("# FAILED to reserve %2zu slots\n", cnt);
        }
    }
    ok(success_count == num_inserts,
       "all slotmap_reserve() calls succeeded (%zu of %zu), %zu slots used",
       success_count, num_inserts, smap->used_slots);

    slotmap_print(smap);

    success_count = 0;
    size_t release_count = 0;
    for (size_t i = 0; i < num_removes; i++) {
        struct reservation* rsvp = to_remove + i;
        if (rsvp->count > 0) {
            release_count++;
            rc = slotmap_release(smap, rsvp->slot, rsvp->count);
            if (0 == rc) {
                success_count++;
                printf("# - released %2zu slots (start = %zu)\n",
                       rsvp->count, rsvp->slot);
            } else {
                printf("# FAILED to release %2zu slots (start = %zu)\n",
                       rsvp->count, rsvp->slot);
            }
        }
    }
    ok(success_count == release_count,
       "all slotmap_release() calls succeeded (%zu of %zu)",
       success_count, release_count);

    slotmap_print(smap);

    rc = slotmap_clear(smap);
    ok(rc == 0, "clear the slotmap");

    done_testing();
}

