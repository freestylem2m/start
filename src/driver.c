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

context_list_t *context_list;

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

void context_check_health()
{
	d_printf("Checking context health\n");
	context_list_t **cl = & context_list;
	context_t *ctx;

	while( cl && *cl ) {
		ctx = (*cl)->context;
		if( ctx->flags & CTX_DEAD ) {
			d_printf("Pruning dead context (%s)!!\n", ctx->name);

			// deal with parent relationships
			// deal with child relationships
			context_list_t *_cl = context_list;

			while( _cl ) {
				context_t *_c = _cl->context;

			//kill(getpid(),11);
				if( _c->child ) {
					context_list_t **_child_list = & ( _c->child );
					while (*_child_list) {
						if( (*_child_list)->context == ctx /* || ((*_child_list)->context->flags & CTX_DEAD) */ ) {
							context_list_t *tail = (*_child_list)->next;
							free( (void *) *_child_list );
							*_child_list = tail;
						} else
							_child_list = & ( *_child_list )->next;
					}
				
					emit(_c, EVENT_CHILD_REMOVED, DRIVER_DATA_NONE );
				}

				if( _c->parent == ctx ) {
					emit(_c, EVENT_PARENT_REMOVED, DRIVER_DATA_NONE );
					_c->parent = 0;
				}

				_cl = _cl->next;
			}
			
			context_list_t *tail = (*cl)->next;
			if( ctx->event )
				deregister_event_handler( ctx->event );

			if( ctx->data )
				free( ctx->data );
			free( (void *) ctx->name );

			while( ctx->child ) {
				context_list_t *next = ctx->child->next;
				free( ctx->child );
				ctx->child = next;
			}
			free( ctx );
			free( *cl );

			*cl = tail;
		} else
			cl = & (*cl)->next;
	}
}

context_t *context_create(const char *service_name, const config_t *service_config, const driver_t *driver, const config_t *driver_config)
{
	context_t *ptr = calloc( sizeof(context_t), 1);
	context_list_t *lptr;
	
	if( ! ptr )
		return 0L;
	
	lptr = calloc( sizeof(context_list_t), 1);

	if( !lptr ) {
		free( ptr );
		return 0L;
	}
	
	ptr->name = strdup( service_name );
	ptr->config = service_config;
	ptr->driver = driver;
	ptr->driver_config = driver_config;
	ptr->flags = 0;

	lptr->context = ptr;
	lptr->next = context_list;
	context_list = lptr;

	return ptr;
}

void context_terminate( context_t *ctx ) {
	d_printf("context_terminate(%s) called\n",ctx->name);
	ctx->flags |= CTX_DEAD;
	// To prevent deadlock, dont notify any children if they
	// are already dead. (but un-reaped)
	driver_data_t temp_data = *DRIVER_DATA_NONE;
	temp_data.source = ctx;

	emit_child( ctx, EVENT_TERMINATE, &temp_data );
}

void set_child( context_t *ctx, context_t *child ) {
	d_printf("set_child( parent = %s, child = %s )\n",ctx->name, child->name);
	context_list_t *cll = calloc( sizeof(context_list_t), 1);
	if( cll ) {
		cll->context = child;
		cll->next = ctx->child;
		ctx->child = cll;
	}
	emit( ctx, EVENT_CHILD_ADDED, DRIVER_DATA_NONE );
}

void set_parent( context_t *ctx, context_t *parent ) {
	d_printf("set_parent( parent = %s, child = %s )\n",parent->name, ctx->name);
	ctx->parent = parent;
	emit( ctx, EVENT_PARENT_ADDED, DRIVER_DATA_NONE );
}

void context_delete(context_t *ctx, const char *name)
{
	context_list_t **lptr = &context_list;

	if( ctx ) {
		while( *lptr && (*lptr)->context != ctx )
			lptr = &((*lptr)->next);
	} else
		if( name ) {
			while( *lptr && strcasecmp( (*lptr)->context->name, name ) )
				lptr = &((*lptr)->next);
		}

	if( lptr && *lptr ) {
		context_list_t *tail = (*lptr)->next;
		context_list_t *cll = (*lptr)->context->child;
		while( cll ) {
			context_list_t *next = cll->next;
			free( (void *) cll );
			cll = next;
		}
		free( (void *) (*lptr)->context->name);
		free( (void *) (*lptr)->context);
		free( (void *) *lptr);
		*lptr = tail;
	}
}

context_t *find_context(const char *name)
{
	context_list_t *lptr = context_list;

	while( lptr && strcasecmp( lptr->context->name, name ))
		lptr = lptr->next;

	return lptr->context;
}
