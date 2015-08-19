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

extern driver_data_t driver_data_dummy;

#define DRIVER_DATA_NONE (&driver_data_dummy)

extern const char *get_env( context_t *ctx, const char *name );
extern const driver_t driver_table[];
extern const driver_t *find_driver(const char *);
extern context_t *context_create(const char *service_name, const config_t *service_config, const driver_t *driver, const config_t *driver_config);
extern int context_terminate( context_t *ctx );
extern void context_delete(context_t *ctx, const char *name);
extern context_t *find_context(const char *name);
extern ssize_t emit( context_t *ctx, event_t event, driver_data_t *event_data );

#endif
