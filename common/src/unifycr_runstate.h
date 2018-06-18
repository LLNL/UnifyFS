#ifndef _UNIFYCR_RUNSTATE_H_
#define _UNIFYCR_RUNSTATE_H_

#include "unifycr_configurator.h"

int unifycr_read_runstate(unifycr_cfg_t *cfg,
                          const char *runstate_path);

int unifycr_write_runstate(unifycr_cfg_t *cfg);

int unifycr_clean_runstate(unifycr_cfg_t *cfg);

#endif // UNIFYCR_RUNSTATE_H
