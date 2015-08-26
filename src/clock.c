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

#ifndef mips
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
	alarm_table[i].next_event = 0;
	
	// Scan table looking for a place to insert.  This only scans existing
	// entries, stopping at EOL or when timestamp > new alarm
	event_alarm_t *j = & alarm_table[0];
	while( j->next_event ) {
		if( alarm_table[j->next_event].event_time > alarm_table[i].event_time )
			break;
		j = & alarm_table[ j->next_event ];
	}

	// If this is the end of the list, insert after, otherwise, its always before
	if( j->next_event && alarm_table[ j->next_event ].event_time > alarm_table[i].event_time ) {
		alarm_table[i].next_event = j->next_event;
		j->next_event = i;
	} else
		j->next_event = i;

	return i;
}

/*
 * Delete an existing alarm by index.
 */
int alarm_delete(context_t *ctx, int index)
{
	if( index < MAX_ALARM && alarm_table[index].flags && ( alarm_table[index].ctx == ctx ) ) {
		printf("trying for loop\n");
		int i;
		for( i = 0; alarm_table[i].next_event; i = alarm_table[i].next_event ) {
			printf("i = %d, alarm_table[i].next_event = %d\n",i,alarm_table[i].next_event);
			if( alarm_table[i].next_event == index ) {
				alarm_table[i].next_event = alarm_table[index].next_event;
				alarm_table[index].flags = 0;
				alarm_table[index].ctx = 0L;
				return 0;
			}
		}
	}

	return -1;
}

#if 0
void dump_alarm_table(void)
{
	int i;
	for( i = 0; i < MAX_ALARM; i++ )
	{
		if( i==0 || alarm_table[i].flags )
			printf("%d (%ld/%ld) ->%d\n",i, alarm_table[i].duration,alarm_table[i].event_time, alarm_table[i].next_event);
	}
}

int main(int ac __attribute__((unused)), char *av[] __attribute__((unused))  )
{
	printf("10 seconds = %d\n", alarm_add(0,10,1));
	printf("20 seconds = %d\n", alarm_add(0,20,1));
	dump_alarm_table();
	printf("5 seconds = %d\n", alarm_add(0,5,1));
	dump_alarm_table();
	printf("15 seconds = %d\n", alarm_add(0,15,1));
	dump_alarm_table();
	printf("1 seconds = %d\n", alarm_add(0,1,1));
	dump_alarm_table();
	printf("7 seconds = %d\n", alarm_add(0,7,1));
	dump_alarm_table();
	printf("13 seconds = %d\n", alarm_add(0,13,1));
	dump_alarm_table();
	printf("alarm_delete(5) = %d\n", alarm_delete( 0, 5 ) );
	dump_alarm_table();
	printf("alarm_delete(2) = %d\n", alarm_delete( 0, 2 ) );
	dump_alarm_table();
	printf("alarm_delete(3) = %d\n", alarm_delete( 0, 3 ) );
	dump_alarm_table();
	printf("1 seconds = %d\n", alarm_add(0,1,1));
	dump_alarm_table();
	printf("20 seconds = %d\n", alarm_add(0,20,1));
	dump_alarm_table();
	printf("5 seconds = %d\n", alarm_add(0,5,1));
	dump_alarm_table();
	return 0;
}
#endif

/*
 * rel_time() differs from 'time()' by the use of a non-wallclock based timer.
 * The use of clock_gettime( CLOCK_MONOTONIC ) ensures that timeout and periodic
 * events which have to occur at specified intervals are unaffected by changes in
 * wall clock time, such as calling the 'date' command, or NTP, or refreshing the
 * system clock from the hardware RTC.
 */

time_t rel_time(time_t *ptr)
{
	struct timespec reltime;

	clock_gettime( CLOCK_MONOTONIC, & reltime );

	if( ptr )
		*ptr = reltime.tv_sec;

	return reltime.tv_sec;
}

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
	if( event && event->tm.tv_nsec == 0 && event->tm.tv_sec == 0 )
		return -1;

	int fd;
#ifdef mips
	fd = timerfd_create( EVENT_CLOCK, 0xFFFF );
#else
	fd = timerfd_create( EVENT_CLOCK, TFD_CLOEXEC | TFD_NONBLOCK );
#endif

	if( event ) {

		struct itimerspec _timer;
		bzero( & _timer, sizeof( struct itimerspec ) );

		event->fd = fd;
		_timer.it_value = event->tm;

		if( event->interval )
			_timer.it_interval = event->tm;

#ifdef mips
		timerfd_settime(fd, 0, &_timer, 0L );
#else
		timerfd_settime(fd, 0, &_timer, 0L );
#endif
	}

	return fd;
}

void event_timer_delete( event_timer_t *event )
{
	if( event )
		close( event->fd );
}

int event_timer_handle( event_timer_t *event )
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
