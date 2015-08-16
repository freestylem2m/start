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
#include <time.h>

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "console.h"

int console_init(context_t *ctx)
{
	d_printf("Hello from CONSOLE(%s) INIT!\n", ctx->name);

	if (0 == (ctx->event = register_event_handler(ctx->name, ctx, console_emit, EH_WANT_TICK))) {
		d_printf("register_event_handler() failed.\n");
		return 0;
	}

	if (0 == (ctx->data = calloc(sizeof(console_config_t), 1))) {
		d_printf("Creating local data failed\n");
		return 0;
	}

	return 1;
}

int console_shutdown(context_t *ctx)
{
	(void)(ctx);
	d_printf("Goodbye from CONSOLE INIT!\n");
	return 1;
}

int console_emit(context_t *ctx, event_t event, driver_data_t *event_data)
{
	fd_list_t      *fd = 0;
	event_data_t *data = 0L;

	console_config_t *cf = (console_config_t *) ctx->data;

	printf("Got an event from %s\n",event_data->source?event_data->source->name:"nowhere");
	printf("event = \"%s\" (%d)\n", event_map[event], event);
	d_printf("event_data = %p\n", event_data);
	d_printf("event_data->type = %s\n", driver_data_type_map[event_data->type]);

	if (event_data && event_data->type == TYPE_FD)
		fd = &event_data->event_fd;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	d_printf("fd = %p\n", fd);

	switch (event) {
		case EVENT_INIT:
			d_printf("CONSOLE INIT event triggered\n");

			// Hard coded STDIO for now.
			cf->fd_in = 0;
			cf->fd_out = 1;

			event_add(ctx->event, cf->fd_in, EH_DEFAULT);
			event_add(ctx->event, SIGQUIT, EH_SIGNAL);
			cf->state = CONSOLE_STATE_RUNNING;
			break;

		case EVENT_TERMINATE:
			d_printf("Got a termination event.  Cleaning up\n");
			close( cf->fd_in );
			cf->flags |= CONSOLE_TERMINATING ;
			event_delete(ctx->event, cf->fd_in, EH_NONE);
			context_terminate(ctx);
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				d_printf("Got a DATA event from my parent...\n");
				d_printf("bytes = %ld\n",data->bytes );
				d_printf("buffer = %s\n",data->data );
				d_printf("buffer[%ld] = %d\n",data->bytes,data->data[data->bytes]);
				if( write( cf->fd_out, data->data, data->bytes ) < 0 )
					d_printf("Failed to forward incoming data\n");
			} else {
				d_printf("Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
			}

			break;

		case EVENT_READ:
			{
				d_printf("Read event triggerred for fd = %d\n", fd->fd);
				size_t          bytes;
				int             rc = event_bytes(fd->fd, &bytes);
#ifdef NDEBUG
				UNUSED(rc);
#endif
				d_printf("event_bytes returned %d (%ld bytes)\n", rc, bytes);
				if (bytes) {
					d_printf("Read event for fd = %d (%ld bytes)\n", fd->fd, bytes);

					char            read_buffer[MAX_READ_BUFFER];

					if( bytes >= MAX_READ_BUFFER ) {
						bytes = MAX_READ_BUFFER-1;
						d_printf("WARNING: Truncating read to %ld bytes\n",bytes);
					}

					ssize_t         result = event_read(fd->fd, read_buffer, bytes);

					if (result >= 0) {
						read_buffer[result] = 0;
						d_printf("Read event returned %ld bytes of data\n", bytes);

						driver_data_t   temp_data = { TYPE_DATA, .source = ctx, .event_data.bytes = bytes,.event_data.data = read_buffer };
						//cf->flags |= CONSOLE_TERMINATING;
						emit_child( ctx, EVENT_DATA_INCOMING, &temp_data );

						//ssize_t written = write(1,read_buffer,bytes);
						//UNUSED(written);
					} else {
						d_printf("read() returned %ld (%s)\n", result, strerror(errno));
					}
				} else {
					d_printf("EOF on file descriptor (%d)\n", fd->fd);
					event_delete(ctx->event, fd->fd, EH_NONE);
					driver_data_t temp_data = *DRIVER_DATA_NONE;
					temp_data.source = ctx;
					emit_child( ctx, EVENT_TERMINATE, &temp_data );
				}

			}
			break;

		case EVENT_EXCEPTION:
			d_printf("Got an exception on FD %d\n", fd->fd);
			break;

		case EVENT_SIGNAL:
			d_printf("Woa! Got a sign from the gods... %d\n", fd->fd);
			break;

		case EVENT_TICK:
			{
				char            buffer[64];
				time_t          now = time(0L);
				strftime(buffer, 64, "%T", localtime(&now));
				d_printf("%s:   ** Tick (%ld seconds) **\n", buffer, cf->last_tick ? time(0L) - cf->last_tick : -1);
				time(&cf->last_tick);
			}
			break;

		default:
			d_printf("\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
