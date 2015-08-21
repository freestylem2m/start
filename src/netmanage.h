
#ifndef __NETMANAGE_H__
#define __NETMANAGE_H__

#include "cmdline.h"
#include "events.h"

#ifndef NDEBUG
#define DEBUG_BUF 1024
#include <alloca.h>
#include <string.h>

#define	d_printf(...)	{ if (debug >= debug_quiet) { char *pbuf = alloca(DEBUG_BUF); snprintf(pbuf,DEBUG_BUF,"%s:%s:%d",__FILE__,__func__,__LINE__); if( !msg_filter || strstr(pbuf, msg_filter) ) {fprintf(stderr,"%s: ",pbuf); fprintf(stderr,__VA_ARGS__); fflush(stderr); } } }
#define	x_printf(x,...)	{ if (debug >= debug_quiet) { char *pbuf = alloca(DEBUG_BUF); snprintf(pbuf,DEBUG_BUF,"%s:%s:%d:<%s>",__FILE__,__func__,__LINE__,x->name); if( !msg_filter || strstr(pbuf, msg_filter) ) {fprintf(stderr,"%s: ",pbuf); fprintf(stderr,__VA_ARGS__); fflush(stderr); } } }
#else
#define	d_printf(...)   {}
#define x_printf(...)   {}
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

// AT is a string represending "__file__:__line:" and can be used as a string
// for concatenation
// eg perror( AT read ) -> perror("myfile.c:54:read")
#define TOSTRING(x) #x
#define AT __FILE__":"TOSTRING(__LINE__)":"

#define CONFIG_MAX_DEPTH 6
extern context_t *start_service( const char *name, const config_t *parent_config, context_t *parent );

#endif // __NETMANAGE_H__
