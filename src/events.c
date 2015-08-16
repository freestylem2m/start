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

event_handler_list_t *event_handler_list;

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
	struct event_child_list_s *next;
} event_child_list_t;

typedef struct event_signals_s {
	sigset_t event_signal_mask;
	sigset_t event_signal_pending;
	int      event_signal_pending_count;
	event_child_list_t *child_list;
	int      prune_pending;
} event_signals_t;

event_signals_t event_handler_signals;

void child_list_add( int status, int pid )
{
	event_child_list_t **child = & event_handler_signals.child_list;

	event_child_list_t *new = (event_child_list_t *) calloc( sizeof(event_child_list_t), 1 );

	new->status = status;
	new->pid = pid;
	new->next = *child;
	*child = new;
}

#if 0
void child_list_delete( int pid )
{
	event_child_list_t **child = & event_handler_signals.child_list;

	while( *child && (*child)->pid != pid )
		child = & (*child)->next;

	if( *child && (*child)->pid == pid ) {
		event_child_list_t *tail = (*child)->next;
		free( *child );
		*child = tail;
	}
}
#endif

int event_waitchld( int *status, int pid )
{
	event_child_list_t **child = &event_handler_signals.child_list;
	int             _pid;

	if (pid > 0) {
		while (*child && (*child)->pid != pid)
			child = &(*child)->next;

		if (*child) {
			*status = (*child)->status;
			_pid = (*child)->pid;
			event_child_list_t *tail = (*child)->next;
			free((void *)*child);
			*child = tail;

			return _pid;
		}
	}

	if (*child) {
		*status = (*child)->status;
		return (*child)->pid;
	} else
		return -1;
}


const event_handler_list_t *find_event_handler( const char *classname )
{
	event_handler_list_t *e = event_handler_list;
	int match = -1;

	while( e && ((match = strncasecmp( classname, e->name, MAX_CLASS_NAME ) )) < 0 )
		e = e->next;

	if( match )
		return 0L;

	return e;
}

event_handler_list_t *add_event_handler( const char *classname, context_t *context, event_handler_t handler, event_handler_flags_t flags )
{
	event_handler_list_t **e = &event_handler_list;
	int match = -1;

	while( (*e) && ((match = strncasecmp( classname, (*e)->name, MAX_CLASS_NAME ) )) < 0 )
		e = &(*e)->next;

	if( match ) {
		event_handler_list_t *n = (event_handler_list_t *) calloc( 1, sizeof( event_handler_list_t ) );
		n->next = *e;
		*e = n;
	}

	if( (*e)->name )
		free( (void *) (*e)->name );

	(*e)->name = strdup( classname );
	(*e)->context = context;
	(*e)->handler = handler;
	(*e)->flags = flags & (unsigned)~EH_DELETED;

	return *e;
}

event_handler_list_t *register_event_handler( const char *classname, context_t *context, event_handler_t handler, event_handler_flags_t flags)
{
	if( find_event_handler( classname ) ) {
		fprintf(stderr,"WARNING: Event handler for class %s already exists\n",classname);
		return 0L;
	}

	return add_event_handler( classname, context, handler, flags );

}

void deregister_event_handler( event_handler_list_t *event )
{
	event_handler_list_t **e = &event_handler_list;

	while( e && *e && (*e != event ))
		e = &(*e)->next;

	if( e && *e && ( *e == event )) {
		event_handler_list_t *tail = (*e)->next;
		fd_list_t *l = (*e)->files;
		while( l ) {
			l->flags |= EH_DELETED;
			l=l->next;
		}
		event_prune( *e );
		free( (void *) (*e)->name );
		free( (void *) *e );
		*e = tail;
	}

}

fd_list_t *find_event_entry( const event_handler_list_t *handler_list, const int fd, event_handler_flags_t flags )
{
	unsigned int event_type = flags & EH_SPECIAL;

	fd_list_t *l = handler_list->files;

	while(l) {
		if( !( event_type ^ ( l->flags & EH_SPECIAL ) ) )
			if( l->fd == fd )
				return l;

		l = l->next;
	}

	return 0L;
}

