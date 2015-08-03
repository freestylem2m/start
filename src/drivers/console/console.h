
#ifndef __DRIVER_CONSOLE_H_
#define __DRIVER_CONSOLE_H_

#include <sys/time.h>

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

typedef enum {
	CONSOLE_IDLE,
	CONSOLE_RUNNING,
	CONSOLE_ERROR
} console_state_t;

typedef struct console_config_t {
	console_state_t state;
	time_t last_tick;
} console_config_t;

extern int console_init(context_t *);
extern int console_shutdown(context_t *);
extern int console_emit(context_t *, event_t event, void *event_data);
#endif
