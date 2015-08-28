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
#include <time.h>
#include <ctype.h>
#include <string.h>

#include "format.h"

unsigned int format_string(char *buffer, unsigned int length, const char *format, format_content_t * fc)
{
	unsigned int    i = 0;

	char            wfmt[FORMAT_MAX_SPEC + 3];
	char           *fmt = wfmt + 1;

	wfmt[0] = '%';

	while (format && *format && i < (length-1)) {
		if (*format == '%') {
			format++;
			int             f = 0;
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

			char            key = *format++;
			int             k = 0;
			while (fc[k].key && fc[k].key != key)
				k++;
			if (fc[k].key) {
				switch (fc[k].type) {
				case FMT_STRING:
					{
						unsigned int    l = strlen(fc[k].s_val);
						if (l + i > length)
							l = length - i;
						memcpy(&buffer[i], fc[k].s_val, l);
						i += l;
					}
					break;
				case FMT_INT:
					strcat(wfmt, "d");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].i_val);
					break;
				case FMT_UINT:
					strcat(wfmt, "u");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].i_val);
					break;
				case FMT_ULONG:
					strcat(wfmt, "llu");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].i_val);
					break;
				case FMT_LONG:
					strcat(wfmt, "lld");
					i += (unsigned int)snprintf(&buffer[i], length - i, wfmt, fc[k].i_val);
					break;
				case FMT_DATESTRING:
					i += strftime(&buffer[i], length - i, fmt, localtime(&fc[k].d_time));
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
				case 'm':
					i += (unsigned int)snprintf(&buffer[i], length - i, "%m");
					break;
				default:
					i += (unsigned int)snprintf(&buffer[i], length - i, "%s%c", wfmt, key);

				}
			}
		} else
			buffer[i++] = *format++;
	}

	buffer[i] = 0;

	return i;
}

#ifdef TEST_MAIN
int main(int ac __attribute__ ((unused)), char *av[] __attribute__ ((unused)))
{

	format_content_t s[] = {
		{'i', FMT_ULONG, {.l_val = 999999999999}},
		{'z', FMT_STRING, {.s_val = "hello"}},
		{'T', FMT_DATESTRING,.d_time = time(0L)},
		{0x00, FMT_NONE, {}}
	};

	char            buffer[1024];
	format_string(buffer, 1024, "first %i%% - %z world today %{%s - %D:%T}T now - %m %23o", s);
	printf("buffer = %s\n", buffer);

	return 0;
}
#endif
