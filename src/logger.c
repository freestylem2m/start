/*
 * File: logger.c
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
#include <stdarg.h>
#include <alloca.h>
#include <string.h>

#include "netmanage.h"
#include "events.h"
#include "driver.h"
#include "logger.h"

void logger(context_t *source, char *fmt, ...) {

	static context_t *logger_context;

	if( ! logger_context ) {
		const char *logdriver = config_item( "global", "logger" );
		if( logdriver )
			start_service( & logger_context, logdriver, config_get_section( "global" ), 0L, 0L );
	}

	va_list fmt_args;
	va_start( fmt_args, fmt );
	char *log_buffer = alloca( LOG_BUFFER_MAX );

	if( fmt ) {
		vsnprintf( log_buffer, LOG_BUFFER_MAX, fmt, fmt_args );
		log_buffer[LOG_BUFFER_MAX-1] = 0;
	}

	if( logger_context ) {
		driver_data_t event = { TYPE_DATA, .source = source, {} };
		event.event_data.bytes = strlen(log_buffer);
		event.event_data.data = log_buffer;
		emit( logger_context, EVENT_LOGGING, &event );
	} else {
		time_t spec = time(0L);
		char spec_buffer[64];
		d_printf("No Logging context, defaulting to stderr\n");
		fwrite( spec_buffer, strftime(spec_buffer,64,"%b %e %H:%M:%S: ", localtime( &spec )), 1, stderr );
		fwrite( log_buffer, strlen(log_buffer), 1, stderr );
		fwrite( "\n", 1, 1, stderr );
	}
}
