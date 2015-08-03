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

int console_init(context_t *context)
{
	d_printf("Hello from CONSOLE(%s) INIT!\n",context->name);

	if ( 0 == (context->event = register_event_handler( context->name, context, console_emit, EH_WANT_TICK ))) {
		d_printf("register_event_handler() failed.\n");
		return 0;
	}

	if ( 0 == (context->data = calloc( sizeof( console_config_t ) , 1 ))) {
		d_printf("Creating local data failed\n");
		return 0;
	}

	return 1;
}

int console_shutdown( context_t *context)
{
	(void)(context);
	d_printf("Goodbye from CONSOLE INIT!\n");
	return 1;
}

int console_emit(context_t *context, event_t event, void *event_data )
{
	UNUSED(context);
	UNUSED(event_data);

	console_config_t *cf = (console_config_t *) context->data;
	fd_list_t *fd = (fd_list_t *)event_data;

	switch( event ) {
		case EVENT_INIT:
			d_printf( "CONSOLE INIT event triggered\n");

			event_add( context->event, 0, EH_DEFAULT );
			event_add( context->event, SIGQUIT, EH_SIGNAL );
			cf->state = CONSOLE_RUNNING;
			break;

		case EVENT_READ:
			{
				d_printf("Read event triggerred for fd = %d\n",fd->fd);
				size_t bytes;
				int rc = event_bytes( fd->fd, &bytes );
				d_printf("event_bytes returned %d (%ld bytes)\n",rc,bytes);
				if( bytes ) {
					d_printf("Read event for fd = %d (%ld bytes)\n",fd->fd, bytes);
					char read_buffer[MAX_READ_BUFFER];

					ssize_t result = event_read( fd->fd, read_buffer, bytes);

					if( result >= 0 ) {
						d_printf("Read event returned %ld bytes of data\n",bytes);

						ssize_t written = write(1,read_buffer,bytes);
						UNUSED(written);
					} else {
						d_printf("read() returned %ld (%s)\n",result,strerror(errno));
					}
				} else {
					d_printf("EOF on file descriptor (%d)\n",fd->fd);
					event_delete( context->event, fd->fd, EH_NONE );
					event_delete( context->event, SIGQUIT, EH_SIGNAL );
				}

			}
			break;

		case EVENT_EXCEPTION:
			d_printf("Got an exception on FD %d\n",fd->fd);
			break;

		case EVENT_SIGNAL:
			d_printf("Woa! Got a sign from the gods... %d\n",fd->fd);
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
			d_printf("Emitted some kind of event (%d)\n",event);
	}
	return 0;
}
