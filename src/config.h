/*
 * File: config.h
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

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <time.h>

#define LINE_MAX 1024

typedef struct config_entry_s {
	const char		      *item;
	const char            *value;
	const char           **list;
	struct config_entry_s *next;
} config_entry_t;

typedef struct config_s {
	const char       *section;
	config_entry_t   *config;
	struct config_s  *next;
} config_t;

extern config_t *global_config;

extern int config_read_file( const char * );
extern void config_cleanup(void);

extern const config_t *config_get_section( const char *section );
extern const config_entry_t *config_get_entry( const config_t *section, const char *item );

extern const char *config_item( const char *section, const char *item );
extern const char **config_itemlist( const char *section, const char *item );

extern const char *config_get_item( const config_t *section, const char *item );
extern const char **config_get_itemlist( const config_t *section, const char *item );

extern int config_get_intval(const config_t *section, const char *item);
extern unsigned int config_get_binval(const config_t *section, const char *item);
extern time_t config_get_timeval(const config_t *section, const char *item);
extern int config_istrue( const config_t *section, const char *item );

#endif
