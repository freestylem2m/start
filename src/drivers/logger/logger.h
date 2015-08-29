
#ifndef __DRIVER_LOGGER_H_
#define __DRIVER_LOGGER_H_

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

#define LOG_BUFFER_MAX 1024

typedef enum {
	LOGGER_STATE_IDLE,
	LOGGER_STATE_RUNNING,
	LOGGER_STATE_STOPPING,
	LOGGER_STATE_ERROR
} logger_state_t;

typedef enum {
	LOGGER_NONE,
	LOGGER_TERMINATING = 1,
} logger_flags_t;

typedef struct logger_config_t {
	logger_state_t state;
	logger_flags_t flags;
	const char *log_driver;
	context_t *logger;
	int    log_fd;
} logger_config_t;

extern int logger_init(context_t *);
extern int logger_shutdown(context_t *);
extern ssize_t logger_handler(context_t *, event_t event, driver_data_t *event_data);
extern void logger(context_t *ctx, context_t *source, char *fmt, ...);
#endif
