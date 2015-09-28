
#ifndef __DRIVER_DNS_H_
#define __DRIVER_DNS_H_

#include "driver.h"
#include "netinet/ip_icmp.h"

#define FD_READ 0
#define FD_WRITE 1
#define ICMP_DATALEN   64-sizeof(struct icmphdr)
#define ICMP_PAYLOAD   128+sizeof(struct icmphdr)+ICMP_DATALEN

#define DNS_CONTROL_CHECK_INTERVAL  3000
#define VPN_STARTUP_DELAY  6000
#define ICMP_SEQUENCE_MASK 0xFFFF

#define DNS_DEFAULT_TIMEOUT 3*1000

typedef enum
{
	DNS_STATE_IDLE,
	DNS_STATE_RUNNING,
	DNS_STATE_STOPPING,
	DNS_STATE_ERROR
} dns_state_t;

typedef enum
{
	DNS_NONE = 0,
	DNS_TERMINATING = 1,
} dns_flags_t;

typedef struct dns_config_t
{
	dns_state_t     state;
	dns_flags_t     flags;

	int             sock_fd;
	char            dns_servers[10][32];	// List of DNS servers
	int             dns_max_servers;	// Number of DNS server addresses loaded
	int             dns_current;	// Index of current server
	int             dns_retries;	// Number of retries (== max_servers)

	char           *current_host;

	time_t          dns_timeout;	// Timeout (in ms) for dns request (and retry interval)
	int             dns_timer;	// Event timer - dns tests
} dns_config_t;

extern int      dns_init(context_t *);
extern int      dns_shutdown(context_t *);
extern ssize_t  dns_handler(context_t *, event_t event, driver_data_t * event_data);

struct DNS_HEADER
{
	unsigned short  id;			// identification number

	unsigned char   rd:1;		// recursion desired
	unsigned char   tc:1;		// truncated message
	unsigned char   aa:1;		// authoritive answer
	unsigned char   opcode:4;	// purpose of message
	unsigned char   qr:1;		// query/response flag

	unsigned char   rcode:4;	// response code
	unsigned char   cd:1;		// checking disabled
	unsigned char   ad:1;		// authenticated data
	unsigned char   z:1;		// its z! reserved
	unsigned char   ra:1;		// recursion available

	unsigned short  q_count;	// number of question entries
	unsigned short  ans_count;	// number of answer entries
	unsigned short  auth_count;	// number of authority entries
	unsigned short  add_count;	// number of resource entries
};

struct QUESTION
{
	unsigned short  qtype;
	unsigned short  qclass;
};

#pragma pack(push, 1)
struct R_DATA
{
	unsigned short  type;
	unsigned short  _class;
	unsigned int    ttl;
	unsigned short  data_len;
};
#pragma pack(pop)


extern int      dns_resolve_host(context_t *, const char *);
extern in_addr_t dns_handle_dns_response(context_t * ctx, event_request_t * request);
extern void     dns_dnsformat(context_t *, unsigned char *, const char *);
extern int      dns_load_servers(context_t *, const char *filename);
#endif
