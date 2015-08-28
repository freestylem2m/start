/*
 * File: cmdline.c
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

#include "cmdline.h"

// ********************************************************************************
// Globals containing result of command line parsing..
// ********************************************************************************
//

const char *programname = 0L;
#ifndef NDEBUG
char *config_file = (char *) "/mnt/netmanage.conf";
#else
char *config_file = (char *) "/flash/netmanage.conf";
#endif
char *msg_filter = 0L;
int  debug = 0;
int  debug_quiet = 0;

// ********************************************************************************
// Command line parsing support...
// ********************************************************************************
//
// Specific types of command line arguments, strings, numbers and flags.
typedef enum _argtype {
	A_FLAG,
	A_STRING,
	A_STR_VEC,
	A_LONG
} argtype;

// For LONG arguments type, support scaling factors for 'kilo', 'mega' 'giga' etc.
typedef struct _scalefactor {
	char c;
	int scale;
} scalefactor;

// Scale factors.  kmg are multipes of 1000, KMG are multipls of 1024. 's' is seconds (as in 1000 ms)
scalefactor argscale[] = {
	{ 'k', 1000 },
	{ 'm', 1000000 },
	{ 's', 1000 },
	{ 'g', 1000000000 },
	{ 'K', 1024 },
	{ 'M', 1024*1024 },
	{ 'G', 1024*1024*1024 },
	{ 0x00, 0 }
};

// Structure containing each command line argument including its name, type, and
// help message.  _str has one special feature;
// "debug" means '-d or --debug' while 'V|debug' means '-V' or '--debug'..
// The long form is never case sensitive, so '-Debug' '-DEBUG' etc are all equivalent.
// The short form is always case sensitive, so 'Debug' matches '-D' in uppercase only
typedef struct _cmdarg
{
	// Argument key.  Can me "hello" which matches -h or --hello or any (unique) substring of,
	// such as --he.  Exact substring match can be controlled by ordering the table carefully.
	// Alternatives include 'X|xray' where '-X' is a substitute for --xray.  note the case.
	// 'a|b|c' or 'longname|short' are not supported.  Only a single letter followed by a pipe symbol.
	const char     *_str;

	// Argument type.  Expect one of the A_types above
	argtype         _type;

	union
	{
		// Flags datatype.  if f_mask ==0, f_ptr counts, otherwise f_mask is used to set bits in f_ptr
		struct {
			int            f_mask;
			int            *f_ptr;
		} f_ptr;

		// LONG pointer, for arguments which take a number
		long           *l_ptr;

		// Generic STRING pointer, for arguments which take a string.  String is a pointer to argv[]. Do not call free()
		char          **s_ptr;

		// String vector.  Accept up to "sv_max" strings.  These are pointers to argv[] and do no need to be free()'d
		struct {
			int			*sv_index;
			int			sv_max;
			char	**sv_ptr;
		} sv_ptr;

	} _vec;

	// Help info for this argument.
	const char     *_help;
} cmdarg;

// Default command line arguments for this program
cmdarg args[] = {
	{ "c|config",  A_STRING, ._vec.s_ptr = &config_file,"--config=file      Specify configuration file." },
	{ "d|debug", A_FLAG,   ._vec.f_ptr = { 0, &debug }, "--debug            Enable verbose logging" },
	{ "q|quiet", A_FLAG,   ._vec.f_ptr = { 0, &debug_quiet }, "--quiet            Silence logging" },
#ifndef NDEBUG
	{ "F|filter", A_STRING, ._vec.s_ptr = &msg_filter,   "--filter=msg       Specify debug log filter" },
#endif
	{ NULL,        A_FLAG,   ._vec = {} }
};

// ********************************************************************************
// Error handlers...
// ********************************************************************************
// __fatal() dies noisily
int __fatal(const char *func, int line, const char *fmt, ...)
{
	va_list fmt_args;
	va_start( fmt_args, fmt );
	fprintf(stderr,"%s@%d: Fatal error: ",func,line);
	if( fmt ) {
		vfprintf(stderr,fmt,fmt_args);
		fprintf(stderr,"\n");
	} else {
		fprintf(stderr,"%s\n",strerror(errno));
	}
	va_end( fmt_args );

	exit(-1);
}

// help() dies even more noisily while pointing out that you are an idjit
int help(const char *cmd, const char *msg, ...)
{
	int ix;
	va_list vec;
	va_start( vec, msg );
	if( msg )
		vfprintf(stderr,msg,vec);
	va_end( vec );

	printf("Usage: %s [--debug] [--config=file]\n",cmd);
	for(ix = 0; args[ix]._str; ix++) {
		if( args[ix]._help )
			printf("%10s%s\n","",args[ix]._help);
	}
	exit(-127);
}

// ********************************************************************************
// Dumper for command line parser.. Converts saved configuration back into command line
// arguments so you can verify operation.
// ********************************************************************************
void dump_cmdline()
{
	int ix = 0;
	printf("--------------------------------------------------------\n");

	while( args[ix]._str ) {
		switch(args[ix]._type) {
			case A_STR_VEC:
				{
					printf(" --%s\n",args[ix]._str[1]=='|'?&args[ix]._str[2]:args[ix]._str);
					int ir = 0;
					while( ir < *args[ix]._vec.sv_ptr.sv_index ) {
						printf("  %2d) %s\n",ir,args[ix]._vec.sv_ptr.sv_ptr[ir]);
						ir++;
					}
				}
				break;
			case A_LONG:
				printf(" --%s=%ld\n",args[ix]._str[1]=='|'?&args[ix]._str[2]:args[ix]._str,*args[ix]._vec.l_ptr);
				break;
			case A_STRING:
				printf(" --%s=%s\n",args[ix]._str[1]=='|'?&args[ix]._str[2]:args[ix]._str,*args[ix]._vec.s_ptr);
				break;
			case A_FLAG:
				if( args[ix]._vec.f_ptr.f_mask == 0 ) {
					if( *args[ix]._vec.f_ptr.f_ptr > 1 ) {
						printf(" --%s [repeated %d]\n",args[ix]._str[1]=='|'?&args[ix]._str[2]:args[ix]._str,*args[ix]._vec.f_ptr.f_ptr);
					} else {
						printf(" --%s [%s]\n",args[ix]._str[1]=='|'?&args[ix]._str[2]:args[ix]._str,*args[ix]._vec.f_ptr.f_ptr?"enabled":"disabled");
					}
				} else {
					printf(" --%s [%s]\n",args[ix]._str[1]=='|'?&args[ix]._str[2]:args[ix]._str,*args[ix]._vec.f_ptr.f_ptr & args[ix]._vec.f_ptr.f_mask ? "enabled":"disabled");
				}
				break;
			default:
				FATAL("WTF?");
		}
		ix++;
	}
	printf("--------------------------------------------------------\n");
}

// ********************************************************************************
// Trivial command line parser.. matches command line arguments using the lookup table
// and puts the result in the variables referred to there. Error checking is imaginary.
// ********************************************************************************
int parse_cmdline(int ac, char *av[])
{
	int             ai = 1;		// tracks the current argv[index]
	char           *_p, *_v;	// _p = argv parameter, _v = argv value (--arg=value results in _p and _v pointing within the same string)
	int             ix, sx;		// ix indexes args[] table, sx indexes argscale[] table.
	int             _s;			// _s == true when using SHORT args (-D -f, etc)

	programname = av[0];
	while (ai < ac && *av[ai] == '-') {
		_s = av[ai][1] != '-';
		for (_p = av[ai]; *_p == '-'; _p++);
		if (_p && *_p) {
			while (_p && *_p) {
				ix = 0;
				if ((_v = _s ? 0 : strchr(_p, '=')))
					*_v++ = 0;

				while (args[ix]._str || help(av[0], "Error: Unknown option %s\n", _p)) {
					if (_s) {
						if (args[ix]._str[0] == *_p)
							break;
					} else {
						if (!strncasecmp(_p, (args[ix]._str[1] == '|') ? args[ix]._str + 2 : args[ix]._str, strlen(_p)))
							break;
					}
					ix++;
				}

				_p = _s ? _p + 1 : 0;

				if (_s && _p && (*_p == '=' || isdigit(*_p))) {
					_v = _p;
					if (*_v == '=')
						*_v++ = 0;
				}

				if (!_v && args[ix]._type != A_FLAG) {
					if (_p && *_p) {
						_v = _p;
						_p = 0;
					} else if (++ai < ac)
						_v = av[ai];
					else
						help(av[0], "Error: argument expected for %s\n", args[ix]._str);
				}

				switch (args[ix]._type) {
				case A_FLAG:
					if (args[ix]._vec.f_ptr.f_mask)
						*(args[ix]._vec.f_ptr.f_ptr) |= args[ix]._vec.f_ptr.f_mask;
					else
						*(args[ix]._vec.f_ptr.f_ptr) += _v ? (int) strtol(_v, &_p, 0) : 1;
					break;
				case A_STRING:
					*(args[ix]._vec.s_ptr) = _v;
					break;
				case A_STR_VEC:
					if (*(args[ix]._vec.sv_ptr.sv_index) < args[ix]._vec.sv_ptr.sv_max)
						args[ix]._vec.sv_ptr.sv_ptr[(*args[ix]._vec.sv_ptr.sv_index)++] = _v;
					else
						FATAL("Too many occurrences for --%s", args[ix]._str);
					break;
				case A_LONG:
					*(args[ix]._vec.l_ptr) = strtol(_v, &_p, 0);
					if (_p == _v)
						help(av[0], "Error: Invalid argument specified for %s\n", args[ix]._str);
					for (sx = 0; _p && *_p && argscale[sx].c; sx++) {
						if (*_p == argscale[sx].c) {
							*(args[ix]._vec.l_ptr) *= argscale[sx].scale;
							_p++;
							break;
						}
					}
					break;
				}
			}
			ai++;
		} else
			help(av[0], NULL);
	}
	// Return value is the index into the first non-parameter command line argument.
	return ai;
}
