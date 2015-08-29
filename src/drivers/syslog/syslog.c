/*
 * File: syslog.c
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

#define _GNU_SOURCE

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
#include <syslog.h>

#include "netmanage.h"
#include "clock.h"
#include "driver.h"
#include "events.h"
#include "syslog.h"
#include "cmdline.h"

typedef struct generic_string_map_s {
	const char *item;
	int value;
} generic_string_map_t;

#define _map(x)  { #x, LOG_ ## x }
static generic_string_map_t options_table[] =
{
	// Available Options
	_map( CONS ), _map( NDELAY ), _map( PERROR ), _map( PID ),
	//
	// Available Facilities
	_map( AUTH ), _map( AUTHPRIV ), _map( CRON ), _map( DAEMON ), _map( FTP ), _map( KERN ),
	_map( LOCAL0 ), _map( LOCAL1 ), _map( LOCAL2 ), _map( LOCAL3 ), _map( LOCAL4 ), _map( LOCAL5 ), _map( LOCAL6 ), _map( LOCAL7 ),
	_map( LPR ), _map( MAIL ), _map( NEWS ), _map( SYSLOG ), _map( USER ), _map( UUCP ),
	//
	// Available Levels
	_map( EMERG ), _map( ALERT ), _map( CRIT ), _map( ERR ), _map( WARNING ), _map( NOTICE ), _map( INFO ), _map( DEBUG ),
	{ 0L, 0 }
};

static int syslog_lookup_ident( const char *str )
{
	generic_string_map_t *ptr = options_table;

	while (ptr->item)
		if( ! strncasecmp( str, ptr->item, strlen(ptr->item)) )
			return ptr->value;
		else
			ptr++;

	return -1;
}

static int syslog_options_lookup( const char *str )
{
	char *_s = strdupa( str );
	char *_n = _s;
	int result = 0;
	int _t;

	if (( _t = atoi( _s ) ) > 0 )
		return _t;

	while( _n ) {
		if (( _n = strchr( _s, '|' ) ) )
			*_n++ = 0;

		if((_t = syslog_lookup_ident( _s )) < 0)
				return -1;

		result |= _t;
		_s = _n;
	}

	return result;
}

int syslog_init(context_t *ctx)
{
	if (0 == (ctx->data = calloc(sizeof(syslog_config_t), 1)))
		return 0;

	return 1;
}

int syslog_shutdown(context_t *ctx)
{
	closelog();

	if( ctx->data )
		free( ctx->data );

	return 0;
}

ssize_t syslog_handler(context_t *ctx, event_t event, driver_data_t *event_data)
{
	event_data_t *data = 0L;

	syslog_config_t *cf = (syslog_config_t *) ctx->data;

	if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch (event) {
		case EVENT_INIT:
			{
				x_printf(ctx,"SYSLOG INIT event triggered\n");

				const char *options  = get_env( ctx, "options" );
				const char *facility = get_env( ctx, "facility" );
				const char *priority = get_env( ctx, "prio" );

				if( options ) {
					if( (cf->options = syslog_options_lookup( options )) < 0) {
						fprintf(stderr,"Warning: invalid options specified for syslog %s\n",options );
						cf->options = LOG_PID;
					}
				} else
					cf->options = LOG_PID;

				if( facility ) {
					if( (cf->facility = syslog_options_lookup( facility )) < 0) {
						fprintf(stderr,"Warning: invalid syslog facility %s. Defaulting to USER\n",facility);
						cf->facility = LOG_USER;
					}
				} else
					cf->facility = LOG_USER;

				if( priority ) {
					if( (cf->prio = syslog_options_lookup( priority )) < 0) {
						fprintf(stderr,"Warning: invalid syslog priority %s. Defaulting to NOTICE\n",facility);
						cf->facility = LOG_NOTICE;
					}
				} else
					cf->facility = LOG_NOTICE;

				cf->ident = get_env( ctx, "ident" );

				if( !cf->ident )
					cf->ident = programname;

				openlog( cf->ident, cf->options, cf->facility );
			}
			break;

		case EVENT_TERMINATE:
			context_terminate(ctx);
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
		case EVENT_LOGGING:
			syslog( cf->prio, "%s", (char *) data->data );
			break;

		default:
			break;
	}
	return 0;
}
