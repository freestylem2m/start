
#ifndef __DRIVER_COORDINATOR_H_
#define __DRIVER_COORDINATOR_H_

#include "driver.h"
#include "netinet/ip_icmp.h"

#include "dns_conf.h"
#include "icmp_conf.h"

#define COORDINATOR_CONTROL_CHECK_INTERVAL  10000
#define VPN_STARTUP_DELAY  6000

#define COORDINATOR_TEST_INTERVAL 3*60*1000

typedef enum
{
	COORDINATOR_STATE_IDLE,
	COORDINATOR_STATE_RUNNING,
	COORDINATOR_STATE_STOPPING,
	COORDINATOR_STATE_ERROR
} coordinator_state_t;

typedef enum
{
	COORDINATOR_NONE = 0,
	COORDINATOR_TERMINATING = 1,
	COORDINATOR_NETWORK_UP = 2,
	COORDINATOR_MODEM_UP = 4,
	COORDINATOR_VPN_UP = 8,
	COORDINATOR_MODEM_ONLINE = 16,
	COORDINATOR_NETWORK_DISABLE = 32,
	COORDINATOR_NETWORK_SHUTDOWN = 64,
	COORDINATOR_VPN_DISABLE = 128,
	COORDINATOR_VPN_STANDALONE = 256,
	COORDINATOR_VPN_STARTING = 512,
	COORDINATOR_TEST_INPROGRESS = 1024,
} coordinator_flags_t;

typedef struct coordinator_config_t
{
	coordinator_state_t state;
	coordinator_flags_t flags;

	const char     *modem_driver;
	const char     *network_driver;
	const char     *vpn_driver;
	const char     *test_driver;

	const char     *control_modem;
	const char     *control_vpn;

	context_t      *s_unicorn;
	context_t      *s_network;
	context_t      *s_vpn;
	context_t      *s_test;

	int             timer_fd;
	time_t          vpn_startup_pending;

	time_t			test_interval;      // Network check interval
	int             test_timer;			// Event timer - icmp tests
} coordinator_config_t;

extern int      coordinator_init(context_t *);
extern int      coordinator_shutdown(context_t *);
extern ssize_t  coordinator_handler(context_t *, event_t event, driver_data_t * event_data);
extern int      check_control_files(context_t * ctx);
#if 0
extern int      coordinator_send_ping(context_t * ctx);
extern int      coordinator_check_ping(context_t * ctx, size_t bytes);
extern u_int16_t coordinator_cksum(u_short * addr, size_t len);
#endif
#endif
