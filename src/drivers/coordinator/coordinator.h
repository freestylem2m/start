
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
#define ICMP_SEQUENCE_MASK 0xFFFF

#define COORDINATOR_ICMP_INTERVAL 3*60*1000

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
	COORDINATOR_DNS_ENABLE      = 2048,
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

	/* dns support */
	char    dns_servers[10][32];		// List of DNS servers
	int     dns_max_servers;			// Number of DNS server addresses loaded
	int		dns_current;                // Index of current server

	const char *dns_host;				// Hostname to use for DNS test
	time_t  dns_interval;				// Time between DNS tests
	int     dns_timer;					// Event timer - dns tests
} coordinator_config_t;

extern int coordinator_init(context_t *);
extern int coordinator_shutdown(context_t *);
extern ssize_t coordinator_handler(context_t *, event_t event, driver_data_t *event_data);
extern int check_control_files(context_t *ctx);
extern int coordinator_send_ping(context_t *ctx);
extern int coordinator_check_ping(context_t *ctx, size_t bytes);
extern u_int16_t coordinator_cksum(u_short *addr, size_t len);

struct DNS_HEADER
{
    unsigned short id;				// identification number
 
    unsigned char rd :1;			// recursion desired
    unsigned char tc :1;			// truncated message
    unsigned char aa :1;			// authoritive answer
    unsigned char opcode :4;		// purpose of message
    unsigned char qr :1;			// query/response flag

    unsigned char rcode :4;			// response code
    unsigned char cd :1;			// checking disabled
    unsigned char ad :1;			// authenticated data
    unsigned char z :1;				// its z! reserved
    unsigned char ra :1;			// recursion available
 
    unsigned short q_count;			// number of question entries
    unsigned short ans_count;		// number of answer entries
    unsigned short auth_count;		// number of authority entries
    unsigned short add_count;		// number of resource entries
};
 
struct QUESTION
{
    unsigned short qtype;
    unsigned short qclass;
};
 
extern int coordinator_resolve_host(context_t *,char*);
extern void coordinator_dnsformat(context_t *,unsigned char*,char*);
extern int coordinator_load_dns(context_t *);
#endif
