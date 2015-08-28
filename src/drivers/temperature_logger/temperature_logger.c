/*
 * File: temperature_logger.c
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
#include <fcntl.h>

#include "netmanage.h"
#include "clock.h"
#include "driver.h"
#include "events.h"
#include "temperature_logger.h"
#include "logger.h"
#include "hvc_util.h"
#include "format.h"

int temperature_logger_init(context_t * ctx)
{
	x_printf(ctx,"Hello from TEMPERATURE LOGGER(%s) INIT!\n", ctx->name);

	temperature_logger_config_t *cf ;

	if (0 == (cf = (temperature_logger_config_t *) calloc( sizeof( temperature_logger_config_t ) , 1 )))
		return 0;

	ctx->data = cf;

	return 1;
}

int temperature_logger_shutdown(context_t * ctx)
{
	x_printf(ctx,"Goodbye from TEMPERATURE LOGGER INIT!\n");

	if( ctx->data )
		free( ctx->data );

	ctx->data = 0;

	return 1;
}

ssize_t temperature_logger_handler(context_t *ctx, event_t event, driver_data_t *event_data)
{
	UNUSED(event_data);

	temperature_logger_config_t *cf = (temperature_logger_config_t *) ctx->data;

	switch (event) {
	case EVENT_INIT:
		{
			unsigned int interval = config_get_timeval( ctx->config, "interval" );
			cf->logfile = config_get_item( ctx->config, "logfile" );
			cf->format_str = config_get_item( ctx->config, "format" );

			if( !cf->logfile )
				cf->logfile = "/flash/temp.log";
			if( !cf->format_str )
				cf->format_str = "%t";

			cf->format_content[0].key = 'T';
			cf->format_content[0].type = FMT_DATESTRING;
			cf->format_content[1].key = 't';
			cf->format_content[1].type = FMT_UINT;

			cf->timer_fd = event_alarm_add( ctx, (time_t) interval, ALARM_INTERVAL );
		}
		break;

	case EVENT_ALARM:
		{
			unsigned int bytes;
			int temperature = hvc_getTemperature();
			int fd = open(cf->logfile, O_CREAT|O_WRONLY|O_APPEND, 0777 );
			if( fd >= 0 ) {
				cf->format_content[0].d_time = time(0L);
				cf->format_content[1].u_val = (unsigned int )temperature;
				bytes = format_string(cf->format_buffer, TEMPERATURE_LOGGER_BUFFER_MAX-1, cf->format_str, cf->format_content);
				cf->format_buffer[bytes++] = '\n';
				if( (write( fd, cf->format_buffer, bytes )) < 0 )
					perror("write");
				close( fd );
			}
		}
		break;

	case EVENT_TERMINATE:
		event_alarm_delete( ctx, cf->timer_fd );
		break;

	case EVENT_RESTART:
	case EVENT_READ:
	case EVENT_EXCEPTION:
	case EVENT_SIGNAL:
	case EVENT_TICK:
		break;

	default:
		x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
