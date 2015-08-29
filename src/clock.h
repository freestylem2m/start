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

int                 clock_getres(clockid_t clk_id, struct timespec *res);
int                 clock_gettime(clockid_t clk_id, struct timespec *tp);
int                 clock_settime(clockid_t clk_id, const struct timespec *tp);

#define EVENT_CLOCK  CLOCK_MONOTONIC
typedef struct event_timer_s
{
	int                 fd;
	struct itimerspec   tm;
} event_timer_t;

typedef enum
{
	ALARM_UNISED,
	ALARM_TIMER = 1,
	ALARM_INTERVAL = 2,
	ALARM_FIRED = 4,
} event_alarm_flags_t;

extern time_t       rel_time(time_t * ptr);
extern int          event_timer_create(event_timer_t * event);
extern void         event_timer_delete(event_timer_t * event);
extern int          event_timer_handle(event_timer_t * event);

extern int          alarm_delete_list(int i);
extern int          alarm_insert_list(int i);
extern long         alarm_getnext(void);

#endif // __CLOCK_H__
