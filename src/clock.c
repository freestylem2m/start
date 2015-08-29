/*
 * File: clock.c
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
 * Various clock functions
 *
 *
 */

#include <stdio.h>
#include <sys/types.h>

#ifdef i386
#include <sys/timerfd.h>
#endif

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <alloca.h>

#include "netmanage.h"
#include "clock.h"

event_alarm_t  alarm_table[MAX_ALARM];

int alarm_delete_list( int i )
{
	event_alarm_t *n = &alarm_table[0];

	while( n->event_next != i )
		n = & alarm_table[n->event_next];

	n->event_next = alarm_table[ i ].event_next;
	return i;
}

int alarm_insert_list( int i )
{
	// Scan table looking for a place to insert.  This only scans existing
	// entries, stopping at EOL or when timestamp > new alarm
	event_alarm_t *j = & alarm_table[0];

	while( j->event_next ) {
		if( alarm_table[j->event_next].event_time > alarm_table[i].event_time )
			break;
		j = & alarm_table[ j->event_next ];
	}

	// If this is the end of the list, insert after, otherwise, its always before
	if( j->event_next && alarm_table[ j->event_next ].event_time > alarm_table[i].event_time ) {
		alarm_table[i].event_next = j->event_next;
		j->event_next = i;
	} else
		j->event_next = i;

	return i;
}

/*
 * Create an alarm event, and insert it into the 'list' of alarms.
 * return the 'index' into the table as the handle of the alarm
 */
int alarm_add(context_t *ctx, time_t interval, event_alarm_flags_t flags)
{
	int i;
	time_t now;
	
	if( !flags )
		return -1;

	now = rel_time(0L);

	// Scan table looking for a free slot.
	for(i = 1; (i < MAX_ALARM) && alarm_table[i].flags; i++);

	if( i == MAX_ALARM )
		return -1;

	// Populate the alarm entry
	alarm_table[i].duration = interval;
	alarm_table[i].flags = flags;
	alarm_table[i].event_time = now + interval;
	alarm_table[i].ctx = ctx;
	alarm_table[i].event_next = 0;
	
	return alarm_insert_list( i );
}

/*
 * Delete an existing alarm by index.
 */
int alarm_delete(context_t *ctx, int key)
{
	if( key < MAX_ALARM && alarm_table[key].flags && ( alarm_table[key].ctx == ctx ) ) {
		alarm_delete_list( key );
		alarm_table[key].flags = 0;
		alarm_table[key].ctx = 0L;
		return 0;
	}

	return -1;
}

/*
 * Get the time offset to the next alarm.   This is essentially the time to
 * the first entry in the list (alarm_table[0].event_next point to first event)
 */
long alarm_getnext(void)
{
	if( alarm_table[ 0 ].event_next ) {
		return alarm_table[ alarm_table[ 0 ].event_next ].event_time;
	}
	return -1;
}

/*
 * Update the alarm with a new interval or new flags
 */
int alarm_update(context_t *ctx, int key, time_t interval, event_alarm_flags_t flags)
{
	if( (key > MAX_ALARM) || (!alarm_table[key].flags) || (alarm_table[key].ctx != ctx) || (!flags) )
		return -1;

	alarm_delete_list( key );

	// populate alarm entry with new settings
	alarm_table[key].duration = interval;
	alarm_table[key].event_time = rel_time(0L) + interval;
	alarm_table[key].flags = flags;

	return alarm_insert_list( key );
}

/*
 * Update the alarm with a new interval or new flags
 */
int alarm_update_interval(context_t *ctx, int key )
{
	event_alarm_t *p;
	time_t now = rel_time(0L);

	if( (key > MAX_ALARM) || (!(alarm_table[key].flags & ALARM_INTERVAL)) || (alarm_table[key].ctx != ctx))
		return -1;

	p = &alarm_table[key] ;
	p->event_time = now + p->duration;
	p->flags &= ~(unsigned int)ALARM_FIRED;

	// short circuit list reording where possible
	if( (! p->event_next) || (p->duration <= alarm_table[p->event_next].event_time) )
		return key;

	alarm_delete_list( key );
	return alarm_insert_list( key );
}

/*
 * rel_time() differs from 'time()' by the use of a non-wallclock based timer.
 * The use of clock_gettime( CLOCK_MONOTONIC ) ensures that timeout and periodic
 * events which have to occur at specified intervals are unaffected by changes in
 * wall clock time, such as calling the 'date' command, or NTP, or refreshing the
 * system clock from the hardware RTC.
 *
 * ands it always returns milliseconds.   This allows the alarms and timers to be
 * managed in milliseconds, and all timeouts based on rel_time() have millisecond
 * resolution.
 */

time_t rel_time(time_t *ptr)
{
	struct timespec reltime;

	clock_gettime( CLOCK_MONOTONIC, & reltime );

	time_t _time = (reltime.tv_sec * 1000) + (reltime.tv_nsec / 1000000 );

	if( ptr )
		*ptr = _time;

	return _time;
}

#ifdef i386
/*
 * With 'timerfd' alarms are simply file descriptors tied to a clock
 *
 * Without timerfd, the choice is 'alarm()' with a signal handler, or
 * simple 'alarm()' to interrupt 'select()/pselect()'. The difficulty
 * is, realizing there can only be one alarm, is maintaining all outstanding
 * timers in a list, and managing the active alarm.
 *
 * A slightly simply solution:- maintain the timer events in a list, where
 * they are sorted by absolute time, and use the delta between rel_time() and alarm->time
 * to seed the [p]select() timeout value.
 *
 * [p]select() returns with a timeout, and the head of the timer list
 * can be checked.
 */

int event_timer_create( event_timer_t *event )
{
	if( (! event) || ( event->tm.it_value.tv_nsec == 0 && event->tm.it_value.tv_sec == 0 ))
		return -1;

	event->fd = timerfd_create( EVENT_CLOCK, TFD_CLOEXEC | TFD_NONBLOCK );
	timerfd_settime(event->fd, 0, & event->tm, 0L );

	return event->fd;
}

void event_timer_delete( event_timer_t *event )
{
	if( event )
		close( event->fd );
}

int event_timer_handler( event_timer_t *event )
{
	if( event ) {
		uint64_t rc;
		if( read( event->fd, &rc, sizeof( rc ) ) < 0 )
			if( errno == EAGAIN || errno == EINTR )
				return 0;
		return (int) rc;
	}
	return -1;
}
#endif
