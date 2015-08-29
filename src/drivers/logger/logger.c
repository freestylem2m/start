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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <alloca.h>
#include <stdlib.h>
#include <fcntl.h>

#include "netmanage.h"
#include "clock.h"
#include "driver.h"
#include "events.h"
#include "logger.h"

int logger_init(context_t *context)
{
	logger_config_t *cf;

	if ( 0 == (cf = (logger_config_t *) calloc( sizeof( logger_config_t ) , 1 )))
		return 0;

	cf->state = LOGGER_STATE_IDLE;
	cf->log_fd = -1;

	context->data = cf;

	return 1;
}

int logger_shutdown(context_t *ctx)
{
	logger_config_t *cf = (logger_config_t *) ctx->data;

	if( cf->logger ) {
		context_terminate( cf->logger );
		cf->logger = 0L;
	}

	if( cf->log_fd >= 0 ) {
		close( cf->log_fd );
		cf->log_fd = -1;
	}

	free(cf);
	return 1;
}

ssize_t logger_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_data_t *data = 0L;

	logger_config_t *cf = (logger_config_t *) ctx->data;

	if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch( event ) {
		case EVENT_INIT:
			if(( cf->log_driver = config_get_item( ctx->config, "log_driver" ) )) {
				if( strchr( cf->log_driver, '/' ) )
					cf->log_fd = open( cf->log_driver, O_RDWR|O_APPEND|O_CREAT, 0777 );
				else
					cf->logger = start_service( cf->log_driver, ctx->config, ctx );
			}
			cf->state = LOGGER_STATE_RUNNING;
			break;

		case EVENT_TERMINATE:
			context_terminate( ctx );
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
        case EVENT_LOGGING:
			if( data ) {
				char *logbuffer = alloca( LOG_BUFFER_MAX );
				snprintf(logbuffer, LOG_BUFFER_MAX, "%s: %s", event_data->source?event_data->source->name:"(unknown)", (char *)data->data);
				logbuffer[LOG_BUFFER_MAX-1] = 0;
				time_t spec;
				char spec_buffer[32];

				if( cf->logger ) {
					driver_data_t log_event = { TYPE_DATA, .source = ctx, {} };
					log_event.event_data.data = logbuffer;
					log_event.event_data.bytes = strlen(logbuffer);
					emit(cf->logger, EVENT_DATA_OUTGOING, &log_event);
				} else {
					time(&spec);
					size_t len = (size_t) strftime(spec_buffer,32,"%b %e %H:%M:%S: ", localtime( &spec ));

					if( cf->log_fd >= 0 ) {
						if( (write( cf->log_fd, spec_buffer, len ) < 0) || ( write(cf->log_fd, logbuffer, strlen(logbuffer) ) < 0 ) )
							perror( AT "write");
					} else {
						fwrite( spec_buffer, len, 1, stderr );
						fwrite( logbuffer, strlen(logbuffer), 1, stderr );
					}
				}
			}
			break;

		default:
			break;
	}
	return 0;
}

void logger(context_t *ctx, context_t *source, char *fmt, ...) {
	va_list fmt_args;
	va_start( fmt_args, fmt );
	char *log_buffer = alloca( LOG_BUFFER_MAX );

	if( fmt ) {
		vsnprintf( log_buffer, LOG_BUFFER_MAX, fmt, fmt_args );
		log_buffer[LOG_BUFFER_MAX-1] = 0;
	}
	if( ctx ) {
		driver_data_t event = { TYPE_DATA, .source = source, {} };
		event.event_data.bytes = strlen(log_buffer);
		event.event_data.data = log_buffer;
		emit( ctx, EVENT_LOGGING, &event );
	} else {
		time_t spec = time(0L);
		char spec_buffer[32];
		x_printf(ctx,"No Logging context, defaulting to stderr\n");
		fwrite( spec_buffer, strftime(spec_buffer,32,"%b %e %H:%M:%S: ", localtime( &spec )), 1, stderr );
		fwrite( log_buffer, strlen(log_buffer), 1, stderr );
	}
}
