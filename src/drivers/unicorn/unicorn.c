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

	cf->last_message = time(0L);
	cf->driver_state = 0xFFFF;

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
	int rc = -1;

	size_t ready = u_ringbuf_ready( &cf->input );
	d_printf("Buffer currently has %d bytes\n",ready);

	if( cf->flags & UNICORN_EXPECTING_DATA ) {
		d_printf("Expecting %d bytes of data.. buffer contains %d\n", cf->msgHdr.length, ready );
	} else {
		d_printf("Check if there is a header in the buffer\n");
		if( ready >= sizeof( frmHdr_t ) ) {
			ssize_t _rc = u_ringbuf_read( &cf->input, & cf->msgHdr, sizeof( cf->msgHdr ) );
#ifdef NDEBUG
			UNUSED(_rc);
#endif
			d_printf("ringbuf read returned %d bytes\n",_rc);
			d_printf("magic = %X\n",cf->msgHdr.magicNo );
			if( MAGIC_NUMBER != cf->msgHdr.magicNo ) {
				d_printf("MAGIC NUMBER FAIL... tossing the baby out with the bathwater..\n");
				u_ringbuf_init( &cf->input );
				cf->flags &= ~(unsigned int)UNICORN_EXPECTING_DATA;
				return 0;
			}

			if( cf->msgHdr.length ) {
				cf->flags |= UNICORN_EXPECTING_DATA;
				cf->data_length = cf->msgHdr.length;
				cf->last_message = time(0L);
			} else {
				// Record the time, so I don't send unnecessary state requests
				cf->last_message = time(0L);
				// handle notifications with no data
				printf("Got CMD   %d\n",cf->msgHdr.cmd );
				printf("Got STATE %d\n",cf->msgHdr.state );

				switch( cf->msgHdr.cmd ) {
					case CMD_KEEPALIVE:
					case CMD_READY:
					case CMD_STATE:
						if( cf->msgHdr.state != cf->driver_state ) {
							cf->last_state_timestamp = cf->last_message;
							d_printf("Handling state change. cf->flags = %x (terminating %s)\n", cf->flags, cf->flags&UNICORN_TERMINATING?"true":"false");
							if( cf->flags & UNICORN_TERMINATING ) {
								d_printf("State = TERMINATING, need to shutdown driver\n");
								if( cf->msgHdr.state == CMD_ST_OFFLINE ) {
									d_printf("modem is offline. shutting down driver\n");
									send_unicorn_command( ctx, CMD_SHUTDOWN, CMD_ST_OFFLINE, 0, 0L );
#if 0
									if( cf->modem ) {
										emit(cf->modem, EVENT_TERMINATE, 0L);
										context_terminate( ctx );
									}
#endif
								} else if( cf->msgHdr.state == CMD_ST_INPROGRESS ) {
									send_unicorn_command( ctx, CMD_DISCONNECT, CMD_ST_OFFLINE, 0, 0L );
								}
									
							} else {
								if( cf->msgHdr.state == CMD_ST_OFFLINE ) {
									d_printf("Sending CONNECT command\n");
									send_unicorn_command(ctx, CMD_CONNECT, CMD_ST_ONLINE, 0, 0 );
								}
							}
							cf->driver_state = cf->msgHdr.state;
							break;

							default:
							d_printf("Not ready to deal with cmd %d\n",cf->msgHdr.cmd);

						} else {
							d_printf("No state change.. doing nothing..\n");
						}

				}

				// indicate that there *was* something in the buffer and I should check for more
				// This is specifically to avoid the issue caused when a single read received mulitple
				// separate command frames
				rc = 0;
			}
		}
	}

	// return because there is nothing useful in the buffer..
	return rc;
}

