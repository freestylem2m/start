/*
 * File: exec.c
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

#define _XOPEN_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdint.h>
#include <termios.h>

#include <stdlib.h>
#include <fcntl.h>
#include <stdbool.h>

#undef _XOPEN_SOURCE

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "exec.h"
#include "logger.h"
#include "clock.h"

int exec_init(context_t *ctx)
{
	exec_config_t *cf;

	if ( 0 == (cf = (exec_config_t *) calloc( sizeof( exec_config_t ) , 1 )))
		return 0;

	cf->state = EXEC_STATE_IDLE;
	cf->restart_delay = 10000;

	if( config_istrue( ctx->config, "respawn" ) )
		cf->flags |= EXEC_RESPAWN;

	u_ringbuf_init( &cf->output );

	ctx->data = cf;

	return 1;
}

int exec_shutdown(context_t *ctx)
{
	exec_config_t *cf = (exec_config_t *) ctx->data;
	int siglist[] = { SIGTERM, SIGTERM, SIGKILL, 0 };
	int sigid = 0;

	if( cf->pid > 0 )
		while( !kill( cf->pid, 0 ) && siglist[sigid] ) {
			kill( cf->pid, siglist[sigid++] );
			usleep( 1000 );
		}

	if( ctx->data )
		free( ctx->data );

	ctx->data = 0;

	return 1;
}

#define CMD_ARGS_MAX 32
#define PATH_MAX 512
#define VAR_MAX  64

char *process_command( context_t *ctx, char *cmd, size_t n_cmd, const char *cmdline, char *exe, char **vec )
{
	char           *cp = (char *) cmdline;
	int             px = 0;
	size_t          ci = 0;

	while( *cp ) {
		switch (*cp) {
			case '$':
				{
					cp++;
					char varname[VAR_MAX];
					int  iv = 0;
					if( *cp == '{' || *cp == '(' ) {
						char q = *cp++ == '{' ? '}' : ')';

						while(( *cp && (q != *cp )) && (iv < VAR_MAX-1))
							varname[iv++] = *cp ++;

						if( *cp != q )
							cp = 0;
						else
							cp ++;

					} else
						while( *cp && isalnum( *cp ) && (iv < VAR_MAX-1) )
							varname[iv++] = *cp++;

					varname[iv] = 0;

					if( cp ) {
						const char *var = get_env( ctx, varname );
						if( var ) {
							strncpy( &cmd[ci], var, n_cmd-ci );
							ci += strlen( var );
						} else
							d_printf("Unable to find %s\n",varname);
					}
				}
				break;
				break;
			case '\\':
				cmd[ci++] = *cp++;
			default:
				cmd[ci++] = *cp++;
				break;
		}
	}

	cmd[ci++] = 0;

	cp = cmd;

	while (cp && *cp && px < (CMD_ARGS_MAX - 1)) {
		size_t          seg = strcspn(cp, " \t\r\n\"'$");
		if (seg)
			vec[px++] = cp;

		cp += seg;

		switch (*cp) {
		case ' ':
		case '\t':
			*cp++ = 0;
			break;
		case '\'':
		case '"':
			{
				char            c = *cp;
				*cp++ = 0;
				vec[px++] = cp;
				if ((cp = strchr(cp, c)))
					*cp++ = 0;
			}
			break;
		}
	}

	if (!cp) {
		d_printf("Failing due to unterminated quote or variable...\n");
		return 0L;
	}

	vec[px] = 0;

	if (strchr(vec[0], '/')) {
		strncpy(exe, vec[0], PATH_MAX);
		return exe;
	} else {
		char           *path = getenv("PATH");
		char           *new_path;
		char           *spath;

		if (path) {
			path = new_path = strdup(path);

			int             rc;
			struct stat     info;

			while( path && *path ) {
				spath = path;
				path = strchr(path, ':');
				if (path && *path)
					*path++ = 0;
				snprintf(exe, PATH_MAX, "%s/%s", spath, vec[0]);
				rc = stat(exe, &info);
				if (!rc && S_ISREG(info.st_mode)) {
					free(new_path);
					return exe;
				}
				exe[0] = 0;
			}
			x_printf(ctx,"Command not found in path!!\n");
			free(new_path);
		}
	}

	return 0;
}

int exec_launch( context_t *ctx, int use_tty )
{
	char exe[PATH_MAX];
	char *vec[CMD_ARGS_MAX];
	const char *cmdline;
	char *cmd;


	if( ! (( cmdline = config_get_item( ctx->config, "cmd" ) )))
		return -1;

	cmd = alloca ( PATH_MAX );
	exec_config_t *cf = (exec_config_t *) ctx->data;

	x_printf(ctx,"calling process_command(%s)\n",cmdline);
	if( ! process_command( ctx, cmd, PATH_MAX, cmdline, exe, vec )) {
		x_printf(ctx,"process_command() failed\n");
		return -1;
	}
	
	int fd_in[2] = { -1,-1 };
	int fd_out[2] = { -1,-1 };

	x_printf(ctx,"Executing %s\n",cmd);

	if( use_tty ) {
		fd_in[FD_READ] = fd_out[FD_WRITE] = open("/dev/ptmx", O_RDWR | O_NOCTTY | O_NONBLOCK );

		if( fd_in[FD_READ] < 0 ) {
			logger(ctx->owner, ctx, "Failed to open a pty to launch %s\n", cmd);

			FATAL("Failed to open pty - terminating\n");
			return 1;
		}

		char *slave_name = ptsname( fd_in[FD_READ] );

		unlockpt( fd_in[FD_READ] );
		grantpt( fd_in[FD_READ] );

		fd_in[FD_WRITE] = fd_out[FD_READ] = open( slave_name, O_RDWR );

		if( fd_in[FD_WRITE] < 0 ) {
			logger(ctx->owner, ctx, "Failed to open pty-slave for %s\n",cmd);
			close( fd_in[FD_READ] );

			FATAL("Failed to open pty-slave\n");
			return 1;
		}
	} else {
		if( pipe( fd_in ) || pipe( fd_out ) ) {
			int i;
			for(i=0;i<2;i++) {
				if( fd_in[i] >= 0 )
					close( fd_in[i] );
				if( fd_out[i] >= 0 )
					close( fd_out[i] );
			}
			return 1;
		}
	}

	int pid;

	if( (pid = fork()) ) {
		cf->pid = pid;
		// Parent.  Close the inverse file descriptors
		close( fd_in[FD_WRITE] );
		cf->fd_in = fd_in[FD_READ];
		close( fd_out[FD_READ] );
		cf->fd_out = fd_out[FD_WRITE];

		if( use_tty ) {
			// Check for local echo
			struct termios tt;

			if( cf->tty_flags ) {
				tcgetattr( cf->fd_in, &tt );

				if( cf->tty_flags & TTY_NOECHO )
					tt.c_lflag &= ~(unsigned int)(ECHO);

				if( cf->tty_flags & TTY_RAW )
					cfmakeraw(&tt);

				tcsetattr( cf->fd_in, TCSANOW, & tt );
			}

			event_add( ctx, cf->fd_in, EH_READ | EH_EXCEPTION );
		} else {
			event_add( ctx, cf->fd_in, EH_READ );
			event_add( ctx, cf->fd_out, EH_EXCEPTION );
		}

		x_printf(ctx,"New child process (pid = %d, fd = %d/%d) %s\n",pid,cf->fd_in,cf->fd_out, cmd );
	} else {
		// Child.  convert to stdin/stdout
		if( dup2( fd_out[FD_READ], 0 ) < 0 )
			x_printf(ctx, "Failed to set up STDIN with %d\n", fd_out[FD_READ]);
		if( dup2( fd_in[FD_WRITE], 1 ) < 0 )
			x_printf(ctx, "Failed to set up STDOUT with %d\n", fd_in[FD_WRITE]);
		if( dup2( fd_in[FD_WRITE], 2 ) < 0 )
			x_printf(ctx, "Failed to set up STDERR with %d\n", fd_in[FD_WRITE]);

		sigset_t all_signals;
		sigemptyset( &all_signals );
		sigaddset( &all_signals, SIGQUIT );
		sigaddset( &all_signals, SIGCHLD );
		sigprocmask(SIG_SETMASK, &all_signals, NULL);

		int fd;
		for(fd = 3; fd < 1024; fd ++)
			close(fd);
		
		execv(exe,vec);
		exit(-128);
	}

	return 0;
}

#define MAX_READ_BUFFER 1024
ssize_t exec_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_request_t *fd = 0L;
	event_data_t *data = 0L;

	exec_config_t *cf = (exec_config_t *) ctx->data;

	if( event_data->type == TYPE_FD )
		fd = & event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	//x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	switch( event ) {
		case EVENT_INIT:

			event_add( ctx, 1000, EH_WANT_TICK );
			event_add( ctx, SIGQUIT, EH_SIGNAL );
			event_add( ctx, SIGCHLD, EH_SIGNAL );
			event_add( ctx, SIGPIPE, EH_SIGNAL );

			if( config_istrue( ctx->config, "tty" ) || config_istrue( ctx->config, "pty" ) )
				cf->flags |= EXEC_TTY_REQUIRED;

			cf->tty_flags = ( config_istrue( ctx->config, "echo" ) ? TTY_ECHO : TTY_NOECHO ) |
					( config_istrue( ctx->config, "raw" ) ? TTY_RAW : 0 );

			cf->tty = cf->flags & EXEC_TTY_REQUIRED;

		case EVENT_START:

			if( ! exec_launch( ctx, cf->tty ) ) {
				cf->state = EXEC_STATE_RUNNING;
				// Let the parent know the process has started/restarted
				context_owner_notify( ctx, CHILD_EVENT, 0 );
			} else {
				cf->state = EXEC_STATE_ERROR;
				return context_terminate(ctx);
			}
			break;

		case EVENT_TERMINATE:
			cf->pending_action_timestamp = rel_time(0L);

			if( cf->state == EXEC_STATE_RUNNING ) {
				// When a 'tty' is in use, you can't close fd_in and fd_out separately.
				if( cf->tty )
					kill( cf->pid, SIGTERM );
				else {
					event_delete( ctx, cf->fd_out, EH_NONE );
					close( cf->fd_out );
				}
				// TERMINATING status is handled during the TICK event
				cf->flags |= EXEC_TERMINATING;
			} else
				context_terminate( ctx );
            break;

        case EXEC_SET_RESPAWN:
            {
                uint8_t flag = 0;
                if( event_data->type == TYPE_CUSTOM && event_data->event_custom )
                    flag = *(uint8_t *) (event_data->event_custom);

                if( flag ) {
                    cf->flags |= EXEC_RESPAWN;
                } else {
                    cf->flags &= ~(unsigned int)EXEC_RESPAWN;
                }
            }
            break;

        case EVENT_RESTART:
            {
                uint8_t sig = 0;
                if( event_data->type == TYPE_CUSTOM && event_data->event_custom )
                    sig = *(uint8_t *) (event_data->event_custom);

                cf->flags |= EXEC_RESTARTING;

                if( sig )
                    kill( cf->pid, sig );

                close(cf->fd_out);
                event_delete( ctx, cf->fd_out, EH_NONE );
                cf->fd_out = -1;

				if( !cf->tty ) {
					close(cf->fd_in);
					event_delete( ctx, cf->fd_in, EH_NONE );
				}

                cf->fd_in = -1;
				cf->state = EXEC_STATE_STOPPING;

				cf->pending_action_timestamp = rel_time(0L);
            }
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if(( cf->state == EXEC_STATE_RUNNING) && data ) {
				ssize_t items = u_ringbuf_write( &cf->output, data->data, data->bytes );

				if( !u_ringbuf_empty( &cf->output ) )
					event_add( ctx, cf->fd_out, EH_WRITE );

				return items;
			}

			break;

		case EVENT_WRITE:
			if( u_ringbuf_ready( &cf->output ))
				u_ringbuf_write_fd( &cf->output, cf->fd_out );

			if( u_ringbuf_empty( &cf->output ) )
				event_delete( ctx, cf->fd_out, EH_WRITE );
			break;

		case EVENT_READ:
			{
				size_t bytes;
				event_bytes( fd->fd, &bytes );

				if( bytes ) {
					char read_buffer[MAX_READ_BUFFER];

					if( bytes >= MAX_READ_BUFFER )
						bytes = MAX_READ_BUFFER-1;

					ssize_t result = event_read( fd->fd, read_buffer, bytes);

					if( result >= 0 ) {
						read_buffer[result] = 0;

						driver_data_t read_data = { TYPE_DATA, ctx, {} };
						read_data.event_data.data = read_buffer;
						read_data.event_data.bytes = (size_t) result;

						if( emit( ctx->owner, EVENT_DATA_INCOMING, &read_data ) != result )
							x_printf(ctx,"WARNING: Failed to send all data to %s\n",ctx->owner->name);

					} else
						x_printf(ctx," * WARNING: read return unexpected result %d\n",result);
				} else {
					event_delete( ctx, cf->fd_in, EH_NONE );
					close( cf->fd_in );
					cf->fd_in = -1;
					if( cf->tty )
						cf->fd_out = -1;

					if( cf->state == EXEC_STATE_STOPPING ) {
						if( (cf->flags & (EXEC_RESPAWN|EXEC_TERMINATING)) == EXEC_RESPAWN ) {
							cf->state = EXEC_STATE_IDLE;
							cf->pending_action_timestamp = rel_time(0L);
						} else
							return context_terminate( ctx );
					} else
						cf->state = EXEC_STATE_STOPPING;
				}
			}
			break;

		case EVENT_SEND_SIGNAL:
			if( cf->pid > 0 )
				kill( cf->pid, event_data->event_signal );
			break;

		case EVENT_SIGNAL:
			if( event_data->event_signal == SIGCHLD ) {
				int status;
				int rc = event_waitchld(&status, cf->pid);

				if( rc == cf->pid ) {
					x_printf(ctx, "PID matches - status = %d. cleaning up\n", WEXITSTATUS(status));
					// disable further events being recognized for this process
					cf->pid = -1;
					if( ! cf->tty ) {
						close(cf->fd_out);
						event_delete( ctx, cf->fd_out, EH_NONE );
					}
					cf->fd_out = -1;

					// Program termination already signalled
					if( cf->state == EXEC_STATE_STOPPING ) {
						if( ( ! (cf->flags & EXEC_TERMINATING )) && ( cf->flags & (EXEC_RESPAWN|EXEC_RESTARTING))) {
							cf->flags &= ~(unsigned int)EXEC_RESTARTING;
							cf->state = EXEC_STATE_IDLE;
							cf->pending_action_timestamp = rel_time(0L);
						} else
							return context_terminate( ctx );
					} else
						cf->state = EXEC_STATE_STOPPING;
				}
			}
			break;

		case EVENT_TICK:
			{
				time_t now = cf->last_tick = rel_time(0L);

				if( cf->flags & (EXEC_TERMINATING|EXEC_RESTARTING) ) {
					if( cf->pid > 0 ) {
						if(( now - cf->pending_action_timestamp ) > (EXEC_PROCESS_TERMINATION_TIMEOUT*2) )
							kill( cf->pid, SIGKILL );
						else if(( now - cf->pending_action_timestamp ) > EXEC_PROCESS_TERMINATION_TIMEOUT )
							kill( cf->pid, SIGTERM );
					}
				}

				if( (cf->state == EXEC_STATE_IDLE) && (( now - cf->pending_action_timestamp ) > cf->restart_delay ))
					emit( ctx, EVENT_START, DRIVER_DATA_NONE );
			}
			break;

		default:
			break;
	}
	return 0;
}
