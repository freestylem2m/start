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

#include "netmanage.h"
#include "driver.h"
#include "config.h"
#include "driver_config.h"

const driver_t driver_table[] = {
#include "driver_setup.h"
};

context_t *context_list;

const int driver_max = sizeof( driver_table ) / sizeof( *driver_table );

// Dummy data
driver_data_t driver_data_dummy = { TYPE_NONE, {} };

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

void context_check_health()
{
	d_printf("Checking context health\n");
	context_t **ctx = & context_list;

	while( ctx && *ctx ) {
		if( (*ctx)->flags & CTX_DEAD ) {
			d_printf("Pruning dead context!!\n");

			context_t *tail = (*ctx)->next;
			if( (*ctx)->event )
				deregister_event_handler( (*ctx)->event );

			if( (*ctx)->data )
				free( (*ctx)->data );
			free( (void *) (*ctx)->name );
			free( *ctx );
			*ctx = tail;
		} else
			ctx = & (*ctx)->next;
	}
	//context_delete(NULL, NULL);
}

context_t *context_create(const char *service_name, const config_t *service_config, const driver_t *driver, const config_t *driver_config)
{
	context_t *ptr = calloc( sizeof(context_t), 1);
	
	if( ptr ) {
		ptr->name = strdup( service_name );
		ptr->config = service_config;
		ptr->driver = driver;
		ptr->driver_config = driver_config;
		ptr->next = context_list;
		ptr->flags = CTX_NONE;
		context_list = ptr;
	}

	return ptr;
}

void context_terminate( context_t *ctx ) {
	ctx->flags |= CTX_DEAD;
}

void set_child( context_t *ctx, context_t *child ) {
	ctx->child = child;
}

void set_parent( context_t *ctx, context_t *parent ) {
	ctx->parent = parent;
}

void context_delete(context_t *ctx, const char *name)
{
	context_t **ptr = &context_list;

	if( ctx ) {
		while( *ptr && *ptr != ctx )
			ptr = &((*ptr)->next);
	} else
		if( name ) {
			while( *ptr && strcasecmp( (*ptr)->name, name ) )
				ptr = &((*ptr)->next);
		}

	if( ptr && *ptr ) {
		context_t *tail = (*ptr)->next;
		free( (void *) (*ptr)->name);
		free( (void *) *ptr);
		*ptr = tail;
	}
}

context_t *find_context(const char *name)
{
	context_t *ptr = context_list;

	while( ptr && strcasecmp( ptr->name, name ))
		ptr = ptr->next;

	return ptr;
}

