#include <sys/types.h>

#include "metadata_suite.h"
#include "unifycr_meta.h"
#include "unifycr_metadata.h"
#include "t/lib/tap.h"

#define TEST_META_GFID_VALUE 0xbeef
#define TEST_META_FID_VALUE  0xfeed
#define TEST_META_FILE "/unifycr/filename/to/nowhere"

int unifycr_set_file_attribute_test(void)
{
    int rc;

    /* create dummy file attribute */
    unifycr_file_attr_t fattr = {0};

    fattr.gfid = TEST_META_GFID_VALUE;
    fattr.fid = TEST_META_FID_VALUE;
    snprintf(fattr.filename, sizeof(fattr.filename), TEST_META_FILE);
    fflush(NULL);

    rc = unifycr_set_file_attribute(&fattr);
    ok(UNIFYCR_SUCCESS == rc, "Stored file attribute");
    fflush(NULL);
    return 0;
}

int unifycr_get_file_attribute_test(void)
{
    int rc;
    unifycr_file_attr_t fattr;

    rc = unifycr_get_file_attribute(TEST_META_GFID_VALUE, &fattr);
    ok(UNIFYCR_SUCCESS == rc &&
        TEST_META_GFID_VALUE == fattr.gfid &&
        TEST_META_FID_VALUE == fattr.fid &&
        (0 == strcmp(fattr.filename, TEST_META_FILE)),
        "Retrieve file attributes (rc = %d, gfid = 0x%02X, fid = 0x%02X)",
        rc, fattr.gfid, fattr.fid
    );
    return 0;
}

// this test is not run right now
int unifycr_get_file_extents_test(void)
{
    int rc, num_values, num_keys;
    int key_lens[16];
    unifycr_key_t keys[16];
    unifycr_keyval_t keyval[16];

    rc = unifycr_get_file_extents(num_keys, &keys, key_lens,
                                  &num_values, &keyval);
    ok(UNIFYCR_SUCCESS == rc,
        "Retrieved file extents (rc = %d)", rc
    );
    return 0;
}
