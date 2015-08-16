
#ifndef __DRIVER_SIGNAL_H_
#define __DRIVER_SIGNAL_H_

typedef enum {
	SIGNAL_STATE_IDLE,
	SIGNAL_STATE_RUNNING,
	SIGNAL_STATE_STOPPING,
	SIGNAL_STATE_ERROR
} signal_state_t;

typedef enum {
	SIGNAL_NONE,
	SIGNAL_TERMINATING = 1,
} signal_flags_t;

typedef struct signal_config_s {
	signal_state_t state;
	signal_flags_t flags;
} signal_config_t;

extern int signal_init(context_t *);
extern int signal_shutdown(context_t *);
extern int signal_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
