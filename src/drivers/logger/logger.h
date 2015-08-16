
#ifndef __DRIVER_LOGGER_H_
#define __DRIVER_LOGGER_H_

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

#define PROCESS_TERMINATION_TIMEOUT 10

typedef enum {
	LOGGER_STATE_IDLE,
	LOGGER_STATE_RUNNING,
	LOGGER_STATE_STOPPING,
	LOGGER_STATE_ERROR
} logger_state_t;

typedef enum {
	LOGGER_NONE,
	LOGGER_RESPAWN = 1,
	LOGGER_TERMINATING = 2,
} logger_flags_t;

typedef struct logger_config_t {
	logger_state_t state;
	logger_flags_t flags;
	int fd_in;
	int fd_out;
	time_t last_tick;
	context_t *logger;
} logger_config_t;

extern int logger_init(context_t *);
extern int logger_shutdown(context_t *);
extern int logger_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
