/*
 * File: console.c
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
#include "console.h"
#include "clock.h"

int console_init(context_t *ctx)
{
	x_printf(ctx,"Hello from CONSOLE(%s) INIT!\n", ctx->name);

	if (0 == (ctx->data = calloc(sizeof(console_config_t), 1))) {
		x_printf(ctx,"Creating local data failed\n");
		return 0;
	}

	return 1;
}

int console_shutdown(context_t *ctx)
{
	x_printf(ctx,"Goodbye from CONSOLE INIT!\n");

	if( ctx->data )
		free( ctx->data );

	return 0;
}

ssize_t console_handler(context_t *ctx, event_t event, driver_data_t *event_data)
{
	event_request_t *fd = 0;
	event_data_t *data = 0L;

	console_config_t *cf = (console_config_t *) ctx->data;

	x_printf(ctx,"Got an event from %s\n",event_data->source?event_data->source->name:"nowhere");
	x_printf(ctx,"event = \"%s\" (%d)\n", event_map[event], event);
	x_printf(ctx,"event_data = %p\n", event_data);
	x_printf(ctx,"event_data->type = %s\n", driver_data_type_map[event_data->type]);

	if (event_data && event_data->type == TYPE_FD)
		fd = &event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	x_printf(ctx,"fd = %p\n", fd);

	switch (event) {
		case EVENT_INIT:
			x_printf(ctx,"CONSOLE INIT event triggered\n");

			// Hard coded STDIO for now.
			cf->fd_in = 0;
			cf->fd_out = 1;

			event_add(ctx, cf->fd_in, EH_DEFAULT);
			event_add(ctx, SIGQUIT, EH_SIGNAL);
			cf->state = CONSOLE_STATE_RUNNING;
			break;

		case EVENT_TERMINATE:
			x_printf(ctx,"Got a termination event.  Cleaning up\n");
			close( cf->fd_in );
			cf->flags |= CONSOLE_TERMINATING ;
			event_delete(ctx, cf->fd_in, EH_NONE);
			context_terminate(ctx);
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				x_printf(ctx,"Got a DATA event from my parent...\n");
				x_printf(ctx,"bytes = %d\n",data->bytes );
				x_printf(ctx,"buffer = %s\n", (char *)data->data );
				x_printf(ctx,"buffer[%d] = %d\n",data->bytes,((char *)data->data)[data->bytes]);
				if( write( cf->fd_out, data->data, data->bytes ) < 0 )
					x_printf(ctx,"Failed to forward incoming data\n");
			} else {
				x_printf(ctx,"Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
			}

			break;

		case EVENT_READ:
			{
				x_printf(ctx,"Read event triggerred for fd = %d\n", fd->fd);
				size_t          bytes;
				int             rc = event_bytes(fd->fd, &bytes);
#ifdef NDEBUG
				UNUSED(rc);
#endif
				x_printf(ctx,"event_bytes returned %d (%d bytes)\n", rc, bytes);
				if (bytes) {
					x_printf(ctx,"Read event for fd = %d (%d bytes)\n", fd->fd, bytes);

					char            read_buffer[MAX_READ_BUFFER];

					if( bytes >= MAX_READ_BUFFER ) {
						bytes = MAX_READ_BUFFER-1;
						x_printf(ctx,"WARNING: Truncating read to %d bytes\n",bytes);
					}

					ssize_t         result = event_read(fd->fd, read_buffer, bytes);

					if (result >= 0) {
						read_buffer[result] = 0;
						x_printf(ctx,"Read event returned %d bytes of data\n", bytes);
					} else {
						x_printf(ctx,"read() returned %d (%s)\n", result, strerror(errno));
					}
				} else {
					x_printf(ctx,"EOF on file descriptor (%d)\n", fd->fd);
					event_delete(ctx, fd->fd, EH_NONE);
					context_terminate(ctx);
				}

			}
			break;

		case EVENT_EXCEPTION:
			x_printf(ctx,"Got an exception on FD %d\n", fd->fd);
			break;

		case EVENT_SIGNAL:
			x_printf(ctx,"Woa! Got a sign from the gods... %d\n", fd->fd);
			break;

		case EVENT_TICK:
			{
				char            buffer[64];
				time_t          now = rel_time(0L);
				strftime(buffer, 64, "%T", localtime(&now));
				x_printf(ctx,"%s:   ** Tick (%ld useconds) **\n", buffer, cf->last_tick ? rel_time(0L) - cf->last_tick : -1);
				rel_time(&cf->last_tick);
			}
			break;

		default:
			x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
