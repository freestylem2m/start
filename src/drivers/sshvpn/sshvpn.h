
#ifndef __DRIVER_SSHVPN_H_
#define __DRIVER_SSHVPN_H_


#include <netinet/in.h>

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
	SSHVPN_TRANSPORT_UP = 4,
	SSHVPN_IF_UP = 8,
	SSHVPN_ROUTE_UP = 16,
} sshvpn_flags_t;

typedef struct sshvpn_config_t {
	sshvpn_state_t state;
	sshvpn_flags_t flags;

	const char *transport_driver;
	const char *network_driver;

    context_t *transport;
    context_t *network;

	const char *resolver_file;
	char *resolver_data;
	ssize_t resolver_data_size;

	int        sock_fd;
	const char *interface;
	in_addr_t  interface_addr;
	const char **route_info;
	int        timer_fd;
} sshvpn_config_t;

typedef enum {
	RESOLVER_NONE,
	RESOLVER_BACKUP,
	RESOLVER_RESTORE,
	RESOLVER_RELEASE,
} sshvpn_resolver_action_t;

extern int sshvpn_manage_resolver(context_t *ctx, sshvpn_resolver_action_t action);
extern int sshvpn_init(context_t *);
extern int sshvpn_shutdown(context_t *);
extern ssize_t sshvpn_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
