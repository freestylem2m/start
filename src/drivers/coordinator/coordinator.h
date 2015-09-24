
#ifndef __DRIVER_COORDINATOR_H_
#define __DRIVER_COORDINATOR_H_

#include "driver.h"
#include "netinet/ip_icmp.h"

#define FD_READ 0
#define FD_WRITE 1
#define ICMP_DATALEN   64-sizeof(struct icmphdr)
#define ICMP_PAYLOAD   128+sizeof(struct icmphdr)+ICMP_DATALEN

#define COORDINATOR_CONTROL_CHECK_INTERVAL  3000
#define VPN_STARTUP_DELAY  6000

typedef enum {
	COORDINATOR_STATE_IDLE,
	COORDINATOR_STATE_RUNNING,
	COORDINATOR_STATE_STOPPING,
	COORDINATOR_STATE_ERROR
} coordinator_state_t;

typedef enum {
	COORDINATOR_NONE            = 0,
	COORDINATOR_TERMINATING     = 1,
	COORDINATOR_NETWORK_UP      = 2,
	COORDINATOR_MODEM_UP        = 4,
	COORDINATOR_VPN_UP          = 8,
	COORDINATOR_MODEM_ONLINE    = 16,
	COORDINATOR_NETWORK_DISABLE = 32,
	COORDINATOR_VPN_DISABLE     = 64,
	COORDINATOR_VPN_STANDALONE  = 128,
	COORDINATOR_VPN_STARTING    = 256,
	COORDINATOR_PING_ENABLE     = 512,
	COORDINATOR_PING_INPROGRESS = 1024,
} coordinator_flags_t;

typedef struct coordinator_config_t {
	coordinator_state_t state;
	coordinator_flags_t flags;

	const char *modem_driver;
	const char *network_driver;
	const char *vpn_driver;

	const char *control_modem;
	const char *control_vpn;

    context_t *unicorn;
    context_t *network;
    context_t *vpn;

	int        timer_fd;
	time_t     vpn_startup_pending;

	/* icmp support */
	int     icmp_ident;
	int     icmp_sock;
	struct sockaddr_in icmp_dest;
	u_char  icmp_out[ICMP_DATALEN+sizeof(struct icmphdr)];
	u_char  icmp_in[ICMP_PAYLOAD];

	int     icmp_max;
	time_t     icmp_ttl;
	int     icmp_interval;
	int     icmp_timer;
} coordinator_config_t;

extern int coordinator_init(context_t *);
extern int coordinator_shutdown(context_t *);
extern ssize_t coordinator_handler(context_t *, event_t event, driver_data_t *event_data);
extern int check_control_files(context_t *ctx);
extern int coordinator_send_ping(context_t *ctx);
extern u_int16_t coordinator_cksum(u_short *addr, size_t len);
#endif
