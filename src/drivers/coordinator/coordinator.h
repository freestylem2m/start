
#ifndef __DRIVER_COORDINATOR_H_
#define __DRIVER_COORDINATOR_H_

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

#define PROCESS_TERMINATION_TIMEOUT 10

typedef enum {
	COORDINATOR_STATE_IDLE,
	COORDINATOR_STATE_RUNNING,
	COORDINATOR_STATE_STOPPING,
	COORDINATOR_STATE_ERROR
} coordinator_state_t;

typedef enum {
	COORDINATOR_NONE,
	COORDINATOR_RESPAWN = 1,
	COORDINATOR_TERMINATING = 2,
} coordinator_flags_t;

typedef struct coordinator_config_t {
	coordinator_state_t state;
	coordinator_flags_t flags;
	time_t last_tick;
	time_t termination_timestamp;
} coordinator_config_t;

extern int coordinator_init(context_t *);
extern int coordinator_shutdown(context_t *);
extern int coordinator_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
