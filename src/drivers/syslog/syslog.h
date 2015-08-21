
#ifndef __DRIVER_SYSLOG_H_
#define __DRIVER_SYSLOG_H_

#include <sys/time.h>

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

typedef struct syslog_config_t {
	int  facility;
	int  prio;
	const char *ident;
	int  options;
} syslog_config_t;

extern int syslog_init(context_t *);
extern int syslog_shutdown(context_t *);
extern ssize_t syslog_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
