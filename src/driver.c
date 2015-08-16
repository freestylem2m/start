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

const int driver_max = sizeof( driver_table ) / sizeof( *driver_table );

// Dummy data
driver_data_t driver_data_dummy = { TYPE_NONE, 0L, {} };

#define ENV_MAX 64
const char *get_env( context_t *ctx, const char *name )
{
	const char *value = config_get_item( ctx->config, name );
	if( !value && ctx->driver_config )
		value = config_get_item( ctx->driver_config, name );

	if( !value ) {
		char env_name[ENV_MAX];
		int  ei = 0;
		const char *p = ctx->name;
		while (*p && ei < ENV_MAX)
			env_name[ei++] = (char) toupper( *p++ );
		if( ei < ENV_MAX ) {
			env_name[ei++] = '_';
			p = name;
			while (*p && ei < ENV_MAX)
				env_name[ei++] = (char) toupper( *p++ );
		}
		if( ei < ENV_MAX )
			env_name[ei] = 0;
		env_name[ENV_MAX-1] = 0;
		value = getenv(env_name);
	}
	return value;
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
		if( (context_table[i].flags != CTX_UNUSED) && (!strcasecmp(context_table[i].name, name))) {
			return &context_table[i];
		}

	return 0L;
}

context_t *context_find_free_slot( void )
{
    int i;

    for( i = 0; i < MAX_CONTEXTS; i++ )
        if( context_table[i].flags == CTX_UNUSED )
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
	ptr->flags = CTX_STARTING;

	return ptr;
}

void context_terminate( context_t *ctx ) {
	d_printf("context_terminate(%s) called\n",ctx->name);

	int i;
	for( i = 0; i < MAX_EVENT_REQUESTS; i++ ) {
		if (event_table[i].ctx == ctx)
			event_table[i].flags = EH_UNUSED;
	}

	if( ctx->driver )
		ctx->driver->shutdown( ctx );

	ctx->flags |= CTX_TERMINATING;
}

void context_delete(context_t *ctx, const char *name)
{
	if( name )
		ctx  = context_find_entry( name );

	if( ctx )
		ctx->flags = CTX_UNUSED;
}

context_t *find_context(const char *name)
{
	return context_find_entry( name );
}
