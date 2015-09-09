/*
 * File: cmdline.h
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */

#ifndef __CMDLINE_H__
#define __CMDLINE_H__

// For LONG arguments type, support scaling factors for 'kilo', 'mega' 'giga' etc.
typedef struct scalefactor_s
{
	char                c;
	unsigned long       scale;
} scalefactor;

extern const scalefactor binary_scale[];
extern const scalefactor time_scale[];

#define FATAL(x,...) __fatal(__func__,__LINE__,x,##__VA_ARGS__)
int                 parse_cmdline(int, char *[]);
void                dump_cmdline();

extern const char  *programname;
extern char        *config_file;
extern int          debug;
extern int          debug_quiet;
#ifndef NDEBUG
extern char        *msg_filter;
#endif

extern int          __fatal(const char *func, int line, const char *fmt, ...) __attribute__((format(printf,3,4)));
//extern int          __fatal(const char *func, int line, const char *fmt, ...);

#endif
