/*
 * File: logger.c
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
#include "logger.h"

int logger_init(context_t *context)
{
	d_printf("Hello from LOGGER INIT!\n");

	// register "emit" as the event handler of choice
	
	logger_config_t *cf;

	if ( 0 == (cf = (logger_config_t *) calloc( sizeof( logger_config_t ) , 1 )))
		return 0;

	cf->state = LOGGER_STATE_IDLE;

	if( config_istrue( context->config, "respawn" ) )
		cf->flags |= LOGGER_RESPAWN;

	context->data = cf;

	return 1;
}

int logger_shutdown(context_t *context)
{
	(void)(context);
	d_printf("Goodbye from LOGGER!\n");
	return 1;
}

#define CMD_ARGS_MAX 32
#define PATH_MAX 512

#define MAX_READ_BUFFER 1024
int logger_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_request_t *fd = 0L;
	event_data_t *data = 0L;

	logger_config_t *cf = (logger_config_t *) ctx->data;

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
			}
			break;

		case EVENT_TERMINATE:
			d_printf("Got a termination event.  Cleaning up\n");
			d_printf("child process will get EOF..\n");

			if( cf->state == LOGGER_STATE_RUNNING ) {
				event_delete( ctx, cf->fd_out, EH_NONE );
				close( cf->fd_out );
				cf->flags |= LOGGER_TERMINATING;
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
				if( write( cf->fd_out, data->data, data->bytes ) < 0)
					d_printf("Failed to forward incoming data\n");
			} else {
				d_printf("Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
			}

			break;

		case EVENT_READ:
			{
				d_printf("Read event triggerred for fd = %d\n",fd->fd);
				size_t bytes;
				event_bytes( fd->fd, &bytes );
				d_printf("exec state = %d\n",cf->state );
				if( bytes ) {
					d_printf("Read event for fd = %d (%ld bytes)\n",fd->fd, bytes);
					char read_buffer[MAX_READ_BUFFER];

					if( bytes >= MAX_READ_BUFFER ) {
						bytes = MAX_READ_BUFFER-1;
						d_printf("WARNING: Truncating read to %ld bytes\n",bytes);
					}

					ssize_t result = event_read( fd->fd, read_buffer, bytes);
					d_printf("Read event returned %ld bytes of data\n",bytes);

					if( result >= 0 ) {
						read_buffer[result] = 0;
					} else
						d_printf(" * WARNING: read return unexpected result %ld\n",result);
				} else {

					d_printf("EOF on input. Cleaning up\n");
					d_printf("read = %d\n",cf->fd_in);
					d_printf("write = %d\n",cf->fd_out);
					d_printf("event file descriptor (%d)\n",fd->fd);

					if( fd->fd == cf->fd_in ) {
						d_printf("\n *\n * child program has terminated\n *\n");
						d_printf("Current state = %d\n",cf->state );

						event_delete( ctx, cf->fd_in, EH_NONE );
						close( cf->fd_in );

						event_delete( ctx, cf->fd_out, EH_NONE );
						close( cf->fd_out );

						// Complete termination is a two step process.
						// 1) file descriptor event causing the closure of all file descriptors and disabling of file events
						// 2) sigchld event causing the reaping of the child and disabling of further sigchild events.
						if( cf->state == LOGGER_STATE_STOPPING ) {
							// program termination already signalled
							if( (cf->flags & (LOGGER_RESPAWN|LOGGER_TERMINATING)) == LOGGER_RESPAWN ) {
								d_printf("attempting to respawn\n");
								cf->state = LOGGER_STATE_IDLE;
								return emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
							} else {
								d_printf("done here. cleaning up.\n");
								context_terminate( ctx );
								return 0;
							}
						}
						cf->state = LOGGER_STATE_STOPPING;
					}
				}

			}
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
			}
			break;

		default:
			d_printf("\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
