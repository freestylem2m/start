/*
 * File: clock.h
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

#ifndef __CLOCK_H__
#define __CLOCK_H__

#ifndef mips
#include <sys/timerfd.h>
#endif

#include <time.h>
#include "events.h"

int             clock_getres(clockid_t clk_id, struct timespec *res);
int             clock_gettime(clockid_t clk_id, struct timespec *tp);
int             clock_settime(clockid_t clk_id, const struct timespec *tp);

#define EVENT_CLOCK  CLOCK_MONOTONIC
typedef struct event_timer_s
{
	int             fd;
	int             interval;
	struct timespec tm;
} event_timer_t;

typedef enum
{
	ALARM_UNISED,
	ALARM_TIMER = 1,
	ALARM_INTERVAL = 2,
} event_alarm_flags_t;

typedef struct event_alarm_s
{
	time_t          duration;
	event_alarm_flags_t flags;
	time_t          event_time;
	int             next_event;
	context_t		   *ctx;
} event_alarm_t;

#define MAX_ALARM 32

extern event_alarm_t alarm_table[MAX_ALARM];

extern time_t   rel_time(time_t * ptr);
extern int      event_timer_create(event_timer_t * event);
extern void     event_timer_delete(event_timer_t * event);
extern int      event_timer_handle(event_timer_t * event);

#endif // __CLOCK_H__
