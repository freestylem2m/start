/*
 * File: driver.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>

#include "netmanage.h"
#include "driver.h"
#include "config.h"
#include "driver_config.h"

const driver_t driver_table[] = {
#include "driver_setup.h"
};

static const int driver_max = sizeof( driver_table ) / sizeof( *driver_table );

// Dummy data.  Used whenever emit() is called with a null event pointer.
// If you call driver->emit() directly, avoid passing a null event pointer, pass
// this instead.
driver_data_t driver_data_dummy = { TYPE_NONE, 0L, {} };

#define ENV_MAX 64
const char *get_env( context_t *ctx, const char *name )
{
	const char *value;

	// Construct an environment variable name from the service name and the item name
	// service 'media' looking for item 'folder' will check for 'MEDIA_FOLDER' in the
	// environment

	char env_name[ENV_MAX];
	int  ei = 0;
	const char *p;

	if( ctx && ctx->name ) {
		p = ctx->name;

		while (*p && ei < ENV_MAX-2)
			env_name[ei++] = (char) toupper( *p++ );

		env_name[ei++] = '_';
	}

	p = name;
	while (*p && ei < ENV_MAX-1)
		env_name[ei++] = (char) toupper( *p++ );

	env_name[ei] = 0;
	value = getenv(env_name);

	if( !value && ctx ) {
		value = config_get_item( ctx->config, name );
		if( !value && ctx->owner )
			value = config_get_item( ctx->owner->config, name );
		if( !value && ctx->driver_config )
			value = config_get_item( ctx->driver_config, name );
	}

	return value;
}

context_t *start_driver( context_t **pctx, const char *driver_name, const char *context_name, const config_t *parent_config, context_t *owner, void *pdata )
{
	const driver_t     *driver;
	const config_t     *driver_config;
	context_t          *ctx;

	if (!context_name)
		context_name = driver_name;

	if ((ctx = find_context(driver_name))) {
		d_printf("find_context(%s) found %p (%p)\n",driver_name, ctx, ctx->name);
		if( pctx )
			*pctx = ctx;
		return ctx;
	}

	driver_config = config_get_section(driver_name);
	if(( driver = find_driver(driver_name) )) {
		if (( ctx = context_create(context_name, parent_config, driver, driver_config) )) {
			ctx->owner = owner;
			if (driver->init(ctx)) {
				if( pctx )
					*pctx = ctx;
				driver_data_t notification = { TYPE_CUSTOM, owner, {} };
				notification.event_custom = pdata;
				ctx->driver->emit(ctx, EVENT_INIT, &notification);

				if( ctx->state == CTX_UNUSED ) {
					d_printf("Driver %s failed to start, cleaning context pointer\n", ctx->name);
					*pctx = ctx = 0L;
				}

				return ctx;
			} else
				context_delete(ctx, NULL);
		}
	} else
		// This should be a call to logger()
		d_printf("Unable to locate driver %s for %s\n", driver_name, parent_config->section);

	return 0L;
}

const driver_t *find_driver( const char *driver )
{
	int i;

	for( i = 0; i < driver_max; i++ )
		if( ! strcasecmp( driver, driver_table[i].name ) )
			return &driver_table[i];

	return 0L;
}

context_t *context_find_entry( const char *name )
{
	int i;

	for( i = 0; i < MAX_CONTEXTS; i++ )
		if( (context_table[i].state != CTX_UNUSED) && (!strcasecmp(context_table[i].name, name)))
			return &context_table[i];

	return 0L;
}

context_t *context_find_free_slot( void )
{
    int i;

    for( i = 0; i < MAX_CONTEXTS; i++ )
        if( context_table[i].state == CTX_UNUSED )
            return &context_table[i];

    return 0L;
}

context_t *context_create(const char *service_name, const config_t *service_config, const driver_t *driver, const config_t *driver_config)
{
    context_t *ptr = context_find_free_slot();

	if( ! ptr )
		return 0L;
	

	strncpy( ptr->name, service_name, MAX_SERVICE_NAME );
	ptr->config = service_config;
	ptr->driver = driver;
	ptr->driver_config = driver_config;
	ptr->state = CTX_STARTING;
#ifndef NDEBUG
	config_get_intval(service_config, "debug", &(ptr->debug));
#endif

	return ptr;
}

void context_owner_notify( context_t *ctx, child_status_t state, int status )
{
	if( ctx->owner ) {
		driver_data_t notification = { TYPE_CHILD, ctx, {} };
        notification.event_child.ctx = ctx;
		notification.event_child.action = state;
		notification.event_child.status = status;
		emit( ctx->owner, EVENT_CHILD, &notification );
	}
}

int context_terminate( context_t *ctx )
{
	int i;

	x_printf(ctx,"Context_Terminate called for %s\n",ctx->name);

	for( i = 0; i < MAX_EVENT_REQUESTS; i++ ) {
		if (event_table[i].ctx == ctx) {
			event_table[i].fd = -1;
			event_table[i].ctx = 0;
			event_table[i].flags = EH_UNUSED;
		}
	}

    context_owner_notify( ctx, CHILD_STOPPING, 0 );

	if( ctx->driver )
		ctx->driver->shutdown( ctx );

    context_owner_notify( ctx, CHILD_STOPPED, 0 );
	ctx->owner = 0;
	ctx->state = CTX_UNUSED;

	return 0;
}

void context_delete(context_t *ctx, const char *name)
{
	if( name )
		ctx  = context_find_entry( name );

	if( ctx )
		ctx->state = CTX_UNUSED;
}

context_t *find_context(const char *name)
{
	return context_find_entry( name );
}
