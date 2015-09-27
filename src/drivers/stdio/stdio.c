/*
 * File: stdio.c
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
#include "stdio.h"
#include "clock.h"

int stdio_init(context_t *ctx)
{
	if (0 == (ctx->data = calloc(sizeof(stdio_config_t), 1)))
		return 0;

	return 1;
}

int stdio_shutdown(context_t *ctx)
{
	if( ctx->data )
		free( ctx->data );

	return 0;
}

ssize_t stdio_handler(context_t *ctx, event_t event, driver_data_t *event_data)
{
	event_request_t *fd = 0;
	event_data_t *data = 0L;

	stdio_config_t *cf = (stdio_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	if (event_data && event_data->type == TYPE_FD)
		fd = &event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch (event) {
		case EVENT_INIT:
			cf->fd_in = 0;
			cf->fd_out = 1;

			x_printf(ctx,"calling event add %d EH_DEFAULT\n",cf->fd_in);
			event_add(ctx, cf->fd_in, EH_DEFAULT);

			cf->state = STDIO_STATE_RUNNING;
			break;

		case EVENT_TERMINATE:
			close( cf->fd_in );
			cf->flags |= STDIO_TERMINATING ;

			event_delete(ctx, cf->fd_in, EH_NONE);
			context_terminate(ctx);
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( write( cf->fd_out, data->data, data->bytes ) < 0 )
				x_printf(ctx,"Failed to forward outgoing data\n");
			break;

		case EVENT_READ:
			{
				size_t          bytes;
				event_bytes((int)fd->fd, &bytes);

				if (bytes) {
					char            read_buffer[MAX_READ_BUFFER];

					if( bytes >= MAX_READ_BUFFER )
						bytes = MAX_READ_BUFFER-1;

					ssize_t         result = event_read((int)fd->fd, read_buffer, bytes);

					if (result >= 0)
						read_buffer[result] = 0;

					driver_data_t notification = { TYPE_DATA, ctx, {} };
					notification.event_data.data = read_buffer;
					notification.event_data.bytes = (size_t) result;
					emit(ctx->owner, EVENT_DATA_INCOMING, &notification );
				} else {
					event_delete(ctx, fd->fd, EH_NONE);
					context_terminate(ctx);
				}

			}
			break;

		default:
			break;
	}

	return 0;
}
