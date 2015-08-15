/*
 * File: events.h
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
 *
 */

#ifndef __EVENTS_H__
#define __EVENTS_H__

#include <sys/types.h>
#include <signal.h>

#include "config.h"

typedef enum {
	EVENT_NONE,
	EVENT_INIT,
	EVENT_READ,
	EVENT_WRITE,
	EVENT_EXCEPTION,
	EVENT_SIGNAL,
	EVENT_TICK,
	EVENT_DATA,
	EVENT_MAX
} event_t;

typedef enum {
	CTX_NONE,
	CTX_DEAD
} context_flags_t;

// A "context" is an instance of a driver, with instance configuration and instance data.
// The "name" is often the same as the name of the "config" stanza.

// The parent/child context allows chain control channels between drivers (eg, udp port -> protocol handler -> process )
typedef struct context_s
{
	const char     *name;
	const config_t *config;

	// Driver used to implement this service
	const struct driver_s *driver;
	const config_t *driver_config;
	struct event_handler_list_s *event;
	void           *data;
	context_flags_t flags;
	struct context_s *parent;
	struct context_s *child;
	struct context_s *next;
} context_t;

#define MAX_CLASS_NAME  256
#define MAX_READ_BUFFER 1024

typedef enum
{
	EH_NONE = 0,
	EH_READ = 1,
	EH_WRITE = 2,
	EH_EXCEPTION = 4,
	EH_DEFAULT = 5,
	EH_WRITE2 = 6,
	EH_READWRITE = 7,
	EH_SIGNAL = 8,
	EH_SPECIAL = (EH_SIGNAL),
	EH_WANT_TICK = 128,
	EH_DELETED = 256,
	EH_PARENT = 512,
	EH_CHILD = 1024,
} event_handler_flags_t;

typedef enum {
	TYPE_NONE,
	TYPE_FD,
	TYPE_CONSOLE,
	TYPE_TICK,
} data_type_t;

typedef struct console_data_s {
	size_t bytes;
	char *data;
} console_data_t;

typedef struct fd_list_s
{
	int             fd;
	event_handler_flags_t flags;
	struct fd_list_s *next;
} fd_list_t;

typedef struct driver_data_s {
	data_type_t type;
	union {
		time_t		   tick;
		fd_list_t	   *fd_data;
		console_data_t console_data;
	};
} driver_data_t;

typedef struct driver_s
{
	const char     *name;
	int             (*init) (context_t *);
	int             (*shutdown) (context_t *);
	int             (*emit) (context_t *context, event_t event, driver_data_t *event_data );
} driver_t;

typedef int (*event_handler_t)(struct context_s *context, event_t event, driver_data_t *event_data);

typedef struct event_handler_list_s
{
	const char     *name;
	event_handler_t handler;
	fd_list_t		*files;
	context_t *context;
	struct event_handler_list_s *next;
	event_handler_flags_t flags;
	char            deleted;
} event_handler_list_t;

extern event_handler_list_t *event_handler_list;

extern void handle_signal_event( int sig_event );

extern const event_handler_list_t *find_event_handler( const char *classname );
extern event_handler_list_t *add_event_handler( const char *classname, context_t *context, event_handler_t handler, event_handler_flags_t flags );
extern event_handler_flags_t set_event_handler_flags( event_handler_list_t *, const event_handler_flags_t flags );
extern event_handler_list_t *register_event_handler( const char *classname, context_t *context, event_handler_t handler, event_handler_flags_t flags );
extern void deregister_event_handler( event_handler_list_t *event );
int event_loop( int timeout );

extern fd_list_t *event_find( event_handler_list_t *handler_list, int fd );
extern fd_list_t *event_set( event_handler_list_t *handler_list, int fd, event_handler_flags_t flags );
extern fd_list_t *event_add( event_handler_list_t *handler_list, int fd, event_handler_flags_t flags );
extern void event_delete( event_handler_list_t *handler_list, int fd, event_handler_flags_t flags );
extern void event_prune( event_handler_list_t *handler_list );

extern int event_bytes( int fd, size_t *pbytes );
extern ssize_t event_read( int fd, char *buffer, size_t len );
extern int event_waitchld( int *status, int pid );

#endif
