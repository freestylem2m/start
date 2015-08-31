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
#include "clock.h"

#if _POSIX_C_SOURCE >= 200112L
#define USE_PSELECT
#endif

// Maximum time shift allowed before considering the offset to be a time-sync event
// If a time shift > 3 days occurs, assume the RTC was out, and a time sync caused
// a massive shift to the current date/time
#define MAXIMUM_SAFE_TIMEDELTA (3*86400*1000)

// Maximum number of services/drivers loaded
#define MAX_CONTEXTS 32
#define MAX_EVENT_REQUESTS 32
#define MAX_SIGCHLD 32
#define MAX_SERVICE_NAME 32

#define MAX_CLASS_NAME  256
#define MAX_READ_BUFFER 1024

typedef enum
{
	EVENT_NONE,
	EVENT_INIT,
	EVENT_START,
	EVENT_READ,
	EVENT_WRITE,
	EVENT_EXCEPTION,
	EVENT_SIGNAL,
	EVENT_SEND_SIGNAL,
	EVENT_ALARM,
	EVENT_TICK,
	EVENT_DATA_INCOMING,
	EVENT_DATA_OUTGOING,
	EVENT_RESTART,
	EVENT_TERMINATE,
	EVENT_CHILD,
	EVENT_RESTARTING,
	EVENT_MAX,
	EVENT_STATE,
	EVENT_LOGGING,

	// Driver specific events
	EXEC_SET_RESPAWN,
} event_t;

#ifndef NDEBUG
extern char        *event_map[];
#endif

typedef enum
{
	CTX_UNUSED,
	CTX_STARTING,
	CTX_RUNNING,
	CTX_TERMINATING
} context_state_t;

// A "context" is an instance of a driver, with instance configuration and instance data.
// The "name" is often the same as the name of the "config" stanza.
typedef struct context_s
{
	char                name[MAX_SERVICE_NAME];

	// driver -> driver jump table, used when starting/stopping the driver and sending messages
	//
	// config, driver_config -> stanza(s) in ini file,
	//          used by get_env() to retrieve service and driver configs
	//
	// owner -> driver or service who owns this.
	const config_t     *config;
	const struct driver_s *driver;
	const config_t     *driver_config;

	struct context_s   *owner;

	void               *data;
	context_state_t     state;
#ifndef NDEBUG
	int                 debug;
#endif
} context_t;

typedef enum
{
	EH_UNUSED = 0,
	EH_NONE = 0,
	EH_READ = 1,
	EH_WRITE = 2,
	EH_EXCEPTION = 4,
	EH_DEFAULT = 5,
	EH_WRITE2 = 6,
	EH_READWRITE = 7,
	EH_SIGNAL = 8,
	EH_TIMER = 16,
	EH_TIMER_FD = 32,
	EH_SPECIAL = (EH_SIGNAL | EH_TIMER | EH_TIMER_FD),
	EH_WANT_TICK = 128,
} event_handler_flags_t;

typedef enum
{
	UNICORN_HEARTBEAT,
	UNICORN_STATE_CHANGE,
} unicorn_event_flags_t;

typedef enum
{
	TYPE_NONE,
	TYPE_FD,					// event_fd
	TYPE_DATA,					// event_data
	TYPE_SIGNAL,				// event_signal
	TYPE_ALARM,					// event_signal
	TYPE_TICK,					// event_tick
	TYPE_CHILD,					// event_child
	// Driver specific data types, which breaks the driver model somewhat, but
	// are infinitely cleaner than un-typed void pointers (aaargh!)
	TYPE_CUSTOM					// custom data
} data_type_t;

#ifndef NDEBUG
extern char        *driver_data_type_map[];
#endif

typedef enum
{
	CHILD_UNKNOWN,
	CHILD_STARTING,
	CHILD_STARTED,
	CHILD_STOPPING,
	CHILD_STOPPED,
	CHILD_FAILED,
	CHILD_EVENT,
} child_status_t;

typedef struct event_child_s
{
	context_t          *ctx;
	child_status_t      action;
	int                 status;
} event_child_t;

typedef struct event_data_s
{
	size_t              bytes;
	void               *data;
} event_data_t;

typedef struct event_request_s
{
	// Owner of the event
	context_t          *ctx;

	// File descriptor or Signal (depending on flags)
	long       fd;
	time_t     timestamp;
	event_handler_flags_t flags;
} event_request_t;

typedef struct driver_data_s
{
	data_type_t         type;
	context_t          *source;	// message origin
	union
	{
		time_t              event_tick;
		event_request_t     event_request;
		event_data_t        event_data;
		int                 event_signal;
		int                 event_alarm;
		event_child_t       event_child;
		void               *event_custom;
	};
} driver_data_t;

typedef enum
{
	MODULE_DRIVER,
	MODULE_SERVICE
} driver_type_t;

typedef int         (*event_callback_t) (context_t *);
typedef             ssize_t(*event_handler_t) (struct context_s * context, event_t event, driver_data_t * event_data);

typedef struct driver_s
{
	const char         *name;
	driver_type_t       type;
	event_callback_t    init;
	event_callback_t    shutdown;
	event_handler_t     emit;
} driver_t;


typedef struct event_alarm_s
{
	time_t              duration;
	event_alarm_flags_t flags;
	time_t              event_time;
	int                 event_next;
	context_t          *ctx;
} event_alarm_t;

#define MAX_ALARM 32

typedef struct event_child_list_s
{
	int                 pid;
	int                 status;
} signal_child_t;

typedef struct event_signals_s
{
	sigset_t            event_signal_mask;
	sigset_t            event_signal_pending;
	sigset_t            event_signal_default;

	int                 event_signal_pending_count;
	int                 event_child_last;
	signal_child_t      child_events[MAX_SIGCHLD];
} event_signals_t;

extern event_signals_t event_signals;
extern event_alarm_t alarm_table[MAX_ALARM];

extern void         handle_signal_event(int sig_event);

int                 event_loop(long timeout);

extern context_t    context_table[MAX_CONTEXTS];
extern event_request_t event_table[MAX_EVENT_REQUESTS];

extern int          event_subsystem_init(void);
extern event_request_t *event_find(const context_t * ctx, long fd, const unsigned int flags);
extern event_request_t *event_set(const context_t * ctx, long fd, unsigned int flags);
extern event_request_t *event_add(context_t * ctx, const long fd, unsigned int flags);
extern void         event_delete(context_t * ctx, int fd, event_handler_flags_t flags);

extern int          alarm_add(context_t * ctx, time_t interval, event_alarm_flags_t flags);
extern int          alarm_delete(context_t * ctx, int key);
extern int          alarm_update(context_t * ctx, int key, time_t interval, event_alarm_flags_t flags);

extern int          alarm_update_interval(context_t * ctx, int key);
extern int          event_alarm_add(context_t * ctx, time_t interval, event_alarm_flags_t flags);
extern void         event_alarm_delete(context_t * ctx, int fd);

extern int          event_bytes(int fd, size_t * pbytes);
extern ssize_t      event_read(int fd, char *buffer, size_t len);
extern int          event_waitchld(int *status, int pid);

#endif
