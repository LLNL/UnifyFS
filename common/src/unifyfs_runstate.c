#include <config.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unifyfs_keyval.h"
#include "unifyfs_log.h"
#include "unifyfs_runstate.h"

const char* runstate_file = "unifyfs-runstate.conf";

int unifyfs_read_runstate(unifyfs_cfg_t* cfg,
                          const char* runstate_path)
{
    int rc = (int)UNIFYFS_SUCCESS;
    int uid = (int)getuid();
    char runstate_fname[UNIFYFS_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        LOGERR("NULL config");
        return EINVAL;
    }

    if (runstate_path == NULL) {
        if (cfg->runstate_dir == NULL) {
            LOGERR("bad runstate dir config setting");
            return (int)UNIFYFS_ERROR_BADCONFIG;
        }
        snprintf(runstate_fname, sizeof(runstate_fname),
                 "%s/%s.%d", cfg->runstate_dir, runstate_file, uid);
    } else {
        snprintf(runstate_fname, sizeof(runstate_fname),
                 "%s", runstate_path);
    }

    if (unifyfs_config_process_ini_file(cfg, runstate_fname) != 0) {
        LOGERR("failed to process runstate file %s", runstate_fname);
        rc = (int)UNIFYFS_ERROR_BADCONFIG;
    }

    return rc;
}

int unifyfs_write_runstate(unifyfs_cfg_t* cfg)
{
    int rc = (int)UNIFYFS_SUCCESS;
    int uid = (int)getuid();
    FILE* runstate_fp = NULL;
    char runstate_fname[UNIFYFS_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        LOGERR("NULL config");
        return EINVAL;
    }

    snprintf(runstate_fname, sizeof(runstate_fname),
             "%s/%s.%d", cfg->runstate_dir, runstate_file, uid);

    runstate_fp = fopen(runstate_fname, "w");
    if (runstate_fp == NULL) {
        rc = errno;
        LOGERR("failed to create file %s", runstate_fname);
    } else {
        if ((unifyfs_log_stream != NULL) &&
            (unifyfs_log_level >= LOG_INFO)) {
            unifyfs_config_print(cfg, unifyfs_log_stream);
        }
        unifyfs_config_print_ini(cfg, runstate_fp);
        fclose(runstate_fp);
    }

    return rc;
}

int unifyfs_clean_runstate(unifyfs_cfg_t* cfg)
{
    int rc = (int)UNIFYFS_SUCCESS;
    int uid = (int)getuid();
    char runstate_fname[UNIFYFS_MAX_FILENAME] = {0};

    if (cfg == NULL) {
        LOGERR("invalid config arg");
        return EINVAL;
    }

    snprintf(runstate_fname, sizeof(runstate_fname),
             "%s/%s.%d", cfg->runstate_dir, runstate_file, uid);

    rc = unlink(runstate_fname);
    if (rc != 0) {
        rc = errno;
        LOGERR("failed to remove file %s", runstate_fname);
    }

    return rc;
}
