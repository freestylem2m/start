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

	if( cf->network )
		context_terminate( cf->network );
	if( cf->unicorn )
		context_terminate ( cf->unicorn );
	if( cf->logger )
		context_terminate( cf->logger );

	free( cf );

	return 1;
}

ssize_t coordinator_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_data_t *data = 0L;
	event_child_t *child = 0L;
	context_t *source = event_data->source;

	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

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
				cf->vpn_driver = config_get_item( ctx->config, "vpndriver" );

				cf->control_modem = config_get_item( ctx->config, "control" );
				cf->control_vpn = config_get_item( ctx->config, "vpncontrol" );

				event_add( ctx, SIGTERM, EH_SIGNAL );

				if( config_istrue( ctx->config, "vpnalways" ) )
					cf->flags |= COORDINATOR_VPN_STANDALONE;

				cf->flags |= COORDINATOR_NETWORK_DISABLE|COORDINATOR_VPN_DISABLE;

				if( log )
					start_service( &cf->logger, log, ctx->config, ctx, 0L );
			}
		case EVENT_START:

			cf->state = COORDINATOR_STATE_RUNNING;
			cf->timer_fd = event_alarm_add( ctx, COORDINATOR_CONTROL_CHECK_INTERVAL, ALARM_INTERVAL );
			event_alarm_add( ctx, 300000, ALARM_INTERVAL );
			check_control_files( ctx );
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

						if( cf->network )
							emit( cf->network, EVENT_TERMINATE, 0L );

						if( (cf->flags & COORDINATOR_TERMINATING) && !cf->network && !cf->vpn )
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
									start_service( &cf->network, cf->network_driver, ctx->config, ctx, 0L );
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
						break;
					case CHILD_EVENT:
						if( !( cf->flags & COORDINATOR_VPN_STANDALONE )) {
							cf->flags |= COORDINATOR_VPN_DISABLE;
							if( cf->vpn )
								emit( cf->vpn, EVENT_TERMINATE, 0L );
						}
						x_printf(ctx,"NETWORK STATE CHANGED TO UP\n");
						cf->flags |= COORDINATOR_NETWORK_UP;

						break;
					case CHILD_STOPPED:
						x_printf(ctx,"NETWORK STATE CHANGED TO DOWN\n");
						cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_UP;
						cf->network = 0L;

						if( cf->vpn && !(cf->flags & COORDINATOR_VPN_STANDALONE) )
							emit( cf->vpn, EVENT_TERMINATE, 0L );

						if( cf->unicorn )
							emit( cf->unicorn, EVENT_RESTART, 0L );
						else if (cf->flags & COORDINATOR_TERMINATING)
							context_terminate(ctx);
						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->vpn ) {
				switch( child->action ) {
					case CHILD_STARTED:
						cf->flags |= COORDINATOR_VPN_UP;
						break;
					case CHILD_STOPPED:
						cf->flags &= ~(unsigned int)COORDINATOR_VPN_UP;
						cf->vpn = 0L;

						cf->flags |= COORDINATOR_VPN_DISABLE;

						if( cf->flags & COORDINATOR_TERMINATING )
							context_terminate(ctx);
						break;
					default:
						break;
				}
			}
			break;

		case EVENT_TERMINATE:
			if( cf->vpn )
				emit( cf->vpn, EVENT_TERMINATE, 0L );

			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );

			if( cf->unicorn )
				emit( cf->unicorn, EVENT_TERMINATE, 0L );
		
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

		case EVENT_SIGNAL:
			if( event_data->event_signal == SIGTERM ) {
				kill(0,SIGTERM);
				exit(0);
			}
			break;

		case EVENT_ALARM:
			if( event_data->event_alarm == cf->timer_fd )
				check_control_files(ctx);
			else {
				kill(0,SIGTERM);
				exit(0);
			}
			break;
		
        default:
            x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
    }
    return 0;
}

int check_control_files(context_t *ctx)
{
	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	struct stat info;

	int modem_enable = stat( cf->control_modem, &info ) ? 1:0;
	int vpn_enable   = stat( cf->control_vpn,   &info ) ? 0:1;

	if( cf->flags & COORDINATOR_NETWORK_DISABLE ) {
		if( modem_enable ) {
			// ensure connection happens
			cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_DISABLE;
			if( cf->modem_driver )
				start_service( &cf->unicorn, cf->modem_driver, ctx->config, ctx, 0L );
		}
	} else {
		if( !modem_enable ) {
			// ensure connection happens
			cf->flags |= COORDINATOR_NETWORK_DISABLE;
			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );
			if( cf->unicorn )
				emit( cf->unicorn, EVENT_TERMINATE, 0L );
		}
	}

	if( cf->flags & COORDINATOR_VPN_DISABLE ) {
		x_printf(ctx,"VPN marked disabled\n");
		if( vpn_enable ) {
			x_printf(ctx,"Control file exists.. enabling\n");
			cf->flags &= ~(unsigned int)COORDINATOR_VPN_DISABLE;
			if( cf->flags & (COORDINATOR_VPN_STANDALONE|COORDINATOR_NETWORK_UP) ) {
				x_printf(ctx,"Attempting to launch vpn service %s\n", cf->vpn_driver);
				if( !cf->vpn ) {
					if( ! start_service( &cf->vpn, cf->vpn_driver, ctx->config, ctx, 0L ) ) {
						x_printf(ctx,"Failed to start VPN.  Disabling - this will cause a retry\n");
						cf->flags |= COORDINATOR_VPN_DISABLE;
					}
				}
			}
		}
	} else {
		if( !vpn_enable ) {
			cf->flags |= COORDINATOR_VPN_DISABLE;
			if( cf->vpn )
				emit( cf->unicorn, EVENT_TERMINATE, 0L );
		}
	}

	return 0;
}
