
#ifndef __DRIVER_STDIO_H_
#define __DRIVER_STDIO_H_

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

typedef enum {
	STDIO_STATE_IDLE,
	STDIO_STATE_RUNNING,
	STDIO_STATE_ERROR
} stdio_state_t;

typedef enum {
    STDIO_NONE = 0,
	STDIO_TERMINATING = 1,
} stdio_flags_t;

typedef struct stdio_config_t {
	stdio_state_t state;
	stdio_flags_t flags;
	int  fd_in;
	int  fd_out;
} stdio_config_t;

extern int stdio_init(context_t *);
extern int stdio_shutdown(context_t *);
extern ssize_t stdio_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
