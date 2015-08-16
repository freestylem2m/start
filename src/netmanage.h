
#ifndef __NETMANAGE_H__
#define __NETMANAGE_H__

#include "cmdline.h"

#ifndef NDEBUG
#define	d_printf(...)	{ if (debug >= debug_quiet) { printf("%s:%s:%d ",__FILE__,__func__,__LINE__); printf(__VA_ARGS__); } }
#else
#define	d_printf(...)
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#endif // __NETMANAGE_H__
