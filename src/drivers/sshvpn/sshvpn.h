
#ifndef __DRIVER_SSHVPN_H_
#define __DRIVER_SSHVPN_H_

#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

#define SSHVPN_CONTROL_CHECK_INTERVAL  3000

typedef enum {
	SSHVPN_STATE_IDLE,
	SSHVPN_STATE_RUNNING,
	SSHVPN_STATE_STOPPING,
	SSHVPN_STATE_ERROR
} sshvpn_state_t;

typedef enum {
	SSHVPN_NONE        = 0,
	SSHVPN_TERMINATING = 1,
	SSHVPN_NETWORK_UP  = 2,
} sshvpn_flags_t;

typedef struct sshvpn_config_t {
	sshvpn_state_t state;
	sshvpn_flags_t flags;

	const char *transport_driver;
	const char *network_driver;

    context_t *transport;
    context_t *network;
	
	int        timer_fd;
} sshvpn_config_t;

extern int sshvpn_init(context_t *);
extern int sshvpn_shutdown(context_t *);
extern ssize_t sshvpn_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
