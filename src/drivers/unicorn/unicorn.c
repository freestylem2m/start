/*
 * File: unicorn.c
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
#include "unicorn.h"

int unicorn_init(context_t * ctx)
{
	d_printf("Hello from UNICORN(%s) INIT!\n", ctx->name);

	unicorn_config_t *cf ;

	if (0 == (cf = (unicorn_config_t *) calloc( sizeof( unicorn_config_t ) , 1 )))
		return 0;

	u_ringbuf_init(&cf->input);
	ctx->data = cf;

	return 1;
}

int unicorn_shutdown(context_t * ctx)
{
	d_printf("Goodbye from UNICORN INIT!\n");
	if( ctx->data )
		free( ctx->data );
	ctx->data = 0;

	return 1;
}

ssize_t send_unicorn_command( context_t *ctx, cmdHost_t cmd, cmdState_t state, unsigned int length, void *data )
{
	unicorn_config_t *cf = ctx->data;

	driver_data_t notification = { TYPE_DATA, ctx, {} };

	frmHdr_t *frame = alloca( sizeof( frmHdr_t ) + length );

	if( !frame )
		return -1;

	frame->magicNo = MAGIC_NUMBER;
	frame->cmd = cmd;
	frame->state = state;
	frame->length = (uint16_t) length;

	if( length ) 
		memmove( &frame[1], data, length );

	notification.event_data.data = frame;
	notification.event_data.bytes = sizeof( frmHdr_t ) + length;

	return emit( cf->modem, EVENT_DATA_OUTGOING, &notification );
}

ssize_t process_unicorn_data( context_t *ctx )
{
	unicorn_config_t *cf = ctx->data;

	size_t avail = u_ringbuf_ready( &cf->input );

	if( cf->state & UNICORN_EXPECTING_DATA ) {
		d_printf("Expecting %d bytes of data.. buffer contains %d\n",
				cf->msgHdr.length, avail );
	} else {
		d_printf("Check if there is a header in the buffer\n");
		if( avail > sizeof( frmHdr_t ) ) {
			ssize_t rc = u_ringbuf_read( &cf->input, & cf->msgHdr, sizeof( cf->msgHdr ) );
			d_printf("ringbuf read returned %d bytes\n",rc);
			d_printf("magic = %X\n",cf->msgHdr.magicNo );
			if( MAGIC_NUMBER != cf->msgHdr.magicNo ) {
				d_printf("MAGIC NUMBER FAIL... tossing the baby out with the bathwater..\n");
				u_ringbuf_init( &cf->input );
				return 0;
			}

			if( cf->msgHdr.length ) {
				cf->state |= UNICORN_EXPECTING_DATA;
			} else {
				// Record the time, so I don't send unnecessary state requests
				cf->last_tick = time(0L);
				// handle notifications with no data
				printf("Cot CMD   %d\n",cf->msgHdr.cmd );
				printf("Cot STATE %d\n",cf->msgHdr.state );

				switch( cf->msgHdr.cmd ) {
					case CMD_READY:
						// trigger a dialup
						d_printf("Sending CONNECT command\n");
						send_unicorn_command(ctx, CMD_CONNECT, CMD_ST_ONLINE, 0, 0 );
						cf->driver_state = cf->msgHdr.state;
						break;

					default:
						d_printf("Not ready to deal with cmd %d\n",cf->msgHdr.cmd);

				}
			}
		}
	}

	return 0;
}

ssize_t unicorn_handler(context_t *ctx, event_t event, driver_data_t *event_data)
{
	event_request_t *fd = 0;
	event_data_t   *data = 0L;

	unicorn_config_t *cf = (unicorn_config_t *) ctx->data;

	d_printf("event = \"%s\" (%d)\n", event_map[event], event);
	//d_printf("event_data = %p\n", event_data);
	//d_printf("event_data->type = %s\n", driver_data_type_map[event_data->type]);

	if (event_data && event_data->type == TYPE_FD)
		fd = &event_data->event_request;
	else if (event_data->type == TYPE_DATA)
		data = &event_data->event_data;

	//d_printf("fd = %p\n", fd);

	switch (event) {
	case EVENT_INIT:
		d_printf("UNICORN INIT event triggered\n");

		event_add( ctx, SIGQUIT, EH_SIGNAL );
		event_add( ctx, 0, EH_WANT_TICK );

		const char *endpoint = config_get_item( ctx->config, "endpoint" );
		if( endpoint )
			cf->modem = start_service( endpoint, ctx->config, ctx );

#if 0
		if( ctx->owner ) {
			driver_data_t child_event = { TYPE_CHILD, ctx, {} };
			child_event.event_child.ctx = ctx;
			child_event.event_child.status = 0;
			emit( ctx->owner, EVENT_CHILD, &child_event );
		}
#endif

		cf->state = UNICORN_STATE_RUNNING;
		break;

	case EVENT_CHILD:
#if 0
		if( event_data->source == cf->modem ) {
			d_printf("Modem driver restarting...  flush buffers and reset state\n");
			cf->flags &= ~(unsigned int)UNICORN_EXPECTING_DATA;
			u_ringbuf_init( &cf->input );
			cf->last_tick = time(0L);
		}
#endif
		break;

	case EVENT_TERMINATE:
		d_printf("Got a termination event.  Cleaning up\n");
		cf->flags |= UNICORN_TERMINATING;
		context_terminate(ctx);
		break;

	case EVENT_DATA_INCOMING:
	case EVENT_DATA_OUTGOING:
		if (data) {
			if( event_data->source == cf->modem ) {
				d_printf("Got a DATA event from the modem driver...\n");
				//d_printf("bytes = %ld\n", data->bytes);
				//d_printf("buffer = %s\n", (char *)data->data);
				//d_printf("buffer[%ld] = %d\n", data->bytes, ((char *)data->data)[data->bytes]);

				size_t bytes = data->bytes;
				size_t offset = 0;

				while( bytes ) {
					size_t to_read = u_ringbuf_avail( &cf->input );
					if( to_read > bytes )
						to_read = bytes;
					else
						d_printf("Got record from %s, but not enough space to store it\n", event_data->source->name);

					d_printf("moving %d bytes of input data to the ring buffer\n", to_read);
					u_ringbuf_write( &cf->input, &((char *)data->data)[offset], to_read );

					bytes -= to_read;
					offset += to_read;

					// process some ring buffer data
					process_unicorn_data(ctx);
				}

				return (ssize_t) offset;
			}
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
			d_printf("event_bytes returned %d (%d bytes)\n", rc, bytes);
			if (bytes) {
				d_printf("Read event for fd = %d (%d bytes)\n", fd->fd, bytes);

				char            read_buffer[MAX_READ_BUFFER];

				if (bytes >= MAX_READ_BUFFER) {
					bytes = MAX_READ_BUFFER - 1;
					d_printf("WARNING: Truncating read to %d bytes\n", bytes);
				}

				ssize_t         result = event_read(fd->fd, read_buffer, bytes);

				if (result >= 0) {
					read_buffer[result] = 0;
					d_printf("Read event returned %d bytes of data\n", bytes);
					cf->flags |= UNICORN_TERMINATING;
				} else {
					d_printf("read() returned %d (%s)\n", result, strerror(errno));
				}
			} else {
				d_printf("EOF on file descriptor (%d)\n", fd->fd);
				event_delete(ctx, fd->fd, EH_NONE);
			}

		}
		break;

	case EVENT_EXCEPTION:
		d_printf("Got an exception on FD %d\n", fd->fd);
		break;

	case EVENT_SIGNAL:
		d_printf("Woa! Got a sign from the gods... %d\n", event_data->event_signal);
		if( event_data->event_signal == SIGQUIT ) {
			if( cf->modem )
				emit(cf->modem, EVENT_TERMINATE, 0L);
			event_delete( ctx, SIGQUIT, EH_SIGNAL );
			context_terminate( ctx );
		}
		break;

	case EVENT_TICK:
		{
			time_t now = time(0L);

			if( (now - cf->last_tick) > 120 ) {
				frmHdr_t frame = {  MAGIC_NUMBER, CMD_STATE, 0, 0 };
				driver_data_t notification = { TYPE_DATA, ctx, {} };
				notification.event_data.data = &frame;
				notification.event_data.bytes = sizeof( frame );

				emit( cf->modem, EVENT_DATA_OUTGOING, &notification );
				cf->last_tick = now;
			}
		}
		break;

	default:
		d_printf("\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
