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
#include <time.h>

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "coordinator.h"
#include "logger.h"
#include "unicorn.h"

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
				event_add( ctx, SIGQUIT, EH_SIGNAL );
				event_add( ctx, SIGINT, EH_SIGNAL );
				event_add( ctx, SIGTERM, EH_SIGNAL );
				event_add( ctx, 0, EH_WANT_TICK );

				d_printf("config = %s\n",ctx->config->section);
				const char *log = config_get_item( ctx->config, "logger" );
				cf->modem_driver = config_get_item( ctx->config, "modemdriver" );
				cf->network_driver = config_get_item( ctx->config, "networkdriver" );

				d_printf("logger = %s\n",log );

                if( log )
                    cf->logger = start_service( log, ctx->config, ctx );

				if( cf->modem_driver )
					cf->unicorn = start_service( cf->modem_driver, ctx->config, ctx );

                if( ! cf->logger ) {
                    fprintf(stderr,"Failed to start the logger process\n");
                    exit (0);
                }

				cf->state = COORDINATOR_STATE_RUNNING;
			}
			break;

		case EVENT_LOGGING:
			// Forward logging messages to the logger
			emit( cf->logger, event, event_data );
			break;

		case EVENT_RESTART:
			if( source == cf->unicorn ) {
				if( event_data->event_child.action == CHILD_EVENT ) {
					x_printf(ctx,"Unicorn driver notifying me that the modem driver has restarted.. terminating network driver\n");
					emit( cf->network, EVENT_TERMINATE, 0L );
				}
			}
			break;

		case EVENT_CHILD:
			d_printf("Got a message from a child (%s:%d).. probably starting\n", child->ctx->name, child->action);
			if( cf->logger )
				logger( cf->logger, ctx, "Child %s entered state %d\n", child->ctx->name, child->action );

			if( child->ctx == cf->unicorn ) {
				if( child->action == CHILD_STOPPED ) {
					x_printf(ctx,"Unicorn driver has exited.  Terminating\n");
					cf->unicorn = 0L;
					cf->flags &= ~(unsigned int)COORDINATOR_MODEM_UP;
					context_terminate( ctx );
				} else if( child->action == CHILD_EVENT ) {
					x_printf(ctx,"Got CHILD_EVENT from %s:  event = %d\n",child->ctx->name, child->status );
					if( child->status == UNICORN_MODE_ONLINE ) {
						cf->flags |= COORDINATOR_MODEM_ONLINE;
						if( cf->network ) {
							// Restart network driver maybe
							d_printf("Modem has come online.  network driver is already running.. It should probably be restart\n");
							emit( cf->network, EVENT_RESTART, 0L );
							// this probably won't restart pppd automatically yet...
						} else {
							d_printf("Calling start_service(%s)\n",cf->network_driver);
							cf->network = start_service( cf->network_driver, ctx->config, ctx );
							if( !cf->network ) {
								d_printf("Network driver %s failed to start. Abandoning.\n", cf->network_driver);
								cf->flags |= COORDINATOR_TERMINATING;
							} else {
								d_printf("Network driver %s is started\n",cf->network_driver );
								printf("cf->network->name = %s\n",cf->network->name );
							}
						}
					} else {
						cf->modem_timestamp = time(0L);
						cf->flags &= ~(unsigned int)COORDINATOR_MODEM_ONLINE;
						if( cf->network ) {
							// Terminate existing network driver
							d_printf("Modem driver reports connection down.  Terminating network driver\n");
							emit( cf->network, EVENT_TERMINATE, 0L );
						}
						d_printf("Unicord driver state updated to %s\n", child->status == UNICORN_MODE_ONLINE?"online":"offline");
					}
				}
			} else if( child->ctx == cf->network ) {
				x_printf(ctx,"Notification from network driver\n");
				if( child->action == CHILD_STOPPED ) {
					x_printf(ctx,"Network drive Down\n");
					cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_UP;
					cf->network = 0L;
					// why did it stop?  killed? disconnecting? network down?
					driver_data_t notification = { TYPE_CHILD, ctx, {} };
					notification.event_child.ctx = ctx;
					notification.event_child.action = CHILD_EVENT;
					notification.event_child.status = 0;
					emit(cf->unicorn, EVENT_RESTART, &notification );
				} else if( child->action == CHILD_STARTED ) {
					x_printf(ctx,"Network drive UP \n");
					cf->flags |= COORDINATOR_NETWORK_UP;
				}
			}
			break;

		case EVENT_TERMINATE:
			d_printf("Got a termination event.  Cleaning up\n");
			d_printf("child process will get EOF..\n");
			
		
			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );

			if( cf->unicorn )
				emit( cf->network, EVENT_TERMINATE, 0L );
		
			// If there is no child process, don't wait for it to terminate
			cf->termination_timestamp = time(0L);

			if( cf->state == COORDINATOR_STATE_RUNNING ) {
				cf->flags |= COORDINATOR_TERMINATING;
			} else
				context_terminate( ctx );
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				x_printf(ctx,"Got a DATA event from %s ...\n",source->name);
				x_printf(ctx,"bytes = %d\n",data->bytes );
				x_printf(ctx,"buffer = %s\n", (char *) data->data );
				x_printf(ctx,"buffer[%d] = %d\n",data->bytes,((char *)data->data)[data->bytes]);
				if( source == cf->unicorn ) {
					x_printf(ctx, "Forwarding unicorn data to network driver\n");
					if( cf->network )
						emit( cf->network, event, event_data );
					else
						x_printf(ctx, "... but network driver is down..\n");
				} else if( source == cf->network ) {
					x_printf(ctx, "Forwarding network data to unicorn driver\n");
					if( cf->unicorn )
						emit( cf->unicorn, event, event_data );
					else
						x_printf(ctx, "... but unicorn driver is down..\n");
				}
			} else {
				d_printf("Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
			}

			break;

		case EVENT_READ:
#if 0
			{
				event_request_t *fd = & event_data->event_request;
				d_printf("read event here..\n");
				char temp;
				size_t sp = 0;
				event_bytes( fd->fd, &sp );
				if( sp > 0 )
					read(fd->fd , &temp, 1);
				else {
					d_printf("EOF on stdin...\n");
					kill(0,SIGKILL);
				}
			}
#endif
			break;

		case EVENT_EXCEPTION:
			break;

		case EVENT_SIGNAL:
			d_printf("Woa! Got a sign from the gods... %d\n",event_data->event_signal);
			if( event_data->event_signal == SIGTERM ) {
				kill(0,SIGKILL);
			}
			break;

		case EVENT_TICK:
			{
				char buffer[64];
				time_t now = time(0L);
				strftime(buffer,64,"%T",localtime(&now));
				//d_printf("%s:   ** Tick (%ld seconds) **\n", buffer, cf->last_tick ? time(0L)-cf->last_tick : -1);
				time( & cf->last_tick );
				if( cf->flags & COORDINATOR_TERMINATING ) {
					d_printf("Been terminating for %ld seconds...\n",time(0L) - cf->termination_timestamp );
					if(( time(0L) - cf->termination_timestamp ) > (PROCESS_TERMINATION_TIMEOUT*2) ) {
						d_printf("REALLY Pushing it along with a SIGKILL\n");
					} else if(( time(0L) - cf->termination_timestamp ) > PROCESS_TERMINATION_TIMEOUT ) {
						d_printf("Pushing it along with a SIGTERM\n");
					}
				}
			}
			break;

        case EVENT_NONE:
        case EVENT_WRITE:
        case EVENT_MAX:
        default:
            d_printf("\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
    }
    return 0;
}
