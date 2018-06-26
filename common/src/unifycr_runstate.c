#include "unifycr_runstate.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

int unifycr_read_runstate(unifycr_cfg_t *cfg,
                          const char *runstate_path)
{
    int rc;
    char runstate_fname[UNIFYCR_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        fprintf(stderr, "%s() - invalid args\n", __func__);
        return (int)UNIFYCR_ERROR_INVAL;
    }

    // TO-DO: check PMIx for runstate file path

    if (runstate_path == NULL) {
        if (cfg->runstate_dir == NULL) {
            fprintf(stderr,
                    "%s() - bad runstate dir config setting\n", __func__);
            return (int)UNIFYCR_ERROR_APPCONFIG;
        }
        snprintf(runstate_fname, sizeof(runstate_fname),
                 "%s/unifycr-runstate.conf", cfg->runstate_dir);
    } else {
        snprintf(runstate_fname, sizeof(runstate_fname),
                 "%s", runstate_path);
    }

    rc = unifycr_config_process_ini_file(cfg, runstate_fname);
    if (rc != 0) {
        fprintf(stderr,
                "%s() - failed to process runstate file %s\n",
                __func__, runstate_fname);
        return (int)UNIFYCR_ERROR_APPCONFIG;
    }

    return (int)UNIFYCR_SUCCESS;
}

int unifycr_write_runstate(unifycr_cfg_t *cfg)
{
    FILE *runstate_file = NULL;
    char runstate_fname[UNIFYCR_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        fprintf(stderr, "%s() - invalid config arg\n", __func__);
        return (int)UNIFYCR_ERROR_INVAL;
    }

    // eventually, should include server pid in file name
    /* sprintf(runstate_fname, "%s/unifycr-runstate.conf.%d",
     *         cfg->runstate_dir, (int)getpid());
     */
    snprintf(runstate_fname, sizeof(runstate_fname),
             "%s/unifycr-runstate.conf", cfg->runstate_dir);

    runstate_file = fopen(runstate_fname, "w");
    if (runstate_file == NULL) {
        fprintf(stderr,
                "%s() - failed to create file %s - %s\n",
                __func__, runstate_fname, strerror(errno));
        return (int)UNIFYCR_ERROR_FILE;
    }
    unifycr_config_print_ini(cfg, runstate_file);
    fclose(runstate_file);

    // TO-DO: publish runstate file path to PMIx

    return (int)UNIFYCR_SUCCESS;
}

int unifycr_clean_runstate(unifycr_cfg_t *cfg)
{
    int rc;
    char runstate_fname[UNIFYCR_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        fprintf(stderr, "%s() - invalid config arg\n", __func__);
        return (int)UNIFYCR_ERROR_INVAL;
    }

    // eventually, should include server pid in file name
    /* sprintf(runstate_fname, "%s/unifycr-runstate.conf.%d",
     *         cfg->runstate_dir, (int)getpid());
     */
    snprintf(runstate_fname, sizeof(runstate_fname),
             "%s/unifycr-runstate.conf", cfg->runstate_dir);

    // TO-DO: remove runstate file path to PMIx

    rc = unlink(runstate_fname);
    if (rc != 0) {
        fprintf(stderr,
                "%s() - failed to remove file %s - %s\n",
                __func__, runstate_fname, strerror(errno));
        return (int)UNIFYCR_ERROR_FILE;
    }

    return (int)UNIFYCR_SUCCESS;
}
