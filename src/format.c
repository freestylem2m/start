/******************************************************************************************************
*
* Freestyle Technology Pty Ltd
*
* Copyright (c) 2015 Freestyle Technology Pty Ltd
*
* Name:            format.c
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "format.h"

size_t format_string(char *buffer, size_t length, const char *format, format_content_t * fc)
{
	size_t              i = 0;

	char                wfmt[FORMAT_MAX_SPEC + 3];
	char               *fmt = wfmt + 1;

	wfmt[0] = '%';

	while (format && *format && i < (length - 1)) {
		if (*format == '%') {
			format++;
			int                 f = 0;
			if (format[f] == '{') {
				format++;
				while (format[f] && f < FORMAT_MAX_SPEC && format[f] != '}')
					fmt[f] = format[f], f++;
				if (format[f] == '}')
					format++;
			} else {
				while (format[f] && (!isalpha(format[f])) && format[f] != '%' && f < FORMAT_MAX_SPEC)
					fmt[f] = format[f], f++;
			}
			format += f;
			fmt[f] = 0;

			char                key = *format++;
			int                 k = 0;
			while (fc[k].key && fc[k].key != key)
				k++;

			// Formats which rely on some variant of printf() may be tricked into misbehaving
			// if alphabetic characters sneak into the format strings.  This chops the format
			// string at the first sign of trouble
			char *cfmt = fmt;

			while( *cfmt && !isalpha( *cfmt ) )
				cfmt ++;

			// fmt = the format string read so far (for %-20z, fmt = "-20")
			// wfmt = '%' followed by fmt
			// cfmt = address in fmt where the final formatting characters must go
			//        (eg, the s in "%-20s")
			// strftime() allows all formatting characters like %T %D %H:%M:%S etc
			// callback also allow all characters.
			// everything else relies in cfmt being sterilized
			if (fc[k].key) {
				switch (fc[k].type) {
				case FMT_STRING:
					strcpy(cfmt, "s");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].s_val);
					break;
				case FMT_INT:
					strcpy(cfmt, "d");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].i_val);
					break;
				case FMT_UINT:
					strcpy(cfmt, "u");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].i_val);
					break;
				case FMT_ULONG:
					strcpy(cfmt, "llu");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].i_val);
					break;
				case FMT_LONG:
					strcpy(cfmt, "lld");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].i_val);
					break;
				case FMT_DATESTRING:
					i += (unsigned int) strftime(&buffer[i], length - i, fmt, localtime(&fc[k].d_time));
					break;
				case FMT_CALLBACK:
					i += (fc[k].c_ptr)( &buffer[i], length - i, fmt, fc[k].c_data );
					break;
				case FMT_NONE:
				default:
					break;
				}
			} else {
				switch (key) {
				case '%':
					buffer[i++] = '%';
					break;
				case 'P':
					i += (unsigned int)snprintf( &buffer[i], length - i, "%d", getpid() );
					break;
				case 'H':
					if( gethostname( &buffer[i], length - i ) )
						strncpy( &buffer[i], "(hostname)", length - i );
					i += strlen(&buffer[i]);
					break;
				case 'm':
#ifdef mips
					i += (unsigned int)snprintf(&buffer[i], length - i, "%s", strerror(errno));
#else
					i += (unsigned int)snprintf(&buffer[i], length - i, "%m");
#endif
					break;
				default:
					i += (unsigned int)snprintf(&buffer[i], length - i, "%s%c", wfmt, key);

				}
			}
		} else
			buffer[i++] = *format++;
	}

	// snprintf() returns the length of the formatted string, even if it extends beyond the
	// end of the buffer.   buffer[i] may therefore be past the end of the buffer
	buffer[length - 1] = 0;

	return i > length ? length : i;
}

#ifdef TEST_MAIN
#define TEST_MAIN
int main(int ac __attribute__ ((unused)), char *av[] __attribute__ ((unused)))
{

	format_content_t    s[] = {
		{'i', FMT_ULONG, {.l_val = 999999999999}},
		{'z', FMT_STRING, {.s_val = "hello"}},
		{'T', FMT_DATESTRING,.d_time = time(0L)},
		{0x00, FMT_NONE, {}}
	};

	char                buffer[1024];
	format_string(buffer, 1024, "first %i%% - %{-2p1}z world today %{%s - %D:%T}T now - %m %23o", s);
	printf("buffer = %s\n", buffer);

	return 0;
}
#endif
