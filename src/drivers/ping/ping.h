
#ifndef __DRIVER_PING_H_
#define __DRIVER_PING_H_

#include "driver.h"
#include "netinet/ip_icmp.h"

#define FD_READ 0
#define FD_WRITE 1
#define ICMP_DATALEN   64-sizeof(struct icmphdr)
#define ICMP_PAYLOAD   128+sizeof(struct icmphdr)+ICMP_DATALEN

#define PING_CONTROL_CHECK_INTERVAL  3000
#define VPN_STARTUP_DELAY  6000
#define ICMP_SEQUENCE_MASK 0xFFFF

#define PING_ICMP_INTERVAL 3*60*1000

typedef enum {
	PING_STATE_IDLE,
	PING_STATE_RUNNING,
	PING_STATE_STOPPING,
	PING_STATE_ERROR
} ping_state_t;

typedef enum {
	PING_NONE            = 0,
	PING_TERMINATING     = 1,
	PING_PING_INPROGRESS = 2,
} ping_flags_t;

typedef struct ping_config_t {
	ping_state_t state;
	ping_flags_t flags;

	int     icmp_ident;					// Ping identifier (getpid())
	int     icmp_sock;					// RAW Socket
	struct sockaddr_in icmp_dest;		// Remote host
	u_char  icmp_out[ICMP_DATALEN+sizeof(struct icmphdr)];
	u_char  icmp_in[ICMP_PAYLOAD];

	time_t  icmp_interval;				// Time between ping tests
	time_t  icmp_ttl;					// Time between retries
	int     icmp_max;					// Maximum retries
	int     icmp_retries;				// Current retry count
	u_int16_t icmp_count;				// Unique ID for each ping packet

	int     icmp_timer;					// Event timer - ping tests
	int     icmp_retry_timer;			// Event timer - retries
} ping_config_t;

extern int ping_init(context_t *);
extern int ping_shutdown(context_t *);
extern ssize_t ping_handler(context_t *, event_t event, driver_data_t *event_data);
extern int ping_send_ping(context_t *ctx);
extern int ping_check_ping(context_t *ctx, size_t bytes);
extern u_int16_t ping_cksum(u_short *addr, size_t len);
#endif
