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

	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	d_printf("> Event = \"%s\" (%d)\n", event_map[event], event);

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

				const char *log = config_get_item( ctx->config, "logger" );
				const char *unicorn = config_get_item( ctx->config, "modemdriver" );

                if( log )
                    cf->logger = start_service( log, ctx->config, ctx );

				if( unicorn )
					cf->unicorn = start_service( unicorn, ctx->config, ctx );

                if( ! cf->logger ) {
                    fprintf(stderr,"Failed to start the logger process\n");
                    exit (0);
                }

				cf->state = COORDINATOR_STATE_RUNNING;
			}
			break;

		case EVENT_CHILD:
			d_printf("Got a message from a child (%s).. probably starting\n", child->ctx->name);
			break;

		case EVENT_TERMINATE:
			d_printf("Got a termination event.  Cleaning up\n");
			d_printf("child process will get EOF..\n");
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
				d_printf("Got a DATA event from my parent...\n");
				d_printf("bytes = %d\n",data->bytes );
				d_printf("buffer = %s\n", (char *) data->data );
				d_printf("buffer[%d] = %d\n",data->bytes,((char *)data->data)[data->bytes]);
			} else {
				d_printf("Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
			}

			break;

		case EVENT_READ:
			d_printf("tick..");
			break;

		case EVENT_EXCEPTION:
			break;

		case EVENT_SIGNAL:
			d_printf("Woa! Got a sign from the gods... %d\n",event_data->event_signal);
			kill(0,SIGKILL);
			break;

		case EVENT_TICK:
			{
				char buffer[64];
				time_t now = time(0L);
				strftime(buffer,64,"%T",localtime(&now));
				d_printf("%s:   ** Tick (%ld seconds) **\n", buffer, cf->last_tick ? time(0L)-cf->last_tick : -1);
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