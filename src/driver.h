/*
 * File: driver.h
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

#ifndef __DRIVER_H__
#define __DRIVER_H__

#include "config.h"
#include "events.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

typedef struct driver_s
{
	const char     *name;
	int             (*init) (context_t *);
	int             (*shutdown) (context_t *);
	int             (*emit) (context_t *context, event_t event, void *event_data );
} driver_t;

extern void set_child( context_t *ctx, context_t *child );
extern void set_parent( context_t *ctx, context_t *parent );
extern const char *get_env( context_t *ctx, const char *name );
extern const driver_t driver_table[];
extern const driver_t *find_driver(const char *);
extern context_t *context_create(const char *service_name, const config_t *service_config, const driver_t *driver, const config_t *driver_config);
extern void context_delete(context_t *ctx, const char *name);
extern void context_check_health();
extern context_t *find_context(const char *name);

#endif
