
#ifndef __DNS_H_
#define __DNS_H_

#include <time.h>

typedef struct dns_conf_s
{
	const char *dns_resolver;			// Filename of resolver conf (/etc/resolv.conf if NULL)
	time_t      dns_timeout;

	const char *dns_host;				// Hostname to use for DNS test
	char    dns_servers[10][32];		// List of DNS servers
	int     dns_max_servers;			// Number of DNS server addresses loaded
	time_t  dns_ttl;	     			// Time between DNS tests
} dns_conf_t;

#endif // _DNS_H_
