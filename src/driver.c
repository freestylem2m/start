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

			// deal with parent relationships
			// deal with child relationships
			context_t *_c = context_list;

			while( _c ) {
				if( _c->child == *ctx ) {
					emit(_c, EVENT_CHILD_REMOVED, DRIVER_DATA_NONE );
					_c->child = 0;
				}
				if( _c->parent == *ctx ) {
					emit(_c, EVENT_PARENT_REMOVED, DRIVER_DATA_NONE );
					_c->parent = 0;
				}

				_c = _c->next;
			}
			
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
		ptr->flags = CTX_DEAD; // all drivers are born dead.. they awaken when everything looks right.
		context_list = ptr;
	}

	return ptr;
}

void context_awaken(context_t *ctx)
{
	d_printf("Starting to awaken context\n");
	while( ctx ) {
		ctx->flags &= ~(unsigned) CTX_DEAD;
		d_printf("Sending EVENT_INIT to %s\n",ctx->name );
		emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
		ctx = ctx->next;
	}
}

void context_terminate( context_t *ctx ) {
	d_printf("context_terminate(%s) called\n",ctx->name);
	ctx->flags |= CTX_DEAD;
	// To prevent deadlock, dont notify any children if they
	// are already dead. (but un-reaped)
	if( ctx->child && (ctx->child->flags & CTX_DEAD) == 0)
		emit_child( ctx, EVENT_TERMINATE, DRIVER_DATA_NONE );
}

void set_child( context_t *ctx, context_t *child ) {
	d_printf("set_child( parent = %s, child = %s )\n",ctx->name, child->name);
	ctx->child = child;
	emit( ctx, EVENT_CHILD_ADDED, DRIVER_DATA_NONE );
}

void set_parent( context_t *ctx, context_t *parent ) {
	d_printf("set_parent( parent = %s, child = %s )\n",parent->name, ctx->name);
	ctx->parent = parent;
	emit( ctx, EVENT_PARENT_ADDED, DRIVER_DATA_NONE );
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

