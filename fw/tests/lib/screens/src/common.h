/* Helpers shared by the screen engine tests. */
#ifndef SCREENS_TEST_COMMON_H_
#define SCREENS_TEST_COMMON_H_

#include <ws/config.h>
#include <ws/screens.h>
#include <ws/sign.h>
#include <ws/vars.h>

extern struct ws_config cfg;
extern struct ws_vars vars;
extern struct ws_cfg_errors errs;

int compile_cfg(const char *json);
int compile_factory(void);
void set_clock(int64_t mono, int hour, int min, int dow);
void dump_frame_diff(const struct ws_frame *got, const uint8_t *want);
int screen_id(const char *id);

#endif
