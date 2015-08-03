
#ifndef __DRIVER_EXEC_H_
#define __DRIVER_EXEC_H_

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

typedef enum {
	EXEC_STATE_IDLE,
	EXEC_STATE_RUNNING,
	EXEC_STATE_STOPPING,
	EXEC_STATE_ERROR
} exec_state_t;

typedef enum {
	EXEC_NONE,
	EXEC_RESPAWN,
} exec_flags_t;

typedef struct exec_config_t {
	exec_state_t state;
	time_t last_tick;
	exec_flags_t flags;
	int pid;
	int fd_in;
	int fd_out;
} exec_config_t;

extern int exec_init(context_t *);
extern int exec_shutdown(context_t *);
extern int exec_emit(context_t *, event_t event, void *event_data);
#endif
