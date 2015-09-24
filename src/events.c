/*
 * File: events.c
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
#include <strings.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <sys/stat.h>

#include "netmanage.h"
#include "clock.h"
#include "events.h"
#include "driver.h"

#ifndef NDEBUG
//#define EVENT_DEBUG
#endif

// special secret squirrels..
// Signal handler handles sigchld internally and stores the PID and status here.

#ifndef NDEBUG
char *event_map[] = {
	"EVENT_NONE",
	"EVENT_INIT",
	"EVENT_START",
	"EVENT_READ",
	"EVENT_WRITE",
	"EVENT_EXCEPTION",
	"EVENT_SIGNAL",
	"EVENT_SEND_SIGNAL",
	"EVENT_ALARM",
	"EVENT_TICK",
	"EVENT_DATA_INCOMING",
	"EVENT_DATA_OUTGOING",
	"EVENT_RESTART",
	"EVENT_TERMINATE",
	"EVENT_CHILD",
	"EVENT_RESTARTING",
	"EVENT_MAX",
	"EVENT_STATE",
	"EVENT_LOGGING",
	0L
};
#endif

#ifndef NDEBUG
char *driver_data_type_map[] = {
	"TYPE_NONE",
	"TYPE_FD",
	"TYPE_DATA",
	"TYPE_SIGNAL",
	"TYPE_ALARM",
	"TYPE_TICK",
	"TYPE_CHILD",
	"TYPE_CUSTOM",
	0L
};
#endif

event_signals_t  event_signals;
context_t        context_table[MAX_CONTEXTS];
event_request_t  event_table[MAX_EVENT_REQUESTS];

int event_waitchld( int *status, int pid )
{
	signal_child_t *child = event_signals.child_events;
	int i;

	if (pid > 0) {
		int _pid;
		for( i = 0; i < MAX_SIGCHLD; i++ )
			if( child[i].pid == pid ) {
				*status = child[i].status;
				_pid = child[i].pid;
				child[i].pid = -1;
				return _pid;
			}
	} else {
		for( i = 0; i < MAX_SIGCHLD; i++ ) {
			if( child[i].pid > 0 ) {
				*status = child[i].status;
				return child[i].pid;
			}
		}
	}
	return -1;
}

int event_subsystem_init(void)
{
	sigset_t nothing;
	sigemptyset( &nothing );

	// Calling SIG_BLOCK with an empty mask does nothing, but returns the current signal mask.
	sigprocmask( SIG_BLOCK, &nothing, &event_signals.event_signal_default );

    // Start a private process group
    setpgid(0,0);

	return 0;
}

event_request_t *event_find( const context_t *ctx, const long fd, const unsigned int flags )
{
	unsigned int event_type = flags & EH_SPECIAL;

	int i;
	for( i = 0; i < MAX_EVENT_REQUESTS; i ++ ) {
		if (event_table[i].ctx == ctx)
			if( !( event_type ^ ( event_table[i].flags & EH_SPECIAL ) ) )
				if( event_table[i].fd == fd )
					return &event_table[i];
	}

	return 0L;
}

event_request_t *event_find_free_slot( void )
{
    int i;
    for( i = 0; i < MAX_EVENT_REQUESTS; i ++ )
        if( event_table[i].flags == EH_UNUSED )
            return &event_table[i];

    return 0L;
}

int event_alarm_add( context_t *ctx, time_t interval, event_alarm_flags_t flags )
{
	int alarm_fd = alarm_add( ctx, interval, flags );

	x_printf(ctx,"calling event add %d EH_TIMER (interval = %ld)\n",alarm_fd, interval);
	if( event_add( ctx, alarm_fd, EH_TIMER ) )
		return alarm_fd;

	return -1;
}

void event_alarm_delete( context_t *ctx, int fd )
{
	event_delete( ctx, fd, EH_TIMER );
	alarm_delete( ctx, fd );
}

event_request_t *event_set( const context_t *ctx, const long fd, const unsigned int flags )
{
	event_request_t *entry = event_find( ctx, fd, flags );

    // Reset flags, but make sure the special bits dont accidentally change.
	if( entry )
		entry->flags = (entry->flags & EH_SPECIAL) | ( flags & ~(unsigned int)EH_SPECIAL );

	return entry;
}

event_request_t *event_add( context_t *ctx, const long fd, unsigned int flags )
{
	event_request_t *entry = event_find( ctx,  fd, flags );

	d_printf("EVENT ADD called.  fd = %ld, flags = %02x (%c)\n",fd,flags,flags&EH_READ?'r':(flags&EH_WRITE?'w':(flags&EH_SPECIAL?'S':'?')));
	if( !entry )
		entry = event_find_free_slot();

    if( ! entry )
        return 0L;

	entry->fd = fd;
	entry->flags |= flags;
	entry->ctx = ctx;

	if( flags & EH_SIGNAL ) {
		// For signals, make sure the handler is installed
		if( ! sigismember( &event_signals.event_signal_mask, fd ) ) {
			struct sigaction action_handler;
			memset( &action_handler, 0, sizeof( action_handler ) );
			action_handler.sa_handler = handle_signal_event;
			action_handler.sa_flags = 0; // SA_RESTART;
			sigaction( fd, &action_handler, NULL );
			sigaddset( &event_signals.event_signal_mask, fd );
			sigdelset( &event_signals.event_signal_default, fd );
		}
	} else if( flags & EH_WANT_TICK ) {
		entry->timestamp = rel_time(0L);
	} else
		// For un-special file descriptors, force non-blocking
        if( (flags & (EH_READ|EH_WRITE)) && ! ( flags & EH_SPECIAL ) )
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

	return entry;
}

void event_delete( context_t *ctx, const long fd, event_handler_flags_t flags )
{
	event_request_t *entry = event_find( ctx, fd, flags );

	d_printf("EVENT DELETE called.  fd = %ld, flags = %02x (%c)\n",fd,flags,flags&EH_READ?'r':(flags&EH_WRITE?'w':(flags&EH_SPECIAL?'S':'?')));

	if( entry ) {
		if( flags )
			entry->flags &= ~(unsigned int)flags;
		else
			entry->flags = EH_UNUSED;

		if( entry->flags == EH_UNUSED ) {
			d_printf("No events for this file descriptor, disabling\n");
			entry->fd = -1;
		}
		d_printf("EVENT remaining - fd = %ld, flags = %02x (%c)\n",entry->fd,entry->flags,entry->flags&EH_READ?'r':(entry->flags&EH_WRITE?'w':(entry->flags&EH_SPECIAL?'S':'?')));
	}
}

int create_event_set( fd_set *readfds, fd_set *writefds, fd_set *exceptfds, int *max )
{
	int count = 0;
	int i;

	*max = 0;

	FD_ZERO( readfds );
	FD_ZERO( writefds );
	FD_ZERO( exceptfds );

#ifdef EVENT_DEBUG
	d_printf("creating event set [");
#endif
	for( i = 0; i < MAX_EVENT_REQUESTS; i++ ) {
		if( event_table[i].flags ) {
#ifdef EVENT_DEBUG
#ifndef NDEBUG
			printf(" %d(%s):%ld",i,event_table[i].ctx->name, event_table[i].fd);
			if( event_table[i].flags & EH_READ )
				printf("r");
			if( event_table[i].flags & EH_WRITE )
				printf("w");
			if( event_table[i].flags & EH_EXCEPTION )
				printf("e");
			if( event_table[i].flags & EH_TIMER )
				printf("t");
			if( event_table[i].flags & EH_TIMER_FD )
				printf("T");
			if( event_table[i].flags & EH_SIGNAL )
				printf("s");
#endif
#endif
			if( event_table[i].flags & EH_SPECIAL ) {

				if( event_table[i].flags & EH_TIMER_FD )
					FD_SET( (unsigned int) event_table[i].fd, readfds), count ++;

				if( event_table[i].flags & EH_TIMER )
					count ++;

				if( event_table[i].flags & EH_SIGNAL )
					count ++;

			} else {

				if( event_table[i].flags & EH_READ )
					FD_SET( (unsigned int) event_table[i].fd, readfds), count ++;
				if( event_table[i].flags & EH_WRITE )
					FD_SET( (unsigned int) event_table[i].fd, writefds), count ++;
				if( event_table[i].flags & EH_EXCEPTION )
					FD_SET( (unsigned int) event_table[i].fd, exceptfds), count ++;

				if( event_table[i].fd >= *max )
					*max = event_table[i].fd+1;
			}
		} else {
#ifndef NDEBUG
			//printf(" %d:-",i);
#endif
		}
	}
#ifdef EVENT_DEBUG
#ifndef NDEBUG
	printf("]\n");
#endif
#endif

	return count;
}

#ifndef NDEBUG
#ifdef mips
char           *sigmap[] = {
	"NONE", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT",
	"EMT", "FPE", "KILL", "BUS", "SEGV", "SYS", "PIPE", "ALRM", "TERM", "USR1", "USR2",
	"CHLD", "PWR", "WINCH", "URG", "IO", "STOP", "TSTP", "CONT", "TTIN", "TTOU", "VTALRM", "PROF", "XCPU", "XFZS"
};
#else
char           *sigmap[] = {
	"NONE", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT",
	"BUS", "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM", "STKFLT", "CHLD",
	"CONT", "STOP", "TSTP", "TTIN", "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "IO", "PWR", "SYS"
};
#endif
#endif

void handle_signal_event( int sig_event )
{
	sigaddset( &event_signals.event_signal_pending, sig_event );
	event_signals.event_signal_pending_count ++;

	// Special case - sigchild is handled internally
	if( sig_event == SIGCHLD ) {
		int status, pid;
		pid = waitpid(-1, &status, WNOHANG );
		if( pid > 0 ) {
			int i;
			for( i = 0; i < MAX_SIGCHLD; i++ )
				if( event_signals.child_events[i].pid <= 0 ) {
					event_signals.child_events[i].pid = pid;
					event_signals.child_events[i].status = status;
					return;
				}
		} else
			d_printf("Hang on... SIGCHLD with no CHILD!! (maybe because of %s)\n", strerror(errno));
	}

	return;
}

int handle_pending_signals( void )
{
	//
	// Note, signals are normally blocked while doing this when called from event_loop()
	//
	sigset_t temp_signals = event_signals.event_signal_pending;

	sigemptyset( &event_signals.event_signal_pending );
	event_signals.event_signal_pending_count = 0;

	int i;
	for( i = 0; i < MAX_EVENT_REQUESTS; i ++ ) {
		if( ( event_table[i].flags & EH_SIGNAL ) == EH_SIGNAL && sigismember( &temp_signals, event_table[i].fd )) {

			driver_data_t data = { TYPE_SIGNAL, 0L, {} };
			data.event_signal = event_table[i].fd;

			event_table[i].ctx->driver->emit( event_table[i].ctx, EVENT_SIGNAL, &data );
		}
	}

	return 0;
}

int handle_event_alarm(event_request_t *event)
{
	int alarm_fd = event->fd;

	if( alarm_table[alarm_fd].flags & ALARM_INTERVAL )
		alarm_update_interval( event->ctx, alarm_fd );
	else
		event_alarm_delete( event->ctx, alarm_fd );

	return 0;
}

int handle_event_set(fd_set * readfds, fd_set * writefds, fd_set * exceptfds)
{
	time_t              now = rel_time(0L);
	int                 i;

	for (i = 0; i < MAX_EVENT_REQUESTS; i++) {
		if (event_table[i].flags && ((event_table[i].flags & EH_SPECIAL) == 0)) {

			driver_data_t       data = { TYPE_FD, 0L, {}
			};
			data.event_request.fd = event_table[i].fd;
			data.event_request.flags = event_table[i].flags;

			if ((event_table[i].flags & EH_EXCEPTION) && FD_ISSET((unsigned int)event_table[i].fd, exceptfds)) {
				//d_printf("Exception event for %s\n",event_table[i].ctx->name);
				event_table[i].ctx->driver->emit(event_table[i].ctx, EVENT_EXCEPTION, &data);
			}

			if ((event_table[i].flags & EH_WRITE) && FD_ISSET((unsigned int)event_table[i].fd, writefds)) {
				//d_printf("Write event for %s\n",event_table[i].ctx->name);
				event_table[i].ctx->driver->emit(event_table[i].ctx, EVENT_WRITE, &data);
			}

			if ((event_table[i].flags & EH_READ) && FD_ISSET((unsigned int)event_table[i].fd, readfds)) {
				//d_printf("Read event for %s\n",event_table[i].ctx->name);
				event_table[i].ctx->driver->emit(event_table[i].ctx, EVENT_READ, &data);
			}

		} else if (event_table[i].flags & EH_TIMER) {

			if (alarm_table[event_table[i].fd].event_time <= now) {
				alarm_table[event_table[i].fd].flags |= ALARM_FIRED;

				driver_data_t       data = { TYPE_ALARM, 0, {} };
				data.event_alarm = event_table[i].fd;
				event_table[i].ctx->driver->emit(event_table[i].ctx, EVENT_ALARM, &data);

				// If the event handler changes the alarm in any way, the 'fired' flag is cleared
				if (alarm_table[event_table[i].fd].flags & ALARM_FIRED) {
					//d_printf("ALARM event for %s\n",event_table[i].ctx->name);
					handle_event_alarm(&event_table[i]);
				}
			}

		}
	}
	return 0;
}

int handle_timer_events()
{
	int i;

	driver_data_t tick = { TYPE_TICK, 0L, {} };
	tick.event_tick = rel_time(0L);

	//d_printf("Checking tick requests..\n");
	// All drivers get the same 'tick' timestamp, even if some drivers take time to process the tick
	for(i = 0; i < MAX_EVENT_REQUESTS; i++ )
		if( ((event_table[i].flags & EH_WANT_TICK) == EH_WANT_TICK) && ((event_table[i].flags & EH_SPECIAL) == 0 )) {
			//d_printf("event[%d] Delivering tick to %s if %ld > %ld\n",i, event_table[i].ctx->name, tick.event_tick, event_table[i].timestamp );
			if( tick.event_tick > event_table[i].timestamp ) {
				event_table[i].ctx->driver->emit( event_table[i].ctx, EVENT_TICK, &tick );
				event_table[i].timestamp = tick.event_tick + (time_t) event_table[i].fd;
			}
		}

	return 0;
}

int event_loop( long timeout )
{
	int max_fd = 0;

	fd_set fds_read, fds_write, fds_exception;
	if( ! create_event_set( &fds_read, &fds_write, &fds_exception, &max_fd ) )
		return -1;

	time_t now = rel_time(0L);
	long alarm_time = alarm_getnext();

	// Reduce 'timeout' to ensure the next scheduled alarm occurs on time
	if( alarm_time >= 0 ) {
		if( alarm_time > now ) {
			if( timeout > ( alarm_time - now ))
				timeout = alarm_time - now;
		} else
			timeout = 0;
	}

#ifdef USE_PSELECT
	// If I know there are signals which need processing, reset the select timeout to 0
	// Signals are normally blocked, so these would be manufactured events.
	struct timespec tm = { timeout / 1000, (timeout % 1000) * 1000 * 1000 };

	if( event_signals.event_signal_pending_count )
		tm.tv_nsec = tm.tv_sec = 0;

	int rc = pselect( max_fd, &fds_read, &fds_write, &fds_exception, &tm, &event_signals.event_signal_default );
#else
	// expect queued signals to occur immediately
	sigprocmask( SIG_SETMASK, &event_signals.event_signal_default, NULL );
	sleep(0);

	if( event_signals.event_signal_pending_count )
		timeout = 0;

	struct timeval tm = { timeout / 1000, (timeout % 1000) * 1000 };

#if 0
#ifndef NDEBUG
	int i = 0;
	printf("READ: ");
	for(i=0;i<64;i++)
		if(FD_ISSET(i,&fds_read)) {
			printf("%d ",i);
			if( fcntl( i, F_GETFL ) < 0 )
				printf("*");
		}
	printf("WRITE: ");
	for(i=0;i<64;i++)
		if(FD_ISSET(i,&fds_write)) {
			printf("%d ",i);
			if( fcntl( i, F_GETFL ) < 0 )
				printf("*");
		}
	printf("EXCEPTION: ");
	for(i=0;i<64;i++)
		if(FD_ISSET(i,&fds_exception)) {
			printf("%d ",i);
			if( fcntl( i, F_GETFL ) < 0 )
				printf("*");
		}
	printf("\n");
#endif
#endif

#ifndef NDEBUG
	//printf("calling select...");fflush(stdout);
#endif
	int rc = select( max_fd, &fds_read, &fds_write, &fds_exception, &tm );
#ifndef NDEBUG
	//printf("done\n");fflush(stdout);
#endif
#endif

	sigprocmask(SIG_BLOCK, &event_signals.event_signal_mask, NULL);

	if( rc < 0 ) {
		d_printf("(p)select returned %d (errno = %d - %s)\n",rc, errno, strerror(errno));

		if( (errno != EINTR)
#ifdef mips
				&& (errno != ENOENT)
#endif
				)
			return -1; // timeout - nothing to do
		else
			return 0;
	}

	if( event_signals.event_signal_pending_count ) {
		//d_printf("Calling handle_pending_signals()\n");
		handle_pending_signals();
	}

	//d_printf("Calling handle_event_set()\n");
	rc = handle_event_set( &fds_read, &fds_write, &fds_exception );

	if( !rc ) {
		//d_printf("Calling handle_timer_events()\n");
		handle_timer_events();
	}

	return 0;
}

int event_bytes( int fd, size_t *pbytes )
{
	*pbytes = 0;
	return ioctl( fd, FIONREAD, pbytes );
}

ssize_t event_read( int fd, char *buffer, size_t len )
{
	ssize_t rc;
	while( (rc = read( fd, buffer, len )) < 0 ) {
		d_printf("Read returned error.  %d:%s(%d)\n",rc,strerror(errno),errno);
		if( errno != EAGAIN )
			return rc;
	}
	return rc;
}

ssize_t emit( context_t *ctx, event_t event, driver_data_t *event_data )
{
	if( ctx && (ctx->state != CTX_UNUSED) && ctx->driver )
		return ctx->driver->emit( ctx, event,event_data ? event_data : DRIVER_DATA_NONE );
	else
		d_printf("emit(%s->%s) called for bad context %p (owner = %s)\n",event_data?event_data->source->name:"unknown",ctx?ctx->name:"unknown",ctx,(ctx&&ctx->owner)?ctx->owner->name:"NA" );

	return -1;
}

ssize_t emit2( context_t *ctx, event_t event, driver_data_t *event_data )
{
	if( ctx && ctx->owner )
		return emit( ctx->owner, event, event_data );
	else
		d_printf("emit2(%s->%s) called for bad context %p (owner = %s)\n",event_data?event_data->source->name:"unknown",ctx?ctx->name:"unknown",ctx,(ctx&&ctx->owner)?ctx->owner->name:"NA" );

	return -1;
}
