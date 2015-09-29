/*
 *
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
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

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

	if( cf->s_network ) {
		cf->flags |= COORDINATOR_NETWORK_SHUTDOWN;
		context_terminate( cf->s_network );
	}
	if( cf->s_unicorn )
		context_terminate ( cf->s_unicorn );

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
				x_printf(ctx,"calling event add SIGTERM\n");
				event_add( ctx, SIGTERM, EH_SIGNAL );

				/* Modem driver configuration and control file */
				cf->modem_driver   = config_get_item( ctx->config, "modemdriver" );
				cf->control_modem = config_get_item( ctx->config, "control" );

				/* Network driver configuration */
				cf->network_driver = config_get_item( ctx->config, "networkdriver" );

				/* VPN driver configuration and control file */
				cf->vpn_driver     = config_get_item( ctx->config, "vpndriver" );
				cf->control_vpn = config_get_item( ctx->config, "vpncontrol" );
				if( config_istrue( ctx->config, "vpnalways", 0 ) )
					cf->flags |= COORDINATOR_VPN_STANDALONE;

				/* Network test driver and configuration */
				cf->test_driver    = config_get_item( ctx->config, "testdriver" );
				if( ! config_get_timeval( ctx->config, "test_interval", &cf->test_interval ) )
						cf->test_interval = COORDINATOR_TEST_INTERVAL;

				cf->flags |= COORDINATOR_NETWORK_DISABLE|COORDINATOR_VPN_DISABLE;
			}
		case EVENT_START:

			cf->state = COORDINATOR_STATE_RUNNING;

			x_printf(ctx,"calling event ALARM add %d ALARM_INTERVAL (control file check)\n",COORDINATOR_CONTROL_CHECK_INTERVAL);
			cf->timer_fd = event_alarm_add( ctx, COORDINATOR_CONTROL_CHECK_INTERVAL, ALARM_INTERVAL );

			if( cf->test_driver ) {
				x_printf(ctx,"calling event ALARM add %ld ALARM_INTERVAL (%s)\n",cf->test_interval, cf->test_driver);
				cf->test_timer = event_alarm_add( ctx, cf->test_interval, ALARM_INTERVAL );
			}

#ifndef NDEBUG
			x_printf(ctx,"calling event ALARM add %d ALARM_INTERVAL (emergency shutdown)\n",300000);
			event_alarm_add( ctx, 300000, ALARM_INTERVAL );
