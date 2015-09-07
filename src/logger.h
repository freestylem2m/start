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
extern void logger(context_t *source, char *fmt, ...);
#endif
