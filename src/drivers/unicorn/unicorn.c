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

#include "netmanage.h"
#include "clock.h"
#include "driver.h"
#include "events.h"
#include "unicorn.h"
#include "logger.h"

int unicorn_init(context_t * ctx)
{
	unicorn_config_t *cf ;

	if (0 == (cf = (unicorn_config_t *) calloc( sizeof( unicorn_config_t ) , 1 )))
		return 0;

	cf->pending_action_timeout = cf->last_message = rel_time(0L);
	cf->driver_state = CMD_ST_UNKNOWN;

	u_ringbuf_init(&cf->input);
	ctx->data = cf;

	return 1;
}

int unicorn_shutdown(context_t * ctx)
{
	if( ctx->data )
		free( ctx->data );

	ctx->data = 0;
	return 1;
}

ssize_t send_unicorn_command( context_t *ctx, cmdHost_t cmd, cmdState_t state, size_t length, void *data )
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

ssize_t process_unicorn_header( context_t *ctx )
{
	unicorn_config_t *cf = ctx->data;

	switch( cf->msgHdr.cmd ) {
		case CMD_READY:
			// If the driver has JUST STARTED, then it bring the network online immediately, otherwise
			// the retry delay takes effect
			cf->flags |= UNICORN_FIRST_START;
			cf->driver_state = CMD_ST_UNKNOWN;
		case CMD_KEEPALIVE:
		case CMD_STATE:
			if( cf->msgHdr.state != cf->driver_state ) {

				if( cf->msgHdr.state == CMD_ST_ONLINE || cf->msgHdr.state == CMD_ST_OFFLINE || cf->msgHdr.state == CMD_ST_ERROR ) {
					x_printf(ctx,"Notifying parent that the network state has changed to %s\n",cf->msgHdr.state == CMD_ST_ONLINE ? "online":"offline");
					context_owner_notify( ctx, CHILD_EVENT, cf->msgHdr.state == CMD_ST_ONLINE ? UNICORN_MODE_ONLINE : UNICORN_MODE_OFFLINE );
				}

				if( cf->flags & UNICORN_TERMINATING ) {
					x_printf(ctx,"State = TERMINATING, need to shutdown driver\n");
					if( cf->msgHdr.state == CMD_ST_OFFLINE )
						send_unicorn_command( ctx, CMD_SHUTDOWN, CMD_ST_OFFLINE, 0, 0L );
					else
						send_unicorn_command( ctx, CMD_DISCONNECT, CMD_ST_OFFLINE, 0, 0L );
				} else {
					switch( cf->msgHdr.state ) {
						case CMD_ST_ONLINE:
							x_printf(ctx,"State is Online - resetting WAITING_FOR_CONNECT/RECONNECTING status\n");
							cf->flags &= ~(unsigned int) (UNICORN_WAITING_FOR_CONNECT|UNICORN_RECONNECTING|UNICORN_FIRST_START);
							break;
						case CMD_ST_OFFLINE:
							x_printf(ctx,"Sending CONNECT command (Setting WAITING_FOR_CONNECT/RECONNECTING FLAG)\n");
							// RECONNECTING causes a CMD_CONNECT to be sent after the RETRY delay
							// WAITING_FOR_CONNECT causes the timeout handler to kill the modem driver if a
							// connect does not occur before a specified window of time has elapsed
							cf->pending_action_timeout = cf->last_message;
							// Special case, in a first start scenario, don't wait retry
							if( cf->flags & UNICORN_FIRST_START) {
								x_printf(ctx,"Forcing connection - FIRST_START is set\n");
								send_unicorn_command(ctx, CMD_CONNECT, CMD_ST_ONLINE, 0, 0 );
								cf->flags |= UNICORN_WAITING_FOR_CONNECT;
							} else {
								x_printf(ctx,"Setting up for delayed connection\n");
								cf->flags |= UNICORN_WAITING_FOR_CONNECT|UNICORN_RECONNECTING;
							}
							break;
						case CMD_ST_ERROR:
							break;
						default:
							break;
					}
				}
				cf->driver_state = cf->msgHdr.state;
			}
			break;
		case CMD_DATA:
			cf->flags |= UNICORN_EXPECTING_DATA;
			cf->data_length = cf->msgHdr.length;
			cf->last_message = rel_time(0L);
			break;
		default:
			x_printf(ctx,"Not ready to deal with cmd %d\n",cf->msgHdr.cmd);
			break;

	}

	return 0;
}

