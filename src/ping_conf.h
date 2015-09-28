
#ifndef __PING_H_
#define __PING_H_

#include <time.h>

typedef struct ping_init_s
{
	const char *ping_host;
	const char *ping_resolver;
	time_t      ping_timeout;
} dns_init_t;

#endif // _PING_H_
