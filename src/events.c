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
#include <time.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/fcntl.h>

#include "netmanage.h"
#include "events.h"
#include "driver.h"

// special secret squirrels..
// Signal handler handles sigchld internally and stores the PID and status here.

#ifndef NDEBUG
char *event_map[] = {
	"EVENT_NONE",
	"EVENT_INIT",
	"EVENT_READ",
	"EVENT_WRITE",
	"EVENT_EXCEPTION",
	"EVENT_SIGNAL",
	"EVENT_TICK",
	"EVENT_DATA_INCOMING",
	"EVENT_DATA_OUTGOING",
	"EVENT_TERMINATE",
	"EVENT_CHILD_ADDED",
	"EVENT_CHILD_REMOVED",
	"EVENT_PARENT_ADDED",
	"EVENT_PARENT_REMOVED",
	"EVENT_MAX",
	0L
};
#endif

#ifndef NDEBUG
char *driver_data_type_map[] = {
	"TYPE_NONE",
	"TYPE_FD",
	"TYPE_DATA",
	"TYPE_SIGNAL",
	"TYPE_TICK",
	0L
};
#endif

typedef struct event_child_list_s {
	int pid;
	int status;
} event_child_t;

typedef struct event_signals_s {
	sigset_t event_signal_mask;
	sigset_t event_signal_pending;
	sigset_t event_signal_default;

	int      event_signal_pending_count;
    int      event_child_last;
	event_child_t child_events[MAX_SIGCHLD];
} event_signals_t;

event_signals_t event_signals;

context_t        context_table[MAX_CONTEXTS];
event_request_t  event_table[MAX_EVENT_REQUESTS];

int event_waitchld( int *status, int pid )
{
	event_child_t *child = event_signals.child_events;
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
	return 0;
}

event_request_t *event_find( const context_t *ctx, const int fd, const event_handler_flags_t flags )
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

event_request_t *event_set( const context_t *ctx, const int fd, const event_handler_flags_t flags )
{
	event_request_t *entry = event_find( ctx, fd, flags );

    // Reset flags, but make sure the special bits dont accidentally change.
	if( entry )
		entry->flags = (entry->flags & EH_SPECIAL) | ( flags & ~(unsigned int)EH_SPECIAL );

	return entry;
}

event_request_t *event_add( context_t *ctx, const int fd, event_handler_flags_t flags )
{
	event_request_t *entry = event_find( ctx,  fd, flags );

	if( !entry )
		entry = event_find_free_slot();

    if( ! entry )
        return 0L;

	entry->fd = fd;
	entry->flags = flags;
	entry->ctx = ctx;

	if( flags & EH_SIGNAL ) {
		if( ! sigismember( &event_signals.event_signal_mask, fd ) ) {
			struct sigaction action_handler;
			memset( &action_handler, 0, sizeof( action_handler ) );
			action_handler.sa_handler = handle_signal_event;
			action_handler.sa_flags = 0; // SA_RESTART;
			sigaction( fd, &action_handler, NULL );
			sigaddset( &event_signals.event_signal_mask, fd );
			sigdelset( &event_signals.event_signal_default, fd );
		}
	} else 
        if( (flags & (EH_READ|EH_WRITE)) && ! ( flags & EH_SPECIAL ) )
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

	return entry;
}