#endif
			check_control_files( ctx );
			break;

		case EVENT_RESTART:
			if( source == cf->s_unicorn ) {
				if( event_data->event_child.action == CHILD_EVENT ) {
					x_printf(ctx,"Unicorn driver notifying me that the modem driver has restarted.. terminating network driver\n");
					if( cf->s_network ) {
						cf->flags |= COORDINATOR_NETWORK_SHUTDOWN;
						emit( cf->s_network, EVENT_TERMINATE, 0L );
					}
				}
			}
			break;

		case EVENT_CHILD:
			if( child->ctx == cf->s_unicorn ) {
				switch( child->action ) {
					case CHILD_STOPPED:
						x_printf(ctx,"Unicorn driver has exited.  Terminating\n");
						cf->s_unicorn = 0L;
						cf->flags &= ~(unsigned int) (COORDINATOR_MODEM_UP|COORDINATOR_MODEM_ONLINE);

						if( cf->s_network ) {
							cf->flags |= COORDINATOR_NETWORK_SHUTDOWN;
							emit( cf->s_network, EVENT_TERMINATE, 0L );
						}

						if( (cf->flags & COORDINATOR_TERMINATING) && !cf->s_network && !cf->s_vpn )
							context_terminate( ctx );
						break;
					case CHILD_EVENT:
						switch( child->status ) {
							case UNICORN_MODE_ONLINE:
								cf->flags |= COORDINATOR_MODEM_ONLINE;
								if( cf->s_network ) {
									// Restart network driver maybe
									uint8_t sig = SIGKILL;
									driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
									notification.event_custom = &sig;
									cf->flags |= COORDINATOR_NETWORK_SHUTDOWN;
									emit( cf->s_network, EVENT_RESTART, &notification );
								} else {
									start_service( &cf->s_network, cf->network_driver, ctx->config, ctx, 0L );
									if( !cf->s_network )
										cf->flags |= COORDINATOR_TERMINATING;
								}
								break;
							case UNICORN_MODE_OFFLINE:
								cf->flags &= ~(unsigned int)COORDINATOR_MODEM_ONLINE;
								if( cf->s_network ) {
									cf->flags |= COORDINATOR_NETWORK_SHUTDOWN;
									emit( cf->s_network, EVENT_TERMINATE, 0L );
								}
								break;
						}

						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->s_network ) {
				switch( child->action ) {
					case CHILD_STARTED:
						break;
					case CHILD_EVENT:
						cf->flags |= COORDINATOR_NETWORK_UP;
						x_printf(ctx,"NETWORK STATE CHANGED TO UP\n");

						if( !( cf->flags & COORDINATOR_VPN_STANDALONE )) {
							cf->flags |= COORDINATOR_VPN_DISABLE;
							if( cf->s_vpn )
								emit( cf->s_vpn, EVENT_TERMINATE, 0L );
						}

						break;
					case CHILD_STOPPED:
						x_printf(ctx,"NETWORK STATE CHANGED TO DOWN\n");
						cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_UP;
						cf->s_network = 0L;

						if( cf->s_vpn && !(cf->flags & COORDINATOR_VPN_STANDALONE) )
							emit( cf->s_vpn, EVENT_TERMINATE, 0L );

						if( cf->s_unicorn ) {
							if( cf->flags & COORDINATOR_MODEM_ONLINE ) {
								driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
								// If 'COORDINATOR_NETWORK_SHUTDOWN' isn't set, this its and unexpected
								// network termination - let the modem/unicorn driver know
								if( ~cf->flags & COORDINATOR_NETWORK_SHUTDOWN)
									notification.event_custom = (void *)1L;
								emit( cf->s_unicorn, EVENT_RESTART, &notification );
								cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_SHUTDOWN;
							}
						}
						else if (cf->flags & COORDINATOR_TERMINATING)
							context_terminate(ctx);
						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->s_vpn ) {
				switch( child->action ) {
					case CHILD_STARTED:
						cf->flags |= COORDINATOR_VPN_UP;
						break;
					case CHILD_STOPPED:
						cf->flags &= ~(unsigned int)COORDINATOR_VPN_UP;
						cf->s_vpn = 0L;

						cf->flags |= COORDINATOR_VPN_DISABLE;

						if( cf->flags & COORDINATOR_TERMINATING )
							context_terminate(ctx);
						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->s_test ) {
				switch( child->action ) {
					case CHILD_STOPPED:
						x_printf(ctx,"Test driver has exited.\n");
						cf->s_test = 0L;
						cf->flags &= ~(unsigned int) (COORDINATOR_TEST_INPROGRESS);
						break;
					case CHILD_EVENT:
						x_printf(ctx,"Test Driver reported status %08lx\n",child->status);
						if( child->status == 0 ) {
							logger(ctx,"Network test reported failure.  Disconnecting network\n");
							// Flag the network shutdown as intentional
							if( cf->s_network ) {
								cf->flags |= COORDINATOR_NETWORK_SHUTDOWN;
								emit( cf->s_network, EVENT_TERMINATE, 0L );
							}
						}
						break;
					default:
						break;
				}
			}

			break;

		case EVENT_TERMINATE:
			if( cf->s_vpn )
				emit( cf->s_vpn, EVENT_TERMINATE, 0L );

			if( cf->s_network ) {
				// Flag the network shutdown as intentional
				cf->flags |= COORDINATOR_NETWORK_SHUTDOWN;
				emit( cf->s_network, EVENT_TERMINATE, 0L );
			}

			if( cf->s_unicorn )
				emit( cf->s_unicorn, EVENT_TERMINATE, 0L );

			if( cf->s_test )
				emit( cf->s_test, EVENT_TERMINATE, 0L );

			if( cf->state == COORDINATOR_STATE_RUNNING ) {
				cf->flags |= COORDINATOR_TERMINATING;
			} else
				context_terminate( ctx );
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				if( source == cf->s_unicorn && cf->s_network )
					return emit( cf->s_network, event, event_data );
				if( source == cf->s_network && cf->s_unicorn )
					return emit( cf->s_unicorn, event, event_data );
			}
			break;

		case EVENT_SIGNAL:
			if( event_data->event_signal == SIGTERM ) {
				kill(0,SIGTERM);
				exit(0);
			}
			break;

		case EVENT_ALARM:
			{
				if( event_data->event_alarm == cf->timer_fd ) {
					check_control_files(ctx);

					if( cf->flags & COORDINATOR_VPN_STARTING ) {
						x_printf(ctx,"VPN Starting up soon...\n");
						time_t now = rel_time(0L);
						if( (now - cf->vpn_startup_pending) > VPN_STARTUP_DELAY ) {
							cf->flags &= ~(unsigned int)COORDINATOR_VPN_STARTING;
							if( ! start_service( &cf->s_vpn, cf->vpn_driver, ctx->config, ctx, 0L ) ) {
								x_printf(ctx,"Failed to start VPN.  Disabling - this will cause a retry\n");
								cf->flags |= COORDINATOR_VPN_DISABLE;
							}
						}
					}
				}
				else if ( event_data->event_alarm == cf->test_timer ) {
					if( cf->flags & COORDINATOR_NETWORK_UP) {
						if( ~cf->flags & COORDINATOR_TERMINATING)  {
							if( ~cf->flags & COORDINATOR_TEST_INPROGRESS ) {
								cf->flags |= COORDINATOR_TEST_INPROGRESS;
								start_service( &cf->s_test, cf->test_driver, ctx->config, ctx, 0L );
							}
						}
					}
				}
#ifndef NDEBUG
				else {
					x_printf(ctx,"Triggering emergency termination\n");
					kill(0,SIGTERM);
					exit(0);
				}
#endif
			}
			break;

		case EVENT_READ:
			x_printf(ctx,"Got a read event on file descriptor %ld",event_data->event_request.fd);
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
				start_service( &cf->s_unicorn, cf->modem_driver, ctx->config, ctx, 0L );
		}
	} else {
		if( !modem_enable ) {
			cf->flags |= COORDINATOR_NETWORK_DISABLE;
			if( cf->s_network ) {
				cf->flags |= COORDINATOR_NETWORK_SHUTDOWN;
				emit( cf->s_network, EVENT_TERMINATE, 0L );
			}
			if( cf->s_unicorn )
				emit( cf->s_unicorn, EVENT_TERMINATE, 0L );
		}
	}

	if( cf->flags & COORDINATOR_VPN_DISABLE ) {
		//x_printf(ctx,"VPN marked disabled\n");
		if( vpn_enable ) {
			x_printf(ctx,"Control file exists.. enabling\n");
			cf->flags &= ~(unsigned int)COORDINATOR_VPN_DISABLE;
			if( cf->flags & (COORDINATOR_VPN_STANDALONE|COORDINATOR_NETWORK_UP) ) {
				x_printf(ctx,"Setting up to launch vpn service %s after startup delay of %d\n", cf->vpn_driver, VPN_STARTUP_DELAY);
				if( !cf->s_vpn ) {
					cf->flags |= COORDINATOR_VPN_STARTING;
					cf->vpn_startup_pending = rel_time(0L);
				}
			}
		}
	} else {
		if( !vpn_enable ) {
			cf->flags |= COORDINATOR_VPN_DISABLE;
			if( cf->s_vpn )
				emit( cf->s_vpn, EVENT_TERMINATE, 0L );
		}
	}

	return 0;
}
