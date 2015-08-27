
#ifndef __DRIVER_CONSOLE_H_
#define __DRIVER_CONSOLE_H_

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

typedef enum {
	CONSOLE_STATE_IDLE,
	CONSOLE_STATE_RUNNING,
	CONSOLE_STATE_ERROR
} console_state_t;

typedef enum {
    CONSOLE_NONE = 0,
	CONSOLE_TERMINATING = 1,
} console_flags_t;

typedef struct console_config_t {
	console_state_t state;
	console_flags_t flags;
	time_t last_tick;
	int  fd_in;
	int  fd_out;
} console_config_t;

extern int console_init(context_t *);
extern int console_shutdown(context_t *);
extern ssize_t console_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