ssize_t unicorn_handler(context_t *ctx, event_t event, driver_data_t *event_data)
{
	event_request_t *fd = 0;
	event_data_t   *data = 0L;
	event_child_t *child = 0L;

	unicorn_config_t *cf = (unicorn_config_t *) ctx->data;
	d_printf(" - cf->flags = %x (terminating %s)\n", cf->flags, cf->flags&UNICORN_TERMINATING?"true":"false");

	//d_printf("event = \"%s\" (%d)\n", event_map[event], event);

	//d_printf("event_data = %p\n", event_data);
	//d_printf("event_data->type = %s\n", driver_data_type_map[event_data->type]);

	if (event_data && event_data->type == TYPE_FD)
		fd = &event_data->event_request;
	else if (event_data->type == TYPE_DATA)
		data = &event_data->event_data;
	else if( event_data->type == TYPE_CHILD )
		child = & event_data->event_child;

#if 0
	if( event_data->type == TYPE_DATA ) {
		d_printf("data = %p\n",data);
		d_printf("data->len = %d\n",data->bytes);
	}
#endif
	//d_printf("fd = %p\n", fd);

	switch (event) {
	case EVENT_INIT:
		d_printf("UNICORN INIT event triggered\n");

		event_add( ctx, SIGQUIT, EH_SIGNAL );
		event_add( ctx, SIGTERM, EH_SIGNAL );
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

	case EVENT_TERMINATE:
		{
			d_printf("Got a termination event.  Cleaning up\n");

			cf->termination_timestamp = time(0L);

			cf->flags |= UNICORN_TERMINATING; // In process of terminating the modem driver
			cf->state  = UNICORN_STATE_STOPPING; // In process of terminating self

			// Ensure 'exec' driver known not to restart when the modem driver terminates.
			// If the modem driver is something other than 'exec', this should be ignored.
			uint8_t flag = 0;
			driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
			notification.event_custom = &flag;
			emit(cf->modem, EXEC_SET_RESPAWN, &notification);

			if( cf->driver_state == CMD_ST_ONLINE ) {
				d_printf("Driver is online - sending disconnect\n");
				send_unicorn_command( ctx, CMD_DISCONNECT, CMD_ST_OFFLINE, 0, 0L );
			} else {
				d_printf("Driver is offline - sending shutdown\n");
				send_unicorn_command( ctx, CMD_STATE, CMD_ST_OFFLINE, 0, 0L );
			}
		}
		break;

	case EVENT_CHILD:
		d_printf("Got a message from a child (%s:%d).. probably starting\n", child->ctx->name, child->action);
		if ( child->ctx == cf->modem ) {
			if( child->action == CHILD_STARTING )
				cf->driver_state = 0xFFFF;

			if ( child->action == CHILD_STOPPED ) {
				d_printf("Modem driver terminated - restart or terminate\n");
				// modem driver terminated.  Restart or exit.
				if ( cf->flags & UNICORN_TERMINATING ) {
					d_printf("Terminating immediately\n");
					context_terminate( ctx );
				} else {
					d_printf("Need to restart modem driver\n");
				}
			}
		}
		break;

	case EVENT_DATA_INCOMING:
	case EVENT_DATA_OUTGOING:
		if (data) {
			if( event_data->source == cf->modem ) {
				d_printf("Got a DATA event from the modem driver...\n");

				size_t bytes = data->bytes;
				size_t offset = 0;

				while( bytes ) {
					size_t to_read = u_ringbuf_avail( &cf->input );
					if( to_read > bytes )
						to_read = bytes;
#ifndef NDEBUG
					else
						d_printf("Got record from %s, but not enough space to store it\n", event_data->source->name);
#endif

					//d_printf("moving %d bytes of input data to the ring buffer\n", to_read);
					u_ringbuf_write( &cf->input, &((char *)data->data)[offset], to_read );

					bytes -= to_read;
					offset += to_read;

					// process some ring buffer data
					while(process_unicorn_data(ctx) >= 0);
				}

				return (ssize_t) offset;
			}
		} else {
			d_printf("Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
		}

		break;

	case EVENT_READ:
#if 0
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
#endif
		break;

	case EVENT_EXCEPTION:
		d_printf("Got an exception on FD %d\n", fd->fd);
		break;

	case EVENT_SIGNAL:
		d_printf("Woa! Got a sign from the gods... %d\n", event_data->event_signal);
		if( event_data->event_signal == SIGQUIT || event_data->event_signal == SIGTERM ) {
			emit( ctx, EVENT_TERMINATE, 0L );
#if 0
			if( cf->modem )
				emit(cf->modem, EVENT_TERMINATE, 0L);
			event_delete( ctx, SIGQUIT, EH_SIGNAL );
			context_terminate( ctx );
#endif
		}
		break;

	case EVENT_TICK:
		{
			time_t now = time(0L);

			if( (now - cf->last_message) > 300 ) {
				// Its been a long time since the last keepalive, despite prompting for one
				// restart the modem driver

				uint8_t sig = SIGHUP;
				driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
				notification.event_custom = &sig;

				emit( cf->modem, EVENT_RESTART, &notification );
				cf->last_message = now;
			}

			if( (now - cf->last_message) > 120 ) {
		
				// Its been a couple of minutes since the last keepalive, prompt for one.
				frmHdr_t frame = {  MAGIC_NUMBER, CMD_STATE, 0, 0 };
				driver_data_t notification = { TYPE_DATA, ctx, {} };
				notification.event_data.data = &frame;
				notification.event_data.bytes = sizeof( frame );

				emit( cf->modem, EVENT_DATA_OUTGOING, &notification );
				send_unicorn_command( ctx, CMD_STATE, CMD_ST_OFFLINE, 0, 0L );
			}

			if( (cf->flags & UNICORN_TERMINATING) && ((now - cf->termination_timestamp) > UNICORN_PROCESS_TERMINATION_TIMEOUT)) {
				d_printf("termination timeout - killing the modem driver with prejudice\n");
				context_terminate( cf->modem );
				context_terminate( ctx );
			}

			// Special case.. If I am expecting a data frame, and it takes too long to arrive,
			// reset state.
			if( (cf->flags & UNICORN_EXPECTING_DATA) && ((now - cf->last_message) > FRAME_TIMEOUT)) {
				u_ringbuf_init( &cf->input );
				cf->flags &= ~(unsigned int)UNICORN_EXPECTING_DATA;
			}

#ifndef NDEBUG
			if( 1 ) {
				size_t bytes = u_ringbuf_ready( &cf->input );
				if( bytes ) {
					d_printf("Un-processed data in ring buffer... %d bytes\n",bytes);
				}
			}
#endif

		}
		break;

	default:
		d_printf("\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