fd_list_t *event_set( event_handler_list_t *handler_list, int fd, event_handler_flags_t flags )
{
	fd_list_t *entry = find_event_entry( handler_list, fd, flags );

	if( entry ) {
		entry->flags = flags;
	}
	return entry;
}

fd_list_t *event_add( event_handler_list_t *handler_list, int fd, event_handler_flags_t flags )
{
	fd_list_t *entry = find_event_entry( handler_list, fd, flags );

	if( !entry ) {
		entry = calloc( sizeof( fd_list_t ), 1 );

		if( ! entry )
			return 0L;

		entry->next = handler_list->files;
		handler_list->files = entry;
	}

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	entry->fd = fd;
	entry->flags = flags & (unsigned) ~EH_DELETED;

	if( flags & EH_SIGNAL ) {
		if( ! sigismember( &event_handler_signals.event_signal_mask, fd ) ) {
			struct sigaction action_handler;
			memset( &action_handler, 0, sizeof( action_handler ) );
			action_handler.sa_handler = handle_signal_event;
			action_handler.sa_flags = 0;
			//action_handler.sa_flags = SA_RESTART;
			sigaction( fd, &action_handler, NULL );
			sigaddset( &event_handler_signals.event_signal_mask, fd );
		}
	}

	return entry;
}

void event_delete( event_handler_list_t *handler_list, int fd, event_handler_flags_t flags )
{
	fd_list_t *entry = find_event_entry( handler_list, fd, flags );
	if( entry ) {
		entry->flags |= EH_DELETED;
		entry->fd = -1;
		event_handler_signals.prune_pending ++;
	}
}

void event_prune( event_handler_list_t *handler_list )
{
	fd_list_t **entry = & handler_list->files;

	while( entry && *entry ) {
		if( (*entry)->flags & EH_DELETED ) {
			fd_list_t *tail = (*entry)->next;
			free( (void *) *entry );
			*entry = tail;
		}
		else
			entry = & (*entry)->next;
	}
}

