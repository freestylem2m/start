/*
 * File: logger.h
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

#ifndef __LOGGER_H_
#define __LOGGER_H_

#include "events.h"

#define LOG_BUFFER_MAX 1024
extern void logger(context_t *, char *fmt, ...) __attribute__((format(printf,2,3)));;
//extern void logger(context_t *, char *fmt, ...);
#endif
