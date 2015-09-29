
#ifndef __DRIVER_ICMP_H_
#define __DRIVER_ICMP_H_

#include <netinet/ip_icmp.h>

#include "driver.h"
#include "icmp_conf.h"

#define FD_READ 0
#define FD_WRITE 1
#define ICMP_DATALEN   64-sizeof(struct icmphdr)
#define ICMP_PAYLOAD   128+sizeof(struct icmphdr)+ICMP_DATALEN

#define ICMP_CONTROL_CHECK_INTERVAL  3000
#define VPN_STARTUP_DELAY  6000
#define ICMP_SEQUENCE_MASK 0xFFFF

typedef enum {
	ICMP_STATE_IDLE,
	ICMP_STATE_RUNNING,
	ICMP_STATE_STOPPING,
	ICMP_STATE_ERROR
} icmp_state_t;

typedef enum {
	ICMP_NONE            = 0,
	ICMP_TERMINATING     = 1,
	ICMP_PING_INPROGRESS = 2,
} icmp_flags_t;

typedef struct icmp_config_t {
	icmp_state_t state;
	icmp_flags_t flags;

	icmp_conf_t	 ping;

	u_char		icmp_out[ ICMP_DATALEN+sizeof(struct icmphdr) ];
	u_char		icmp_in[ ICMP_PAYLOAD ];

	int			icmp_sock;					// ICMP Socket
	int			icmp_retries;				// Current retry count
	int			icmp_timer;					// Event timer - retries
} icmp_config_t;

extern int icmp_init(context_t *);
extern int icmp_shutdown(context_t *);
extern ssize_t icmp_handler(context_t *, event_t event, driver_data_t *event_data);
extern int icmp_send_ping(context_t *ctx);
extern int icmp_check_ping(context_t *ctx, size_t bytes);
extern u_int16_t icmp_cksum(u_short *addr, size_t len);
#endif
