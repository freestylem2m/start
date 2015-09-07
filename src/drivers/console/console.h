
#ifndef __DRIVER_CONSOLE_H_
#define __DRIVER_CONSOLE_H_

#include <termios.h>
#include <sys/ioctl.h>

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

typedef enum {
	CONSOLE_STATE_IDLE,
	CONSOLE_STATE_RUNNING,
	CONSOLE_STATE_ERROR
} console_state_t;

#define CHILD_EVENT_WINSIZE 1

typedef struct console_config_t {
	console_state_t state;
	struct termios pty_config;
	struct winsize pty_size;
	int  pty;
} console_config_t;

extern int console_init(context_t *);
extern int console_shutdown(context_t *);
extern ssize_t console_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
