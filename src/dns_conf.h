
#ifndef __DNS_H_
#define __DNS_H_

#include <time.h>

typedef struct dns_init_s
{
	const char *dns_host;
	const char *dns_resolver;
	time_t      dns_timeout;
} dns_init_t;

#endif // _DNS_H_
