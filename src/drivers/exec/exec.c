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
	x_printf(ctx,"%s Hello from EXEC INIT!\n", ctx->name);

	// register "emit" as the event handler of choice
	
	exec_config_t *cf;

	if ( 0 == (cf = (exec_config_t *) calloc( sizeof( exec_config_t ) , 1 )))
		return 0;

	cf->state = EXEC_STATE_IDLE;
	cf->restart_delay = 10;

	if( config_istrue( ctx->config, "respawn" ) )
		cf->flags |= EXEC_RESPAWN;

	u_ringbuf_init( &cf->output );

	ctx->data = cf;

	return 1;
}

int exec_shutdown(context_t *ctx)
{
	x_printf(ctx,"Goodbye from EXEC!\n");

	x_printf(ctx,"context data = %p\n",ctx->data);

	exec_config_t *cf = (exec_config_t *) ctx->data;
	int siglist[] = { SIGTERM, SIGTERM, SIGKILL, 0 };
	int sigid = 0;

	// Cycle through the list of signals, sending them until the
	// child process has terminated, (or I run out of signals)

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

char *process_command( char *cmd, char *exe, char **vec )
{
	//x_printf(ctx,"cmd = %s\n", cmd);

	char           *cp = cmd;
	int             px = 0;

	while (cp && *cp && px < (CMD_ARGS_MAX - 1)) {
		//x_printf(ctx,"cp = %s\n", cp);
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
				d_printf("Scanning for a %c\n", c);
				*cp++ = 0;
				vec[px++] = cp;
				d_printf("remainder = %s\n", cp);
				if ((cp = strchr(cp, c)))
					*cp++ = 0;
				d_printf("Found the close %c at %p\n", c, cp);
			}
			break;
		case '$':
			d_printf("Found a %c.. like I support variables..\n",*cp);
			cp++;
			break;
		}
	}

	if (!cp) {
		d_printf("Failing due to unterminated quote...\n");
		return 0L;
	}

	vec[px] = 0;

	if (*vec[0] == '/' || strchr(vec[0], '/')) {
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

			while (path && *path && px < (PATH_MAX - 1)) {
				spath = path;
				path = strchr(path, ':');
				if (path && *path)
					*path++ = 0;
				snprintf(exe, PATH_MAX, "%s/%s", spath, vec[0]);
				//d_printf("%s\n", exe);
				rc = stat(exe, &info);
				if (!rc && S_ISREG(info.st_mode)) {
					//d_printf("Found my command!\n");
					free(new_path);
					return exe;
				}
				exe[0] = 0;
			}
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

	cmdline = config_get_item( ctx->config, "cmd" );

	if( !cmdline )
		return -1;

	cmd = strdup( cmdline );

	const char *path = process_command( cmd, exe, vec );

	exec_config_t *cf = (exec_config_t *) ctx->data;

	if( !path ) {
		x_printf(ctx,"%s - Failing with unable-to-find-file error\n", ctx->name);
		free(cmd);
		return -1;
	}
	
	int fd_in[2] = { 0,0 };
	int fd_out[2] = { 0,0 };

	if( use_tty ) {
		x_printf(ctx,"Trying to open a tty!!!!\n");
		fd_in[FD_READ] = fd_out[FD_WRITE] = open("/dev/ptmx", O_RDWR | O_NOCTTY | O_NONBLOCK );

		if( fd_in[FD_READ] < 0 ) {
			logger(ctx->owner, ctx, "Failed to open a pty to launch %s\n", cmd);
			FATAL("Failed to open pty - terminating\n");
			free(cmd);
			return 1;
		}

		char *slave_name = ptsname( fd_in[FD_READ] );
		x_printf(ctx, "slave_name = %s\n",slave_name);

		unlockpt( fd_in[FD_READ] );
		grantpt( fd_in[FD_READ] );

		fd_in[FD_WRITE] = fd_out[FD_READ] = open( slave_name, O_RDWR );

		if( fd_in[FD_WRITE] < 0 ) {

			logger(ctx->owner, ctx, "Failed to open pty-slave for %s\n",cmd);
			FATAL("Failed to open pty-slave\n");
			close( fd_in[FD_READ] );
			free(cmd);
			return 1;
		}
	} else {
		if( pipe( fd_in ) || pipe( fd_out ) ) {
			int i;
			for(i=0;i<2;i++) {
				if( fd_in[i] )
					close( fd_in[i] );
				if( fd_out[i] )
					close( fd_out[i] );
			}
			free(cmd);
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
			event_add( ctx, cf->fd_in, EH_READ | EH_EXCEPTION );
		} else {
			event_add( ctx, cf->fd_in, EH_READ );
			event_add( ctx, cf->fd_out, EH_EXCEPTION );
		}

		x_printf(ctx,"New child process (pid = %d, fd = %d/%d) %s\n",pid,cf->fd_in,cf->fd_out, cmd );
		free( cmd );
	} else {
		// Child.  convert to stdin/stdout
		//int fd;

		x_printf(ctx,"replacing stdin and out/err with %d / %d\n",fd_out[FD_READ], fd_in[FD_WRITE]);
		if( dup2( fd_out[FD_READ], 0 ) < 0 ) {
			x_printf(ctx, "Failed to set up STDIN with %d\n", fd_out[FD_READ]);
		}
		if( dup2( fd_in[FD_WRITE], 1 ) < 0 ) {
			x_printf(ctx, "Failed to set up STDOUT with %d\n", fd_in[FD_WRITE]);
		}
		if( dup2( fd_in[FD_WRITE], 2 ) < 0 ) {
			x_printf(ctx, "Failed to set up STDERR with %d\n", fd_in[FD_WRITE]);
		}

		close( fd_in[FD_READ] );
		close( fd_in[FD_WRITE] );
		close( fd_out[FD_READ] );
		close( fd_out[FD_WRITE] );

		sigset_t all_signals;
		sigemptyset( &all_signals );
		sigaddset( &all_signals, SIGQUIT );
		sigaddset( &all_signals, SIGCHLD );
		sigprocmask(SIG_SETMASK, &all_signals, NULL);

		//for(fd = 3; fd < 1024; fd ++)
			//close(fd);
		
		execv(exe,vec);
		x_printf(ctx,"%s - Execv failed!\n", ctx->name);
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

	if( event != EVENT_TICK )
		x_printf(ctx,"event = \"%s\" (%d)\n", event_map[event], event);

	if( event_data->type == TYPE_FD )
		fd = & event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch( event ) {
		case EVENT_INIT:
			x_printf(ctx, "INIT event triggered\n");
			{
				event_add( ctx, 0, EH_WANT_TICK );
				event_add( ctx, SIGQUIT, EH_SIGNAL );
				event_add( ctx, SIGCHLD, EH_SIGNAL );
				event_add( ctx, SIGPIPE, EH_SIGNAL );

				if( config_istrue( ctx->config, "tty" ) )
					cf->flags |= EXEC_TTY_REQUIRED;

				if( ! exec_launch( ctx, (int) (cf->flags & EXEC_TTY_REQUIRED) ) ) {
					cf->state = EXEC_STATE_RUNNING;
					// Let the parent know the process has started/restarted
					driver_data_t notification = { TYPE_CHILD, ctx, {} };
					notification.event_child.action = CHILD_EVENT;
					notification.event_child.status = 0;
					emit(ctx->owner, EVENT_RESTART, &notification);
				}
				else {
					x_printf(ctx,"%s - exec() failed to start process\n", ctx->name);
					cf->state = EXEC_STATE_ERROR;
					return context_terminate(ctx);
				}
			}
			break;

		case EVENT_TERMINATE:
			x_printf(ctx,"%s - Got a termination event.  Cleaning up\n", ctx->name);
			cf->pending_action_timestamp = rel_time(0L);

			if( cf->state == EXEC_STATE_RUNNING ) {
				event_delete( ctx, cf->fd_out, EH_NONE );
				// This should trigger eof-on-input for the child, who should terminate
				close( cf->fd_out );
				cf->flags |= EXEC_TERMINATING;
			} else {
				context_terminate( ctx );
            }
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
                if( sig ) {
					x_printf(ctx,"Sending signal %d to process\n",sig);
                    kill( cf->pid, sig );
				}

                close(cf->fd_out);
                event_delete( ctx, cf->fd_out, EH_NONE );
                cf->fd_out = -1;

                close(cf->fd_in);
                event_delete( ctx, cf->fd_in, EH_NONE );
                cf->fd_in = -1;
				cf->state = EXEC_STATE_STOPPING;

				cf->pending_action_timestamp = rel_time(0L);

                x_printf(ctx,"%s - Got a restart request (signal = %d)..\n", ctx->name, sig);
            }
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if(( cf->state == EXEC_STATE_RUNNING) && data ) {
				
				x_printf(ctx,"Got a DATA event from my parent...\n" );
				ssize_t items = u_ringbuf_write( &cf->output, data->data, data->bytes );
				x_printf(ctx,"write %d bytes to ring buffer\n", items);

				if( !u_ringbuf_empty( &cf->output ) ) {
					x_printf( ctx, "Adding WRITE event to FD_out\n");
					event_add( ctx, cf->fd_out, EH_WRITE );
				}

				// items = number of items queued.. or -1 for error.
				x_printf(ctx,"EVENT_DATA - adding %d bytes to ring buffer\n",items);
				return items;
			}

			break;

		case EVENT_WRITE:
			x_printf(ctx,"Got a write event on fd %d\n",fd->fd);
			
			if( u_ringbuf_ready( &cf->output )) {
				u_ringbuf_write_fd( &cf->output, cf->fd_out );
			}

			if( u_ringbuf_empty( &cf->output ) )
				event_delete( ctx, cf->fd_out, EH_WRITE );

			break;

		case EVENT_READ:
			{
				x_printf(ctx,"%s - Read event triggerred for fd = %d\n", ctx->name, fd->fd);
				size_t bytes;
				event_bytes( fd->fd, &bytes );
				if( bytes ) {

					x_printf(ctx,"%s - Read event for fd = %d (%d bytes)\n", ctx->name, fd->fd, bytes);
					char read_buffer[MAX_READ_BUFFER];

					if( bytes >= MAX_READ_BUFFER ) {
						bytes = MAX_READ_BUFFER-1;
						x_printf(ctx,"%s - WARNING: Truncating read to %d bytes\n",ctx->name, bytes);
					}

					ssize_t result = event_read( fd->fd, read_buffer, bytes);
					x_printf(ctx,"%s - Read event returned %d bytes of data\n",ctx->name, bytes);

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
					x_printf(ctx,"%s - EOF on input. Cleaning up\n", ctx->name);
					if( fd->fd == cf->fd_in ) {
						x_printf(ctx,"Child program has terminated\n");

						//x_printf(ctx,"EOF - closing input file descriptor %d\n", cf->fd_in);
						event_delete( ctx, cf->fd_in, EH_NONE );
						close( cf->fd_in );
						cf->fd_in = -1;

						if( cf->state == EXEC_STATE_STOPPING ) {
							// program termination already signalled
							if( (cf->flags & (EXEC_RESPAWN|EXEC_TERMINATING)) == EXEC_RESPAWN ) {
								cf->state = EXEC_STATE_IDLE;
								cf->pending_action_timestamp = rel_time(0L);
								//return emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
							} else
								return context_terminate( ctx );
						} else
							cf->state = EXEC_STATE_STOPPING;
					}
				}

			}
			break;

		case EVENT_SEND_SIGNAL:
			x_printf(ctx,"%s - Asked to send a signal to process\n",ctx->name);
			if( cf->pid > 0 ) {
				kill( cf->pid, event_data->event_signal );
#ifndef NDEBUG
			} else {
				x_printf(ctx,"%s - No process to signal\n", ctx->name);
#endif
			}
			break;

		case EVENT_SIGNAL:
			if( event_data->event_signal == SIGCHLD ) {
				x_printf(ctx,"%s - Reaping deceased child (expecting pid %d)\n",ctx->name, cf->pid);
				int status;
				int rc = event_waitchld(&status, cf->pid);

				if( rc == cf->pid ) {
					x_printf(ctx, "PID matches.  cleaning up\n");
					// disable further events being recognized for this process
					cf->pid = -1;
					//x_printf(ctx,"SIGNAL - closing output file descriptor\n");
					close(cf->fd_out);
					event_delete( ctx, cf->fd_out, EH_NONE );
					cf->fd_out = -1;
					// Program termination already signalled
					if( cf->state == EXEC_STATE_STOPPING ) {
						x_printf(ctx,"state is STOPPING.\n");
						if( cf->flags & (EXEC_RESPAWN|EXEC_RESTARTING) ) {
							cf->flags &= ~(unsigned int)EXEC_RESTARTING;
							x_printf(ctx,"setting up for respawn after %d seconds.\n",cf->restart_delay);
							cf->state = EXEC_STATE_IDLE;
							cf->pending_action_timestamp = rel_time(0L);
							//return emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
						} else if( cf->flags & EXEC_TERMINATING ) {
							x_printf(ctx,"terminating.");
							return context_terminate( ctx );
						}
					} else {
						x_printf(ctx,"Signal before EOF.  Setting state to STOPPING\n");
						// Process may have terminated, but you cannot assume output has drained.
						cf->state = EXEC_STATE_STOPPING;
					}
				} else {
					x_printf(ctx, "PID does not match.  Ignoring.\n");
				}
			}
			break;

		case EVENT_TICK:
			{
				time_t now = rel_time(0L);

				cf->last_tick = now;
				if( cf->flags & (EXEC_TERMINATING|EXEC_RESTARTING) ) {
					if( cf->pid > 0 ) {
						if(( now - cf->pending_action_timestamp ) > (EXEC_PROCESS_TERMINATION_TIMEOUT*2) ) {
							x_printf(ctx,"%s - REALLY Pushing it along with a SIGKILL (pid = %d)\n", ctx->name, cf->pid);
							kill( cf->pid, SIGKILL );
						} else if(( now - cf->pending_action_timestamp ) > EXEC_PROCESS_TERMINATION_TIMEOUT ) {
							x_printf(ctx,"%s - Pushing it along with a SIGTERM (pid = %d)\n", ctx->name, cf->pid);
							kill( cf->pid, SIGTERM );
						}
					}
				}

				if( cf->state == EXEC_STATE_IDLE ) {
					x_printf(ctx,"process state is idle.  process is not running.\n");
					x_printf(ctx,"timer = %ld seconds\n", now - cf->pending_action_timestamp );
				}

				if( (cf->state == EXEC_STATE_IDLE) && (( now - cf->pending_action_timestamp ) > cf->restart_delay )) {
					x_printf(ctx,"Restart delay expired.  Restarting\n");
					emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
				}
			}
			break;

		default:
			x_printf(ctx,"\n *\n *\n * %s - Emitted some kind of event \"%s\" (%d)\n *\n *\n", ctx->name, event_map[event], event);
	}
	return 0;
}
