/*
 * File: signal.c
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

#include <driver.h>

#include "netmanage.h"
#include "signal.h"

int signal_init(context_t *context)
{
	UNUSED(context);
	d_printf("Hello from SIGNAL INIT!");

	return 0;
}

int signal_shutdown(context_t *context)
{
	(void)(context);
	d_printf("Goodbye from SIGNAL!\n");
	return 1;
}

int signal_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_request_t *fd = 0L;
	event_data_t *data = 0L;

	signal_config_t *cf = (signal_config_t *) ctx->data;

	d_printf("event = \"%s\" (%d)\n *\n *\n", event_map[event], event);

	if( event_data->type == TYPE_FD )
		fd = & event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch( event ) {
		case EVENT_INIT:
			d_printf( "INIT event triggered\n");
			break;

		case EVENT_TERMINATE:
			d_printf("Got a termination event.  Cleaning up\n");
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				d_printf("Got a DATA event from my parent...\n");
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
						return 0;
					} else
						d_printf(" * WARNING: read return unexpected result %ld\n",result);
				} else {

					d_printf("EOF on input. Cleaning up\n");
					d_printf("event file descriptor (%d)\n",fd->fd);
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
				d_printf("%s:   ** Tick **\n", buffer );
			}
			break;

		default:
			d_printf("\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
