
#ifndef __NETMANAGE_H__
#define __NETMANAGE_H__

#ifndef NDEBUG
#define	d_printf(...)	{ printf("%s:%s:%d ",__FILE__,__func__,__LINE__); \
						printf(__VA_ARGS__); }
#else
#define	d_printf(...)
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#endif // __NETMANAGE_H__
