
#ifndef __ICMP_H_
#define __ICMP_H_

#include <time.h>
#include "netinet/in.h"
#include "events.h"

#define ICMP_DATALEN   64-sizeof(struct icmphdr)
#define ICMP_PAYLOAD   128+sizeof(struct icmphdr)+ICMP_DATALEN

typedef struct icmp_conf_s
{
	const char *icmp_host;
	time_t      icmp_timeout;

	int			icmp_ident;
	struct sockaddr_in icmp_dest;

	int         icmp_sequence;
} icmp_conf_t;

#endif // _ICMP_H_
