#ifndef _UNIFYFS_RUNSTATE_H_
#define _UNIFYFS_RUNSTATE_H_

#include "unifyfs_configurator.h"

#ifdef __cplusplus
extern "C" {
#endif

int unifyfs_read_runstate(unifyfs_cfg_t* cfg,
                          const char* runstate_path);

int unifyfs_write_runstate(unifyfs_cfg_t* cfg);

int unifyfs_clean_runstate(unifyfs_cfg_t* cfg);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_RUNSTATE_H