int create_event_set( fd_set *readfds, fd_set *writefds, fd_set *exceptfds, int *max )
{
	int count = 0;

	FD_ZERO( readfds );
	FD_ZERO( writefds );
	FD_ZERO( exceptfds );
	*max = 0;

	if( event_handler_signals.prune_pending ) {
		event_handler_list_t *e = event_handler_list;
		while( e ) {
			event_prune( e );
			e = e->next;
		}
	}

	event_handler_list_t *e = event_handler_list;
	while( e ) {
		d_printf(" ** Adding registered events for %s\n",e->name );
		fd_list_t *fdl = e->files;
		while( fdl ) {
			if( fdl->flags & EH_DELETED ) {
				event_handler_signals.prune_pending ++;
			} else {
				if( fdl->flags & EH_READ )
					FD_SET(fdl->fd, readfds), count ++;
				if( fdl->flags & EH_WRITE )
					FD_SET(fdl->fd, writefds), count ++;
				if( fdl->flags & EH_EXCEPTION )
					FD_SET(fdl->fd, exceptfds), count ++;
				if( fdl->flags & EH_SIGNAL )
					count ++;

				if( fdl->fd >= *max )
					*max = fdl->fd+1;
			}
			fdl = fdl->next;
		}
		e = e->next;
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

	sigaddset( &event_handler_signals.event_signal_pending, sig_event );
	event_handler_signals.event_signal_pending_count ++;

	// Special case - sigchild is handled internally
	if( sig_event == SIGCHLD ) {
		d_printf("\n *\n * REAPING A CHILD PROCESS\n *\n");
		int status, pid;
		pid = waitpid(-1, &status, WNOHANG );
		d_printf("Child PID = %d\n",pid);
		if( pid > 0 )
			child_list_add( status, pid );
	}
}

int handle_pending_signals( void )
{
	//
	// Note, signals are normally blocked while doing this when called from event_loop()
	//
	sigset_t temp_signals = event_handler_signals.event_signal_pending;

	sigemptyset( &event_handler_signals.event_signal_pending );
	event_handler_signals.event_signal_pending_count = 0;

	event_handler_list_t *e = event_handler_list;

	while( e ) {
		fd_list_t *fdl = e->files;
		while( fdl ) {
			if( fdl->flags & EH_DELETED ) {
				event_handler_signals.prune_pending ++;
			} else {
				if( fdl->flags & EH_SIGNAL && sigismember( &temp_signals, fdl->fd ) ) {
					driver_data_t data = { TYPE_SIGNAL, .event_signal = fdl->fd };
					e->handler( e->context, EVENT_SIGNAL, &data );
				}
			}
			fdl = fdl->next;
		}
		e = e->next;
	}

	return 0;
}

int handle_event_set( fd_set *readfds, fd_set *writefds, fd_set *exceptfds )
{
	//
	// Make the following signal mask handover atomic by blocking signals
	//
	event_handler_list_t *e = event_handler_list;
	e = event_handler_list;
	while( e ) {
		fd_list_t *fdl = e->files;
		while( fdl ) {
			if( fdl->flags & EH_DELETED ) {
				event_handler_signals.prune_pending ++;
			} else {
				driver_data_t data = { TYPE_FD, .event_fd.fd = fdl->fd, .event_fd.flags = fdl->flags };
				if( (fdl->flags & EH_EXCEPTION) &&  FD_ISSET( fdl->fd, exceptfds ) ) 
					e->handler( e->context, EVENT_EXCEPTION, &data );
				if( (fdl->flags & EH_WRITE) &&  FD_ISSET( fdl->fd, writefds ) )
					e->handler( e->context,  EVENT_WRITE, &data );
				if( (fdl->flags & EH_READ) &&  FD_ISSET( fdl->fd, readfds ) )
					e->handler( e->context, EVENT_READ, &data );

			}
			fdl = fdl->next;
		}
		e = e->next;
	}
	return 0;
}

int handle_timer_events()
{
	event_handler_list_t *e = event_handler_list;

	driver_data_t tick = { TYPE_TICK, { .event_tick = time(0L) } };

	while( e ) {
		if( ! (e->flags & EH_DELETED) )
			if( e->flags & EH_WANT_TICK )
				e->handler( e->context, EVENT_TICK, &tick );
		e = e->next;
	}
	return 0;
}

int event_loop( int timeout )
{
	fd_set fds_read, fds_write, fds_exception;
	struct timespec tm = { timeout / 1000, (timeout % 1000) * 1000 };
	//struct timeval tm = { timeout / 1000, (timeout % 1000) };
	sigset_t sigmask;
	sigemptyset( &sigmask );

	int max_fd = 0;
	if( ! create_event_set( &fds_read, &fds_write, &fds_exception, &max_fd ) )
		return -1;

	// If I know there are signals which need processing, reset the select timeout to 0
	if( event_handler_signals.event_signal_pending_count )
		tm.tv_nsec = tm.tv_sec = 0;
		//tm.tv_usec = tm.tv_sec = 0;

#ifndef NDEBUG
	d_printf("Entering select...");
	fflush(stdout);
#endif
	//sigprocmask(SIG_UNBLOCK, &event_handler_signals.event_signal_mask, NULL);
	int rc = pselect( max_fd, &fds_read, &fds_write, &fds_exception, &tm, &sigmask );
	//int rc = select( max_fd, &fds_read, &fds_write, &fds_exception, &tm );
	sigprocmask(SIG_BLOCK, &event_handler_signals.event_signal_mask, NULL);
#ifndef NDEBUG
	d_printf("done\n");
	fflush(stdout);
#endif

	if( rc < 0 ) {
		d_printf("(p)select returned %d (errno = %d - %s)\n",rc, errno, strerror(errno));
		if( errno != EINTR )
			return -1; // timeout - nothing to do
		else
			return 0;
	}

	if( event_handler_signals.event_signal_pending_count )
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

int emit_parent( context_t *ctx, event_t event, driver_data_t *event_data )
{
	if( ctx->parent )
		return emit( ctx->parent, event | EH_CHILD, event_data );
	else
		d_printf(" ** WARNING:  emit_parent() called without a parent.\n");

	return -1;
}

int emit_child( context_t *ctx, event_t event, driver_data_t *event_data )
{
	if( ctx->child )
		return emit( ctx->child, event | EH_PARENT, event_data );
	else
		d_printf(" ** WARNING:  emit_child() called without a child.\n");

	return -1;
}
