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

#define FATAL(x,...) __fatal(__func__,__LINE__,x,##__VA_ARGS__)
int parse_cmdline( int, char *[]);
void dump_cmdline();

extern const char *programname;
extern char *config_file;
extern char *msg_filter;
extern int debug;
extern int debug_quiet;

extern int __fatal(const char *func, int line, const char *fmt, ...);

#endif
