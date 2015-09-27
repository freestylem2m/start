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

int signal_init(context_t *ctx)
{
#ifdef NDEBUG
	UNUSED(ctx);
#endif
	x_printf(ctx,"Hello from SIGNAL INIT!");

	return 0;
}

int signal_shutdown(context_t *ctx)
{
#ifdef NDEBUG
	UNUSED(ctx);
#endif
	x_printf(ctx,"Goodbye from SIGNAL!\n");
	return 1;
}

ssize_t signal_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_request_t *fd = 0L;
	event_data_t *data = 0L;

#ifndef NDEBUG
	signal_config_t *cf = (signal_config_t *) ctx->data;
#endif
#ifdef NDEBUG
	UNUSED(ctx);
#endif

	x_printf(ctx,"event = \"%s\" (%d)\n *\n *\n", event_map[event], event);

	if( event_data->type == TYPE_FD )
		fd = & event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch( event ) {
		case EVENT_INIT:
			x_printf(ctx, "INIT event triggered\n");
			break;

		case EVENT_TERMINATE:
			x_printf(ctx,"Got a termination event.  Cleaning up\n");
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				x_printf(ctx,"Got a DATA event from my parent...\n");
			} else {
				x_printf(ctx,"Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
			}

			break;

		case EVENT_READ:
			{
				x_printf(ctx,"Read event triggerred for fd = %ld\n",fd->fd);
				size_t bytes;
				event_bytes( (int)fd->fd, &bytes );
				x_printf(ctx,"exec state = %d\n",cf->state );
				if( bytes ) {
					x_printf(ctx,"Read event for fd = %ld (%d bytes)\n",fd->fd, (int)bytes);
					char read_buffer[MAX_READ_BUFFER];

					if( bytes >= MAX_READ_BUFFER ) {
						bytes = MAX_READ_BUFFER-1;
						x_printf(ctx,"WARNING: Truncating read to %d bytes\n",(int)bytes);
					}

					ssize_t result = event_read( (int) fd->fd, read_buffer, bytes);
					x_printf(ctx,"Read event returned %d bytes of data\n",(int)bytes);

					if( result >= 0 ) {
						read_buffer[result] = 0;
						return 0;
					} else
						x_printf(ctx," * WARNING: read return unexpected result %d\n",(int)result);
				} else {

					x_printf(ctx,"EOF on input. Cleaning up\n");
					x_printf(ctx,"event file descriptor (%ld)\n",fd->fd);
				}

			}
			break;

		case EVENT_EXCEPTION:
			x_printf(ctx,"Got an exception on FD %ld\n",fd->fd);
			break;

		case EVENT_SIGNAL:
			x_printf(ctx,"Woa! Got a sign from the gods... %d\n",event_data->event_signal);
			break;

		case EVENT_TICK:
			x_printf(ctx," ** Tick **\n" );
			break;

		default:
			x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
