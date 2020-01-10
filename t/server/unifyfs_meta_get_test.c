#include <sys/types.h>

#include "metadata_suite.h"
#include "unifyfs_meta.h"
#include "unifyfs_metadata.h"
#include "t/lib/tap.h"

#define TEST_META_GFID_VALUE 0xbeef
#define TEST_META_FID_VALUE  0xfeed
#define TEST_META_FILE "/unifyfs/filename/to/nowhere"

int unifyfs_set_file_attribute_test(void)
{
    int rc;

    /* create dummy file attribute */
    unifyfs_file_attr_t fattr = {0};

    fattr.gfid = TEST_META_GFID_VALUE;
    snprintf(fattr.filename, sizeof(fattr.filename), TEST_META_FILE);
    fflush(NULL);

    rc = unifyfs_set_file_attribute(1, 1, &fattr);
    ok(UNIFYFS_SUCCESS == rc, "Stored file attribute");
    fflush(NULL);
    return 0;
}

int unifyfs_get_file_attribute_test(void)
{
    int rc;
    unifyfs_file_attr_t fattr;

    rc = unifyfs_get_file_attribute(TEST_META_GFID_VALUE, &fattr);
    ok(UNIFYFS_SUCCESS == rc &&
        TEST_META_GFID_VALUE == fattr.gfid &&
        (0 == strcmp(fattr.filename, TEST_META_FILE)),
        "Retrieve file attributes (rc = %d, gfid = 0x%02X)",
        rc, fattr.gfid
    );
    return 0;
}

// this test is not run right now
int unifyfs_get_file_extents_test(void)
{
    int rc, num_values, num_keys;
    int key_lens[16];
    unifyfs_key_t keys[16];
    unifyfs_keyval_t keyval[16];

    rc = unifyfs_get_file_extents(num_keys, &keys, key_lens,
                                  &num_values, &keyval);
    ok(UNIFYFS_SUCCESS == rc,
        "Retrieved file extents (rc = %d)", rc
    );
    return 0;
}
