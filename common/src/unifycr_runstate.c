#include <config.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unifycr_runstate.h"
#include "unifycr_pmix.h"

const char* runstate_file = "unifycr-runstate.conf";

int unifycr_read_runstate(unifycr_cfg_t* cfg,
                          const char* runstate_path)
{
    int rc = (int)UNIFYCR_SUCCESS;
    int have_path = 0;
    int uid = (int)getuid();
    char runstate_fname[UNIFYCR_MAX_FILENAME] = {0};
#ifdef HAVE_PMIX_H
    char* pmix_path = NULL;
#endif

    if (cfg == NULL) {
        fprintf(stderr, "%s() - invalid args\n", __func__);
        return (int)UNIFYCR_ERROR_INVAL;
    }

    if (runstate_path == NULL) {

#ifdef HAVE_PMIX_H
        // lookup runstate file path in PMIx
        if (unifycr_pmix_lookup(pmix_key_runstate, 0, &pmix_path) == 0) {
            have_path = 1;
            snprintf(runstate_fname, sizeof(runstate_fname),
                     "%s", pmix_path);
            free(pmix_path);
        }
#endif

        if (!have_path) {
            if (cfg->runstate_dir == NULL) {
                fprintf(stderr,
                        "%s() - bad runstate dir config setting\n", __func__);
                return (int)UNIFYCR_ERROR_APPCONFIG;
            }
            snprintf(runstate_fname, sizeof(runstate_fname),
                     "%s/%s.%d", cfg->runstate_dir, runstate_file, uid);
        }
    } else {
        snprintf(runstate_fname, sizeof(runstate_fname),
                 "%s", runstate_path);
    }

    if (unifycr_config_process_ini_file(cfg, runstate_fname) != 0) {
        fprintf(stderr,
                "%s() - failed to process runstate file %s\n",
                __func__, runstate_fname);
        rc = (int)UNIFYCR_ERROR_APPCONFIG;
    }

    return rc;
}

int unifycr_write_runstate(unifycr_cfg_t* cfg)
{
    int rc = (int)UNIFYCR_SUCCESS;
    int uid = (int)getuid();
    FILE* runstate_fp = NULL;
    char runstate_fname[UNIFYCR_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        fprintf(stderr, "%s() - invalid config arg\n", __func__);
        return (int)UNIFYCR_ERROR_INVAL;
    }

    snprintf(runstate_fname, sizeof(runstate_fname),
             "%s/%s.%d", cfg->runstate_dir, runstate_file, uid);

    runstate_fp = fopen(runstate_fname, "w");
    if (runstate_fp == NULL) {
        fprintf(stderr,
                "%s() - failed to create file %s - %s\n",
                __func__, runstate_fname, strerror(errno));
        rc = (int)UNIFYCR_ERROR_FILE;
    } else {
        unifycr_config_print_ini(cfg, runstate_fp);
        fclose(runstate_fp);

#ifdef HAVE_PMIX_H
        // publish runstate file path to PMIx
        if (unifycr_pmix_publish(pmix_key_runstate, runstate_fname) != 0) {
            fprintf(stderr, "%s() - failed to publish %s k-v pair\n",
                    __func__, pmix_key_runstate);
            rc = (int)UNIFYCR_ERROR_PMIX;
        }
#endif
    }

    return rc;
}

int unifycr_clean_runstate(unifycr_cfg_t* cfg)
{
    int rc = (int)UNIFYCR_SUCCESS;
    char runstate_fname[UNIFYCR_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        fprintf(stderr, "%s() - invalid config arg\n", __func__);
        return (int)UNIFYCR_ERROR_INVAL;
    }

    snprintf(runstate_fname, sizeof(runstate_fname),
             "%s/%s", cfg->runstate_dir, runstate_file);

    rc = unlink(runstate_fname);
    if (rc != 0) {
        fprintf(stderr,
                "%s() - failed to remove file %s - %s\n",
                __func__, runstate_fname, strerror(errno));
        rc = (int)UNIFYCR_ERROR_FILE;
    }

    return rc;
}
