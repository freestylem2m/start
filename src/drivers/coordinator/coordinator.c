/*
 * File: coordinator.c
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
#include <unistd.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "coordinator.h"
#include "logger.h"
#include "unicorn.h"
#include "clock.h"

int coordinator_init(context_t *context)
{
	coordinator_config_t *cf;

	if ( 0 == (cf = (coordinator_config_t *) calloc( sizeof( coordinator_config_t ) , 1 )))
		return 0;

	cf->state = COORDINATOR_STATE_IDLE;

	context->data = cf;

	return 1;
}

int coordinator_shutdown( context_t *ctx)
{
	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	if( cf->logger )
		context_terminate( cf->logger );

	free( cf );

	return 1;
}

#define MAX_READ_BUFFER 1024
ssize_t coordinator_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_data_t *data = 0L;
	event_child_t *child = 0L;
	context_t *source = event_data->source;

	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	//x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;
	else if( event_data->type == TYPE_CHILD )
		child = & event_data->event_child;

	switch( event ) {
		case EVENT_INIT:
			{
				const char *log = config_get_item( ctx->config, "logger" );
				cf->modem_driver = config_get_item( ctx->config, "modemdriver" );
				cf->network_driver = config_get_item( ctx->config, "networkdriver" );
				cf->control_file = config_get_item( ctx->config, "controlfile" );

				cf->flags |= COORDINATOR_NETWORK_DISABLE;

                if( log )
                    cf->logger = start_service( log, ctx->config, ctx );

				cf->state = COORDINATOR_STATE_RUNNING;

				event_alarm_add( ctx, COORDINATOR_CONTROL_CHECK_INTERVAL, ALARM_INTERVAL );
				check_control_file( ctx );
			}
			break;

		case EVENT_LOGGING:
			emit( cf->logger, event, event_data );
			break;

		case EVENT_RESTART:
			if( source == cf->unicorn ) {
				if( event_data->event_child.action == CHILD_EVENT ) {
					x_printf(ctx,"Unicorn driver notifying me that the modem driver has restarted.. terminating network driver\n");
					if( cf->network )
						emit( cf->network, EVENT_TERMINATE, 0L );
				}
			}
			break;

		case EVENT_CHILD:
			if( child->ctx == cf->unicorn ) {
				switch( child->action ) {
					case CHILD_STOPPED:
						x_printf(ctx,"Unicorn driver has exited.  Terminating\n");
						cf->unicorn = 0L;
						cf->flags &= ~(unsigned int)COORDINATOR_MODEM_UP;
						if( (cf->flags & COORDINATOR_TERMINATING) && !cf->network )
							context_terminate( ctx );
						break;
					case CHILD_EVENT:
						switch( child->status ) {
							case UNICORN_MODE_ONLINE:
								cf->flags |= COORDINATOR_MODEM_ONLINE;
								if( cf->network ) {
									// Restart network driver maybe
									uint8_t sig = SIGKILL;
									driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
									notification.event_custom = &sig;
									emit( cf->network, EVENT_RESTART, &notification );
								} else {
									cf->network = start_service( cf->network_driver, ctx->config, ctx );
									if( !cf->network ) 
										cf->flags |= COORDINATOR_TERMINATING;
								}
								break;
							case UNICORN_MODE_OFFLINE:
								cf->flags &= ~(unsigned int)COORDINATOR_MODEM_ONLINE;
								if( cf->network )
									emit( cf->network, EVENT_TERMINATE, 0L );
								break;
						}
		
						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->network ) {
				switch( child->action ) {
					case CHILD_STARTED:
						cf->flags |= COORDINATOR_NETWORK_UP;
						break;
					case CHILD_STOPPED:
						cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_UP;
						cf->network = 0L;

						if( cf->unicorn ) {
							emit( cf->unicorn, EVENT_RESTART, 0l );
						} else if (cf->flags & COORDINATOR_TERMINATING) 
							context_terminate(ctx);
						break;
					default:
						break;
				}
			}
			break;

		case EVENT_TERMINATE:
			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );

			if( cf->unicorn )
				emit( cf->network, EVENT_TERMINATE, 0L );
		
			if( cf->state == COORDINATOR_STATE_RUNNING ) {
				cf->flags |= COORDINATOR_TERMINATING;
			} else
				context_terminate( ctx );
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data )
				if( source == cf->unicorn && cf->network )
					return emit( cf->network, event, event_data );
				if( source == cf->network && cf->unicorn )
					return emit( cf->unicorn, event, event_data );
			break;

		case EVENT_ALARM:
			check_control_file(ctx);
			break;
		
        default:
            x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
    }
    return 0;
}

int check_control_file(context_t *ctx) 
{
	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	struct stat info;

	int control = stat( cf->control_file, &info );

	if( cf->flags & COORDINATOR_NETWORK_DISABLE ) {
		if( control ) {
			// ensure connection happens
			cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_DISABLE; 
			if( cf->modem_driver )
				cf->unicorn = start_service( cf->modem_driver, ctx->config, ctx );
		}
	} else {
		if( !control ) {
			// ensure connection happens
			cf->flags |= COORDINATOR_NETWORK_DISABLE; 
			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );
			if( cf->unicorn )
				emit( cf->unicorn, EVENT_TERMINATE, 0L );
		}
	}

	return 0;
}

