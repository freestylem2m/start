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

extern char *config_file;
extern int debug;
extern int debug_quiet;

#endif