ssize_t process_unicorn_data( context_t *ctx, size_t ready )
{
	unicorn_config_t *cf = ctx->data;

	x_printf(ctx, "Entering process_unicorn_data()\n");
	if( ready >= cf->msgHdr.length ) {
		char *data_buffer = alloca( cf->msgHdr.length );
		u_ringbuf_read( &cf->input, data_buffer, cf->msgHdr.length );

		driver_data_t notification = { TYPE_DATA, ctx, {} };
		notification.event_data.bytes = cf->msgHdr.length;
		notification.event_data.data = data_buffer;
		emit( ctx->owner, EVENT_DATA_INCOMING, &notification );

		cf->flags &= ~(unsigned int)UNICORN_EXPECTING_DATA;
		return 0;
	}
	x_printf(ctx, "Leaving process_unicorn_data()\n");
	return -1;
}

ssize_t process_unicorn_packet( context_t *ctx )
{
	unicorn_config_t *cf = ctx->data;

	x_printf(ctx, "Entering process_unicorn_packet()\n");

	size_t ready = u_ringbuf_ready( &cf->input );

	if( !ready )
		return -1;

	if( cf->flags & UNICORN_EXPECTING_DATA ) {
		return process_unicorn_data( ctx, ready );
	} else {
		if( ready >= sizeof( frmHdr_t ) ) {
			u_ringbuf_read( &cf->input, & cf->msgHdr, sizeof( cf->msgHdr ) );
			if( MAGIC_NUMBER != cf->msgHdr.magicNo ) {
				x_printf(ctx,"MAGIC NUMBER FAIL... tossing the baby out with the bathwater..\n");
				u_ringbuf_init( &cf->input );
				cf->flags &= ~(unsigned int)UNICORN_EXPECTING_DATA;
				x_printf(ctx, "Leaving process_unicorn_packet()\n");
				return -1;
			}

			cf->last_message = rel_time(0L);
			x_printf(ctx, "Leaving process_unicorn_packet() via process_unicorn_header()\n");
			return process_unicorn_header( ctx );
		}
	}

	x_printf(ctx, "Leaving process_unicorn_packet()\n");
	// return because there is nothing useful in the buffer..
	return -1;
}

