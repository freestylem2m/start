/******************************************************************************************************
*
* Freestyle Technology Pty Ltd
*
* Copyright (c) 2015 Freestyle Technology Pty Ltd
*
* Name:            hvc_util.c
*
* Description:     Utility functions for HVC management
*                  This serves to ensure the HVC functions are all aliased and can be
*                  cleanly overridden where required
*
* Project:         Freestyle Micro Engine
* Owner:           Freestyle Technology Pty Ltd
*
* THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
******************************************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wait.h>

#include "netmanage.h"
#include "hvc_util.h"

#define HVCCTL_MAX_LINE  128

int hvc_getTemperature(void)
{
#ifdef mips
#ifdef HVCLIBS
	int temp = hvcGetTemperature();
#else
	char *cmd = "hvcctl getTemperature";
	FILE *p = popen( cmd, "r" );
	char buffer[HVCCTL_MAX_LINE];
	int temp = -1;

	if( p ) {
		if( fgets(buffer,HVCCTL_MAX_LINE,p) ) {
			char *d = strpbrk( buffer, "0123456789" );
			if( d )
				temp = strtol( d, 0L, 10 );
		}
		pclose(p);
	}
#endif
	return temp;
#else
	return -1;
#endif
}

const char *hvc_nvram_get(char *item)
{
#ifdef mips
#ifdef HVCLIBS
	const char *result = nvram_get( RT2860_NVRAM, item );
#else
	static char buffer[HVCCTL_MAX_LINE];
	char cmd[HVCCTL_MAX_LINE];
	char *result = 0L;

	if( snprintf(cmd, HVCCTL_MAX_LINE, "nvram_get 2860 %s",item ) > HVCCTL_MAX_LINE ) {
		fprintf(stderr,"%s: parameter too long\n",__func__);
		return 0L;
	}

	FILE *p = popen( cmd, "r" );

	if( p ) {
		if( fgets(buffer,HVCCTL_MAX_LINE,p) ) {
			char *eol = strchr(buffer,'\n');
			if( eol )
				*eol = 0;
			result = buffer;
		}

		if( pclose(p) == -1 )
			result = 0L;
	}
#endif
	return result;
#else
	UNUSED(item);
	return 0L;
#endif
}

int hvc_nvram_set(char *item, char *value)
{
#ifdef mips
	char cmd[HVCCTL_MAX_LINE];
	char buffer[HVCCTL_MAX_LINE];

	if( snprintf(cmd, HVCCTL_MAX_LINE, "nvram_set 2860 %s %s",item, value ) > HVCCTL_MAX_LINE ) {
		fprintf(stderr,"%s: parameter too long\n",__func__);
		return -1;
	}

	FILE *p = popen( cmd, "r" );

	if( p ) {
		// Absorb output otherwise it returns an error
		while(fgets(buffer,HVCCTL_MAX_LINE,p));
		// return code is mostly ignored
		return WEXITSTATUS( pclose(p) );
	}
#else
	UNUSED(item);
	UNUSED(value);
#endif
	return -1;
}
