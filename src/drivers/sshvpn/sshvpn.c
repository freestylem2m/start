/*
 * File: sshvpn.c
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
#include "sshvpn.h"
#include "logger.h"
#include "clock.h"

int sshvpn_init(context_t *context)
{
	sshvpn_config_t *cf;

	if ( 0 == (cf = (sshvpn_config_t *) calloc( sizeof( sshvpn_config_t ) , 1 )))
		return 0;

	cf->state = SSHVPN_STATE_IDLE;

	context->data = cf;

	return 1;
}

int sshvpn_shutdown( context_t *ctx)
{
	sshvpn_config_t *cf = (sshvpn_config_t *) ctx->data;

	if( cf->network )
		context_terminate( cf->network );
	if( cf->transport )
		context_terminate( cf->transport );

	free( cf );

	return 1;
}

ssize_t sshvpn_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_data_t *data = 0L;
	event_child_t *child = 0L;
	context_t *source = event_data->source;

	sshvpn_config_t *cf = (sshvpn_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;
	else if( event_data->type == TYPE_CHILD )
		child = & event_data->event_child;

	switch( event ) {
		case EVENT_INIT:
			cf->transport_driver = config_get_item( ctx->config, "transportdriver" );
			cf->network_driver = config_get_item( ctx->config, "networkdriver" );

			x_printf(ctx, "transport driver = %s\n",cf->transport_driver );
			x_printf(ctx, "network driver = %s\n",cf->network_driver );

			cf->state = SSHVPN_STATE_RUNNING;

		case EVENT_START:
			if( !cf->network_driver )
				context_terminate( ctx );
			else {
				if( cf->transport_driver )
					start_service( &cf->transport, cf->transport_driver, ctx->config, ctx, 0L );
				else
					start_service( &cf->network,  cf->network_driver, ctx->config, ctx, 0L );
			}

			break;

		case EVENT_CHILD:
			x_printf(ctx,"got event_child from %s\n",source->name);
			if( child->ctx == cf->transport ) {
				x_printf(ctx,"noting change of state for transport layer: %d\n", child->action);
				switch( child->action ) {
					case CHILD_STOPPED:
						x_printf(ctx,"transport driver has exited.  Terminating\n");
						cf->transport = 0L;

						if( cf->network ) {
							x_printf(ctx,"Telling network layer to terminate\n");
							emit( cf->network, EVENT_TERMINATE, 0L );
						} else
							context_terminate( ctx );
						break;
					case CHILD_EVENT:
						x_printf(ctx,"Got a CHILD_EVENT status = %d, launching network driver\n",child->status);

						if( !cf->network )
							start_service( &cf->network, cf->network_driver, ctx->config, ctx, 0L );

						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->network ) {
				x_printf(ctx,"noting change of state for network layer: %d\n",child->action);
				switch( child->action ) {
					case CHILD_STARTED:
						cf->flags |= SSHVPN_NETWORK_UP;
						break;
					case CHILD_STOPPED:
						cf->flags &= ~(unsigned int)SSHVPN_NETWORK_UP;
						cf->network = 0L;

						if( cf->transport ) {
							x_printf(ctx,"telling transport layer to restart\n");
							emit( cf->transport, EVENT_TERMINATE, 0l );
						} else
							context_terminate(ctx);
						break;

					case CHILD_EVENT:
						context_owner_notify( ctx, CHILD_EVENT, 0 );
						break;
					default:
						break;
				}
			}
			break;

		case EVENT_RESTART:
		case EVENT_TERMINATE:
			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );

			if( cf->transport )
				emit( cf->transport, EVENT_TERMINATE, 0L );

			if( cf->state == SSHVPN_STATE_RUNNING ) {
				cf->flags |= SSHVPN_TERMINATING;
			} else
				context_terminate( ctx );
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( source == cf->transport && cf->network ) {
				x_printf(ctx,"DATA: transport -> network\n");
				return emit( cf->network, event, event_data );
			} else {
				x_printf(ctx,"no network available, discarding data\n");
				return (ssize_t) data->bytes;
			}
			if( source == cf->network && cf->transport ) {
				x_printf(ctx,"DATA: network -> transport\n");
				return emit( cf->transport, event, event_data );
			} else {
				x_printf(ctx,"no transport available, discarding data\n");
				return (ssize_t) data->bytes;
			}
			break;

		case EVENT_ALARM:
			break;

		default:
			x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
    return 0;
}