void event_delete( context_t *ctx, int fd, event_handler_flags_t flags )
{
	event_request_t *entry = event_find( ctx, fd, flags );

	if( entry ) {
		entry->flags = EH_UNUSED;
		entry->fd = -1;
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

	for( i = 0; i < MAX_EVENT_REQUESTS; i++ )
	{
		if( event_table[i].flags ) {
			d_printf(" ** Adding registered events for %s\n",event_table[i].ctx->name );

			if( event_table[i].flags & EH_SPECIAL ) {
				if( event_table[i].flags & EH_SIGNAL )
					count ++;
			} else {
				if( event_table[i].flags & EH_READ )
					FD_SET(event_table[i].fd, readfds), count ++;
				if( event_table[i].flags & EH_WRITE )
					FD_SET(event_table[i].fd, writefds), count ++;
				if( event_table[i].flags & EH_EXCEPTION )
					FD_SET(event_table[i].fd, exceptfds), count ++;
				if( event_table[i].fd >= *max )
					*max = event_table[i].fd+1;
			}
		}
	}

	return count;
}

char           *sigmap[] = {
	"NONE", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM",
	"STKFLT", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "IO", "PWR", "SYS"
};

void handle_signal_event( int sig_event )
{
	d_printf(" - SIGNAL SIG%s(%d)\n",sigmap[sig_event],sig_event);

	sigaddset( &event_signals.event_signal_pending, sig_event );
	event_signals.event_signal_pending_count ++;

	// Special case - sigchild is handled internally
	if( sig_event == SIGCHLD ) {
		d_printf("\n *\n * REAPING A CHILD PROCESS\n *\n");
		int status, pid;
		pid = waitpid(-1, &status, WNOHANG );
		if( pid > 0 ) {
			d_printf("Child PID = %d\n",pid);
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
			driver_data_t data = { TYPE_SIGNAL, .event_signal = event_table[i].fd };
			event_table[i].ctx->driver->emit( event_table[i].ctx, EVENT_SIGNAL, &data );
		}
	}

	return 0;
}

int handle_event_set( fd_set *readfds, fd_set *writefds, fd_set *exceptfds )
{
	int i;
	for( i = 0; i < MAX_EVENT_REQUESTS; i++ ) {
		if( event_table[i].flags && ((event_table[i].flags & EH_SPECIAL) == 0 )) {
			driver_data_t data = { TYPE_FD, .event_request.fd = event_table[i].fd, .event_request.flags = event_table[i].flags };
			if( (event_table[i].flags & EH_EXCEPTION) &&  FD_ISSET( event_table[i].fd, exceptfds ) ) 
				event_table[i].ctx->driver->emit( event_table[i].ctx, EVENT_EXCEPTION, &data );
			if( (event_table[i].flags & EH_WRITE) &&  FD_ISSET( event_table[i].fd, writefds ) )
				event_table[i].ctx->driver->emit( event_table[i].ctx,  EVENT_WRITE, &data );
			if( (event_table[i].flags & EH_READ) &&  FD_ISSET( event_table[i].fd, readfds ) )
				event_table[i].ctx->driver->emit( event_table[i].ctx, EVENT_READ, &data );
		}
	}
	return 0;
}

int handle_timer_events()
{
	int i;

	driver_data_t tick = { TYPE_TICK, .event_tick = time(0L) };

	// All drivers get the same 'tick' timestamp, even if some drivers take time to process the tick
	for(i = 0; i < MAX_EVENT_REQUESTS; i++ )
		if( ((event_table[i].flags & EH_WANT_TICK) == EH_WANT_TICK) && ((event_table[i].flags & EH_SPECIAL) == 0 )) {
			event_table[i].ctx->driver->emit( event_table[i].ctx, EVENT_TICK, &tick );
		}
	return 0;
}

int event_loop( int timeout )
{
	fd_set fds_read, fds_write, fds_exception;
	struct timespec tm = { timeout / 1000, (timeout % 1000) * 1000 };
	int max_fd = 0;

	if( ! create_event_set( &fds_read, &fds_write, &fds_exception, &max_fd ) )
		return -1;

	// If I know there are signals which need processing, reset the select timeout to 0
	// Signals are normally blocked, so these would be manufactured events.
	if( event_signals.event_signal_pending_count )
		tm.tv_nsec = tm.tv_sec = 0;

	int rc = pselect( max_fd, &fds_read, &fds_write, &fds_exception, &tm, &event_signals.event_signal_default );
	sigprocmask(SIG_BLOCK, &event_signals.event_signal_mask, NULL);

	if( rc < 0 ) {
		d_printf("(p)select returned %d (errno = %d - %s)\n",rc, errno, strerror(errno));
		if( errno != EINTR )
			return -1; // timeout - nothing to do
		else
			return 0;
	}

	if( event_signals.event_signal_pending_count )
		handle_pending_signals();

	rc = handle_event_set( &fds_read, &fds_write, &fds_exception );

	if( !rc )
		handle_timer_events();

	return 0;
}

int event_bytes( int fd, size_t *pbytes )
{
	//d_printf("Calling FIONREAD on fd %d\n",fd);
	*pbytes = 0;
	if( ioctl( fd, FIONREAD, pbytes ) < 0 ) {
		d_printf("Error with FIONREAD.  err = %s\n",strerror(errno));
		return -1;
	}

	return 0;
}

ssize_t event_read( int fd, char *buffer, size_t len )
{
	//d_printf("Calling read(%d, %p, %ld)\n",fd,buffer,len);
	ssize_t rc;
	while( (rc = read( fd, buffer, len )) < 0 ) {
		d_printf("Read returned error.  %ld:%s(%d)\n",rc,strerror(errno),errno);
		if( errno != EAGAIN )
			return rc;
	}
	return rc;
}

int emit( context_t *ctx, event_t event, driver_data_t *event_data )
{
	if( ctx->driver )
		return ctx->driver->emit( ctx, event,event_data ? event_data : DRIVER_DATA_NONE );

	return -1;
}
