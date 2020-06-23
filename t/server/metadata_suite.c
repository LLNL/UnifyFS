#include "unifyfs_configurator.h"
#include "unifyfs_metadata_mdhim.h"

#include "t/lib/tap.h"
#include "metadata_suite.h"

int main(int argc, char* argv[])
{
    /* need to initialize enough of the server to use the metadata API */
    unifyfs_cfg_t server_cfg;
    int rc;

    /* get the configuration */
    rc = unifyfs_config_init(&server_cfg, argc, argv);
    if (rc != 0) {
        exit(1);
    }

    rc = meta_init_store(&server_cfg);
    if (rc != 0) {
        LOG(LOG_ERR, "%s",
            unifyfs_rc_enum_description(UNIFYFS_ERROR_META));
        exit(1);
    }

    /*
     * necessary infrastructure is initialized
     * running tests
     */

    plan(NO_PLAN);

    // keep the following two calls in order
    unifyfs_set_file_attribute_test();
    unifyfs_get_file_attribute_test();


    /*
     * shut down infrastructure
     */

    // shutdown the metadata service
    meta_sanitize();

    // finish the testing
    // needs to be last call
    done_testing();

    return EXIT_SUCCESS;
}
