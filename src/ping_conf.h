
#ifndef __PING_H_
#define __PING_H_

#include <time.h>
#include "events.h"

#define ICMP_DATALEN   64-sizeof(struct icmphdr)
#define ICMP_PAYLOAD   128+sizeof(struct icmphdr)+ICMP_DATALEN

typedef struct ping_conf_s
{
	const char *ping_host;
	const char *ping_resolver;
	time_t      ping_timeout;

	context_t  *dns;

	int			icmp_ident;
	struct sockaddr_in icmp_dest;

	time_t		icmp_interval;
	time_t		icmp_ttl;
	int			icmp_max;
} ping_conf_t;

#endif // _PING_H_
