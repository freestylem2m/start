
#ifndef __DRIVER_EXEC_H_
#define __DRIVER_EXEC_H_

#include "ringbuf.h"
#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

#define EXEC_PROCESS_TERMINATION_TIMEOUT 10000

typedef enum {
	EXEC_STATE_IDLE,
	EXEC_STATE_RUNNING,
	EXEC_STATE_STOPPING,
	EXEC_STATE_ERROR
} exec_state_t;

typedef enum {
	EXEC_NONE,
	EXEC_RESPAWN = 1,
	EXEC_RESTARTING = 2,
	EXEC_TERMINATING = 4,
    EXEC_TTY_REQUIRED = 8,
} exec_flags_t;

typedef enum {
	TTY_NONE,
	TTY_RAW=1,
	TTY_ECHO=2,
	TTY_NOECHO=4
} exec_tty_flags_t;

typedef struct exec_config_t {
	exec_state_t state;
	exec_flags_t flags;
	int pid;
	int fd_in;
	int fd_out;
	int tty;
	exec_tty_flags_t tty_flags;
	int restart_delay;
	time_t last_tick;
	time_t pending_action_timestamp;
	u_ringbuf_t output;

	const char *pid_file;
} exec_config_t;

extern int exec_init(context_t *);
extern int exec_shutdown(context_t *);
extern ssize_t exec_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
