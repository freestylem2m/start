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
			unsigned int  (*c_ptr) (char *, size_t, char *, void *);
			void           *c_data;
		};
	};
} format_content_t;

/*
 * Hexdump() functions.
 *
 * The base 'format_hexdump()' function takes a byte array and an output buffer.
 *
 * The output buffer is filled, and the number of bytes is returned.
 *
 * If the total number of bytes returned is greater than the length of the output buffer
 * then the output will be truncated.
 *
 * For normal use, call format_hexdump(), check the return code, if the return code is greater
 * than the length of the buffer, reallocate the buffer and retry.
 *
 * The following macros implement the debug functions:
 *   x_hexdump( context, data, bytes )
 *   d_hexdump( data, bytes )
 *   print_hexdump( data, bytes )
 *
 * These each call 'c_hexdump()' with a callback function of x_printf(), d_printf() and printf() respectively.
 *
 * The use of the macros simplifies the use of the hexdump function, they use alloca() to allocate sufficient
 * space on the stack, format the data, and call the callback for each line of data in the output buffer
 *
 */

#ifndef NDEBUG
#define HEXDUMP_BUFFER_MIN 1024
#define __d_printf(x)     d_printf("%s\n",x)
#define __x_printf(x,ctx) x_printf(ctx,"%s\n",x)
#define __printf(x)       printf("%s\n",x)

#define c_hexdump(data,bytes, func, ...) { \
	size_t _l = HEXDUMP_BUFFER_MIN; \
	char  *_o = alloca( _l ); \
	size_t _s = format_hex( _o, _l, data, bytes ); \
	if( _s > _l ) { _o = alloca( _s ); format_hex( _o, _s, buf, bytes ); } \
	while( *_o ) { func(_o, ## __VA_ARGS__ ); _o += strlen(_o)+1; } \
}

#define x_hexdump(ctx,data,bytes) c_hexdump(data, bytes, __x_printf, ctx)
#define d_hexdump(data,bytes) c_hexdump(data, bytes, __d_printf)
#define print_hexdump(data,bytes) c_hexdump(data, bytes, __printf)
#else
#define x_hexdump(...) {}
#define d_hexdump(...) {}
#define print_hexdump(...) {}
#endif

extern size_t format_string(char *buffer, size_t length, const char *format, format_content_t * fc);
extern size_t format_hex( char *buffer, size_t length, const unsigned char *data, size_t bytes);

#endif
