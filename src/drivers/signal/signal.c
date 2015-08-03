/*
 * File: signal.c
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
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

#include <driver.h>

#include "netmanage.h"
#include "signal.h"

int signal_init(context_t *context)
{
	UNUSED(context);
	d_printf("Hello from SIGNAL INIT!");

	return 0;
}
