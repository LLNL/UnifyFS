#include <config.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unifycr_keyval.h"
#include "unifycr_log.h"
#include "unifycr_runstate.h"

const char* runstate_file = "unifycr-runstate.conf";

int unifycr_read_runstate(unifycr_cfg_t* cfg,
                          const char* runstate_path)
{
    int rc = (int)UNIFYCR_SUCCESS;
    int uid = (int)getuid();
    char runstate_fname[UNIFYCR_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        LOGERR("NULL config");
        return (int)UNIFYCR_ERROR_INVAL;
    }

    if (runstate_path == NULL) {
        if (cfg->runstate_dir == NULL) {
            LOGERR("bad runstate dir config setting");
            return (int)UNIFYCR_ERROR_APPCONFIG;
        }
        snprintf(runstate_fname, sizeof(runstate_fname),
                 "%s/%s.%d", cfg->runstate_dir, runstate_file, uid);
    } else {
        snprintf(runstate_fname, sizeof(runstate_fname),
                 "%s", runstate_path);
    }

    if (unifycr_config_process_ini_file(cfg, runstate_fname) != 0) {
        LOGERR("failed to process runstate file %s", runstate_fname);
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
        LOGERR("NULL config");
        return (int)UNIFYCR_ERROR_INVAL;
    }

    snprintf(runstate_fname, sizeof(runstate_fname),
             "%s/%s.%d", cfg->runstate_dir, runstate_file, uid);

    runstate_fp = fopen(runstate_fname, "w");
    if (runstate_fp == NULL) {
        LOGERR("failed to create file %s", runstate_fname);
        rc = (int)UNIFYCR_ERROR_FILE;
    } else {
        if ((unifycr_log_stream != NULL) &&
            (unifycr_log_level >= LOG_INFO)) {
            unifycr_config_print(cfg, unifycr_log_stream);
        }
        unifycr_config_print_ini(cfg, runstate_fp);
        fclose(runstate_fp);
    }

    return rc;
}

int unifycr_clean_runstate(unifycr_cfg_t* cfg)
{
    int rc = (int)UNIFYCR_SUCCESS;
    int uid = (int)getuid();
    char runstate_fname[UNIFYCR_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        LOGERR("invalid config arg");
        return (int)UNIFYCR_ERROR_INVAL;
    }

    snprintf(runstate_fname, sizeof(runstate_fname),
             "%s/%s.%d", cfg->runstate_dir, runstate_file, uid);

    rc = unlink(runstate_fname);
    if (rc != 0) {
        LOGERR("failed to remove file %s", runstate_fname);
        rc = (int)UNIFYCR_ERROR_FILE;
    }

    return rc;
}
