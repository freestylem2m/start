
#ifndef __DRIVER_UNICORN_H_
#define __DRIVER_UNICORN_H_

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

#define PROCESS_TERMINATION_TIMEOUT 10

typedef enum {
	UNICORN_STATE_IDLE,
	UNICORN_STATE_RUNNING,
	UNICORN_STATE_STOPPING,
	UNICORN_STATE_ERROR
} unicorn_state_t;

typedef enum {
	UNICORN_NONE = 0,
	UNICORN_TERMINATING = 1,
} unicorn_flags_t;

typedef struct unicorn_config_t {
	unicorn_state_t state;
	unicorn_flags_t flags;
	time_t last_tick;
} unicorn_config_t;

extern int unicorn_init(context_t *);
extern int unicorn_shutdown(context_t *);
extern int unicorn_emit(context_t *, event_t event, driver_data_t *event_data);
#endif