ssize_t unicorn_handler(context_t *ctx, event_t event, driver_data_t *event_data)
{
	event_data_t   *data = 0L;
	event_child_t *child = 0L;

	unicorn_config_t *cf = (unicorn_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	if (event_data->type == TYPE_DATA)
		data = &event_data->event_data;
	else if( event_data->type == TYPE_CHILD )
		child = & event_data->event_child;

	switch (event) {
	case EVENT_INIT:
		{
			x_printf(ctx,"calling event add SIGQUIT\n");
			event_add( ctx, SIGQUIT, EH_SIGNAL );
			x_printf(ctx,"calling event add SIGTERM\n");
			event_add( ctx, SIGTERM, EH_SIGNAL );
			x_printf(ctx,"calling event 1000 EH_WANT_TICK\n");
			event_add( ctx, 1000, EH_WANT_TICK );

			const char *endpoint = config_get_item( ctx->config, "endpoint" );

			cf->retry_time = config_get_timeval( ctx->config, "retry" );
			if( !cf->retry_time )
				cf->retry_time = 120*1000;

			if( endpoint )
				start_service( &cf->modem, endpoint, ctx->config, ctx, 0L );

			if( !cf->modem ) {
				logger( ctx->owner, ctx, "Unable to start endpoint driver. Exiting\n" );
				cf->state = UNICORN_STATE_ERROR;
				context_terminate( ctx );
				return -1;
			}

			cf->state = UNICORN_STATE_IDLE;
		}
		break;

	case EVENT_TERMINATE:
		{
			cf->pending_action_timeout = rel_time(0L);

			cf->flags |= UNICORN_TERMINATING; // In process of terminating the modem driver
			cf->state  = UNICORN_STATE_STOPPING; // In process of terminating self

			// Ensure 'exec' driver known not to restart when the modem driver terminates.
			// If the modem driver is something other than 'exec', this should be ignored.
			uint8_t flag = 0;
			driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
			notification.event_custom = &flag;
			emit(cf->modem, EXEC_SET_RESPAWN, &notification);

			if( cf->driver_state == CMD_ST_ONLINE ) {
				x_printf(ctx,"Driver is online - sending disconnect\n");
				send_unicorn_command( ctx, CMD_DISCONNECT, CMD_ST_OFFLINE, 0, 0L );
			} else {
				x_printf(ctx,"Driver is offline - sending shutdown\n");
				send_unicorn_command( ctx, CMD_STATE, CMD_ST_OFFLINE, 0, 0L );
			}
		}
		break;

	case EVENT_RESTART:
		// This event is used to signal that the modem driver needs to resync.
		// set the 'reconnecting' flag and send a disconnect
		x_printf(ctx,"EVENT_RESTART: - sending disconnect to modem\n");
		if( event_data->source == ctx->owner ) {
			cf->pending_action_timeout = rel_time(0L);
			cf->flags |= UNICORN_WAITING_FOR_CONNECT;
			if( cf->modem ) {
				x_printf(ctx, "Sending CMD_DISCONNECT to modem driver (%s)\n",cf->modem->name);
				send_unicorn_command( ctx, CMD_DISCONNECT, CMD_ST_OFFLINE, 0, 0L );
			} else {
				x_printf(ctx, "Modem driver not running.. doing nothing.\n");
			}
		} else {
			x_printf(ctx,"Forwarding EVENT_RESTART to owner (%s)\n",ctx->name);
			emit( ctx->owner, EVENT_RESTART, event_data);
		}
		break;

	case EVENT_CHILD:
		x_printf(ctx,"Got a message from a child (%s:%d).. probably starting\n", child->ctx->name, child->action);
		if ( child->ctx == cf->modem ) {
			if( child->action == CHILD_STARTING ) {
				cf->state = UNICORN_STATE_RUNNING;
				cf->flags &= ~(unsigned int) UNICORN_RESTARTING;
			}

			if ( child->action == CHILD_STOPPED ) {
				x_printf(ctx,"Modem driver terminated - restart or terminate\n");
				// modem driver terminated.  Restart or exit.
				cf->state = UNICORN_STATE_IDLE;
				if ( cf->flags & UNICORN_TERMINATING ) {
					x_printf(ctx,"Terminating immediately\n");
					context_terminate( ctx );
				} else {
					x_printf(ctx,"Need to restart modem driver\n");
					cf->flags |= UNICORN_RESTARTING;
					cf->pending_action_timeout = rel_time(0L);
					// Reset the driver state, and notify the parent that we are offline
					cf->driver_state = CMD_ST_UNKNOWN;
					context_owner_notify( ctx, CHILD_EVENT, UNICORN_MODE_OFFLINE );
				}
			}
		}
		break;

	case EVENT_DATA_INCOMING:
	case EVENT_DATA_OUTGOING:
		if( event_data->source == cf->modem ) {

			size_t bytes = data->bytes;
			size_t offset = 0;

			while( bytes ) {

				size_t to_read = u_ringbuf_avail( &cf->input );
				if( to_read > bytes )
					to_read = bytes;

				u_ringbuf_write( &cf->input, &((char *)data->data)[offset], to_read );

				bytes -= to_read;
				offset += to_read;

				while(process_unicorn_packet(ctx) >= 0);
			}

			return (ssize_t) offset;
		} else {
			send_unicorn_command( ctx, CMD_DATA, CMD_ST_ONLINE, data->bytes,data->data);
			return (ssize_t) data->bytes;
		}

		break;

	case EVENT_READ:
		break;

	case EVENT_EXCEPTION:
		break;

	case EVENT_SIGNAL:
		x_printf(ctx,"Woa! Got a sign from the gods... %d\n", event_data->event_signal);
		if( event_data->event_signal == SIGQUIT || event_data->event_signal == SIGTERM )
			emit( ctx, EVENT_TERMINATE, 0L );
		break;

	case EVENT_TICK:
		{
			time_t now = rel_time(0L);

			// Handle case where a massive time shift due to NTP resync causes all timeouts to fire simultaneously
			if( (now - cf->last_message) > MAXIMUM_SAFE_TIMEDELTA )
				cf->last_message = now;

			if( (now - cf->last_message) > 300*1000 ) {
				// Its been a long time since the last keepalive, despite prompting for one
				// restart the modem driver

				uint8_t sig = SIGHUP;
				driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
				notification.event_custom = &sig;

				emit( cf->modem, EVENT_RESTART, &notification );
				cf->last_message = now;
			}

			if( ((now - cf->last_message) > 120*1000 ) && ( cf->driver_state != CMD_ST_UNKNOWN )) {

				// Its been a couple of minutes since the last keepalive, reset the driver_state
				// to unknown and prompt for one.
				cf->driver_state = CMD_ST_UNKNOWN;

				frmHdr_t frame = {  MAGIC_NUMBER, CMD_STATE, 0, 0 };
				driver_data_t notification = { TYPE_DATA, ctx, {} };
				notification.event_data.data = &frame;
				notification.event_data.bytes = sizeof( frame );

				emit( cf->modem, EVENT_DATA_OUTGOING, &notification );
				send_unicorn_command( ctx, CMD_STATE, CMD_ST_OFFLINE, 0, 0L );
			}

			if( (cf->flags & UNICORN_RESTARTING) && ((now - cf->pending_action_timeout) > UNICORN_RESTART_DELAY )) {
				x_printf(ctx,"Restart delay expired - restarting modem driver\n");
				cf->pending_action_timeout = rel_time(0L);
				const char *endpoint = config_get_item( ctx->config, "endpoint" );
				if( endpoint )
					start_service( &cf->modem, endpoint, ctx->config, ctx, 0L );
			} else if( (cf->flags & UNICORN_RECONNECTING) && ((now - cf->pending_action_timeout) > cf->retry_time )) {
				x_printf(ctx,"Reconnect delay expired - attempting reconnect\n");
				cf->pending_action_timeout = rel_time(0L);
				cf->flags &= ~(unsigned int)UNICORN_RECONNECTING;
				if( cf->modem )
					send_unicorn_command(ctx, CMD_CONNECT, CMD_ST_ONLINE, 0, 0 );
			} else if( (cf->flags & UNICORN_WAITING_FOR_CONNECT) && ((now - cf->pending_action_timeout) > UNICORN_CONNECT_TIMEOUT )) {
				x_printf(ctx,"Timeout during connect - terminating modem driver\n");
				cf->flags &= ~(unsigned int) UNICORN_WAITING_FOR_CONNECT;
				cf->state = UNICORN_STATE_IDLE;
				if( cf->modem ) {
					emit( cf->modem, EVENT_TERMINATE, 0L );
				}
			}

			if( (cf->flags & UNICORN_TERMINATING) && ((now - cf->pending_action_timeout) > UNICORN_PROCESS_TERMINATION_TIMEOUT)) {
				x_printf(ctx,"termination timeout - killing the modem driver with prejudice\n");
				cf->state = UNICORN_STATE_IDLE;
				if( cf->modem )
					context_terminate( cf->modem );
				context_terminate( ctx );
			}

			// Special case.. If I am expecting a data frame, and it takes too long to arrive,
			// reset state.
			if( (cf->flags & UNICORN_EXPECTING_DATA) && ((now - cf->last_message) > FRAME_TIMEOUT)) {
				x_printf(ctx,"FRAME TIMEOUT - resetting input buffer\n");
				u_ringbuf_init( &cf->input );
				cf->flags &= ~(unsigned int)UNICORN_EXPECTING_DATA;
			}

#ifndef NDEBUG
			size_t bytes = u_ringbuf_ready( &cf->input );
			if( bytes )
				x_printf(ctx,"Un-processed data in ring buffer... %d bytes\n",bytes);
#endif

		}
		break;

	default:
		x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
