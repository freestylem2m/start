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
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "console.h"
#include "logger.h"
#include "clock.h"

int console_init(context_t *ctx)
{
	if (0 == (ctx->data = calloc(sizeof(console_config_t), 1)))
		return 0;

	return 1;
}

int console_shutdown(context_t *ctx)
{
	console_config_t *cf = (console_config_t *) ctx->data;

	if( cf->state == CONSOLE_STATE_RUNNING )
		if( cf->pty >= 0 ) {
			ioctl( cf->pty, TIOCMSET, &cf->pty_config );
			close( cf->pty );
		}

	free( cf );
	return 0;
}

ssize_t console_handler(context_t *ctx, event_t event, driver_data_t *event_data)
{
	event_request_t *fd = 0;
	event_data_t *data = 0L;

	console_config_t *cf = (console_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	if (event_data && event_data->type == TYPE_FD)
		fd = &event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch (event) {
		case EVENT_INIT:
			cf->pty = open( "/dev/tty", O_RDWR|O_NOCTTY|O_NONBLOCK );

			event_add(ctx, cf->pty, EH_DEFAULT);
			event_add(ctx, SIGWINCH, EH_SIGNAL);

			ioctl(cf->pty, TIOCMGET, &cf->pty_config );
			ioctl(cf->pty, TIOCGWINSZ, &cf->pty_size );

			cf->state = CONSOLE_STATE_RUNNING;

		case EVENT_START:
			break;

		case EVENT_TERMINATE:
			event_delete(ctx, cf->pty, EH_NONE);
			context_terminate(ctx);
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( write( cf->pty, data->data, data->bytes ) != (ssize_t) (data->bytes) )
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

					emit2(ctx, EVENT_DATA_INCOMING, &notification );
				} else {
					event_delete(ctx, fd->fd, EH_NONE);
					context_terminate(ctx);
				}

			}
			break;

		case EVENT_SIGNAL:
			if( event_data->event_signal == SIGWINCH ) {
				ioctl(cf->pty, TIOCGWINSZ, &cf->pty_size );
				context_owner_notify( ctx, CHILD_EVENT, CHILD_EVENT_WINSIZE);
			}
			break;

		default:
			break;
	}

	return 0;
}
