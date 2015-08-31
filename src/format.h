/******************************************************************************************************
*
* Freestyle Technology Pty Ltd
*
* Copyright (c) 2015 Freestyle Technology Pty Ltd
*
* Name:            format.h
*
* Description:
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


#ifndef __FORMAT_H__
#define __FORMAT_H__

#include <time.h>

#define FORMAT_MAX_SPEC 30

typedef enum
{
	FMT_NONE,
	FMT_STRING,
	FMT_INT,
	FMT_UINT,
	FMT_LONG,
	FMT_ULONG,
	FMT_DATESTRING,
	FMT_CALLBACK,
} format_content_type_t;

typedef struct format_content_s
{
	char            key;
	format_content_type_t type;
	union
	{
		int             i_val;
		unsigned int    u_val;
		long long       l_val;
		unsigned long long ul_val;
		char           *s_val;
		time_t          d_time;
		struct {
			unsigned int  (*c_ptr) (char *, unsigned int, char *, void *);
			void           *c_data;
		};
	};
} format_content_t;

extern unsigned int format_string(char *buffer, unsigned int length, const char *format, format_content_t * fc);

#endif
