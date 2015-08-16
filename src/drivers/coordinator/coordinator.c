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

int coordinator_init(context_t *context)
{
	d_printf("Hello from COORDINATOR INIT!\n");

	coordinator_config_t *cf;

	if ( 0 == (cf = (coordinator_config_t *) calloc( sizeof( coordinator_config_t ) , 1 )))
		return 0;

	cf->state = COORDINATOR_STATE_IDLE;

	if( config_istrue( context->config, "respawn" ) )
		cf->flags |= COORDINATOR_RESPAWN;

	context->data = cf;

	return 1;
}

int coordinator_shutdown( context_t *context)
{
	(void)(context);
	d_printf("Goodbye from EXEC!\n");
	return 1;
}

#define MAX_READ_BUFFER 1024
int coordinator_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_request_t *fd = 0L;
	event_data_t *data = 0L;

	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	d_printf("event = \"%s\" (%d)\n *\n *\n", event_map[event], event);

	if( event_data->type == TYPE_FD )
		fd = & event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch( event ) {
		case EVENT_INIT:
			d_printf( "INIT event triggered\n");
			{
				event_add( ctx, SIGQUIT, EH_SIGNAL );
				event_add( ctx, SIGCHLD, EH_SIGNAL );

				cf->state = COORDINATOR_STATE_RUNNING;
			}
			break;

		case EVENT_TERMINATE:
			d_printf("Got a termination event.  Cleaning up\n");
			d_printf("child process will get EOF..\n");
			// If there is no child process, don't wait for it to terminate
			cf->termination_timestamp = time(0L);

			if( cf->state == COORDINATOR_STATE_RUNNING ) {
				cf->flags |= COORDINATOR_TERMINATING;
			} else {
				context_terminate( ctx );
			}
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				d_printf("Got a DATA event from my parent...\n");
				d_printf("bytes = %ld\n",data->bytes );
				d_printf("buffer = %s\n",data->data );
				d_printf("buffer[%ld] = %d\n",data->bytes,data->data[data->bytes]);
			} else {
				d_printf("Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
			}

			break;

		case EVENT_READ:
			d_printf("Read event triggerred for fd = %d\n",fd->fd);
			break;

		case EVENT_EXCEPTION:
			d_printf("Got an exception on FD %d\n",fd->fd);
			break;

		case EVENT_SIGNAL:
			d_printf("Woa! Got a sign from the gods... %d\n",event_data->event_signal);
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

		default:
			d_printf("\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
