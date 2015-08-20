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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "exec.h"

int exec_init(context_t *context)
{
	d_printf("Hello from EXEC INIT!\n");

	// register "emit" as the event handler of choice
	
	exec_config_t *cf;

	if ( 0 == (cf = (exec_config_t *) calloc( sizeof( exec_config_t ) , 1 )))
		return 0;

	cf->state = EXEC_STATE_IDLE;

	if( config_istrue( context->config, "respawn" ) )
		cf->flags |= EXEC_RESPAWN;

	u_ringbuf_init( &cf->output );

	context->data = cf;

	return 1;
}

int exec_shutdown(context_t *ctx)
{
	d_printf("Goodbye from EXEC!\n");

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
	//d_printf("cmd = %s\n", cmd);

	char           *cp = cmd;
	int             px = 0;

	while (cp && *cp && px < (CMD_ARGS_MAX - 1)) {
		//d_printf("cp = %s\n", cp);
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

int exec_launch( context_t *context )
{
	char exe[PATH_MAX];
	char *vec[CMD_ARGS_MAX];
	const char *cmdline;
	char *cmd;

	cmdline = config_get_item( context->config, "cmd" );

	if( !cmdline )
		return -1;

	cmd = strdup( cmdline );

	const char *path = process_command( cmd, exe, vec );

	exec_config_t *cf = (exec_config_t *) context->data;

	if( !path ) {
		d_printf("Failing with unable-to-find-file error\n");
		free(cmd);
		return -1;
	}
	
	int fd_in[2] = { 0,0 };
	int fd_out[2] = { 0,0 };
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

	int pid;

	if( (pid = fork()) ) {
		cf->pid = pid;
		// Parent.  Close the inverse file descriptors
		close( fd_in[FD_WRITE] );
		cf->fd_in = fd_in[FD_READ];
		close( fd_out[FD_READ] );
		cf->fd_out = fd_out[FD_WRITE];

		event_add( context, cf->fd_in, EH_READ );
		event_add( context, cf->fd_out, EH_EXCEPTION );

		free( cmd );
	} else {
		// Child.  convert to stdin/stdout
		int fd;

		dup2( fd_out[FD_READ], 0 );
		dup2( fd_in[FD_WRITE], 1 );
		dup2( fd_in[FD_WRITE], 2 );

		sigset_t all_signals;
		sigemptyset( &all_signals );
		sigaddset( &all_signals, SIGQUIT );
		sigaddset( &all_signals, SIGCHLD );
		sigprocmask(SIG_SETMASK, &all_signals, NULL);

		for(fd = 3; fd < 1024; fd ++)
			close(fd);
		
		execv(exe,vec);
		d_printf("Execv failed!\n");
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

	//d_printf("event = \"%s\" (%d)\n", event_map[event], event);

	if( event_data->type == TYPE_FD )
		fd = & event_data->event_request;
	else if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;

	switch( event ) {
		case EVENT_INIT:
			d_printf( "INIT event triggered\n");
			{
				event_add( ctx, 0, EH_WANT_TICK );
				event_add( ctx, SIGQUIT, EH_SIGNAL );
				event_add( ctx, SIGCHLD, EH_SIGNAL );
				event_add( ctx, SIGPIPE, EH_SIGNAL );

				if( ! exec_launch( ctx ) )
					cf->state = EXEC_STATE_RUNNING;
				else {
					d_printf("exec() failed to start process\n");
					cf->state = EXEC_STATE_ERROR;
					return context_terminate(ctx);
				}
			}
			break;

		case EVENT_TERMINATE:
			d_printf("Got a termination event.  Cleaning up\n");
			cf->termination_timestamp = time(0L);

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
                if( sig )
                    kill( cf->pid, sig );

                close(cf->fd_out);
                event_delete( ctx, cf->fd_out, EH_NONE );
                cf->fd_out = -1;

                d_printf("Got a restart request (signal = %d)..\n", sig);
            }
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				d_printf("Got a DATA event from my parent...\n");
				//d_printf("bytes = %d\n",data->bytes );
				//d_printf("buffer = %s\n", (char *) data->data );
				//d_printf("buffer[%d] = %d\n",data->bytes,((char *)data->data)[data->bytes]);
				//d_printf("writing to file %d\n",cf->fd_out);
				//d_printf("state = %d\n",cf->state);
				if( cf->state == EXEC_STATE_RUNNING )
					if( write( cf->fd_out, data->data, data->bytes ) < 0)
						d_printf("Failed to forward incoming data\n");
			} else {
				d_printf("Got a DATA event from my parent... WITHOUT ANY DATA!!!\n");
				ssize_t items = u_ringbuf_write( &cf->output, data->data, data->bytes );
				if( !u_ringbuf_empty( &cf->output ) )
					event_add( ctx, cf->fd_out, EH_WRITE );

				// items = number of items queued.. or -1 for error.
				return items;
			}

			break;

		case EVENT_READ:
			{
				d_printf("Read event triggerred for fd = %d\n",fd->fd);
				size_t bytes;
				event_bytes( fd->fd, &bytes );
				if( bytes ) {

					d_printf("Read event for fd = %d (%d bytes)\n",fd->fd, bytes);
					char read_buffer[MAX_READ_BUFFER];

					if( bytes >= MAX_READ_BUFFER ) {
						bytes = MAX_READ_BUFFER-1;
						d_printf("WARNING: Truncating read to %d bytes\n",bytes);
					}

					ssize_t result = event_read( fd->fd, read_buffer, bytes);
					d_printf("Read event returned %d bytes of data\n",bytes);

					if( result >= 0 ) {
						read_buffer[result] = 0;

						driver_data_t read_data = { TYPE_DATA, ctx, {} };
						read_data.event_data.data = read_buffer;
						read_data.event_data.bytes = (size_t) result;

						if( emit( ctx->owner, EVENT_DATA_INCOMING, &read_data ) != result )
							d_printf("WARNING: %s failed to send all data to %s\n",ctx->name,ctx->owner->name);

					} else
						d_printf(" * WARNING: read return unexpected result %d\n",result);
				} else {
					d_printf("EOF on input. Cleaning up\n");
					if( fd->fd == cf->fd_in ) {
						d_printf("Child program has terminated\n");

						//d_printf("EOF - closing input file descriptor %d\n", cf->fd_in);
						event_delete( ctx, cf->fd_in, EH_NONE );
						close( cf->fd_in );
						cf->fd_in = -1;

						if( cf->state == EXEC_STATE_STOPPING ) {
							// program termination already signalled
							if( (cf->flags & (EXEC_RESPAWN|EXEC_TERMINATING)) == EXEC_RESPAWN ) {
								cf->state = EXEC_STATE_IDLE;
								return emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
							} else
								context_terminate( ctx );
						}
						cf->state = EXEC_STATE_STOPPING;
					}
				}

			}
			break;

		case EVENT_SEND_SIGNAL:
			d_printf("Asked to send a signal to process\n");
			if( cf->pid > 0 ) {
				kill( cf->pid, event_data->event_signal );
#ifndef NDEBUG
			} else {
				d_printf("No process to signal\n");
#endif
			}
			break;

		case EVENT_SIGNAL:
			if( event_data->event_signal == SIGCHLD ) {
				//d_printf("Reaping deceased child (expecting pid %d)\n",cf->pid);
				int status;
				int rc = event_waitchld(&status, cf->pid);

				if( rc == cf->pid ) {
					// disable further events being recognized for this process
					cf->pid = -1;
					//d_printf("SIGNAL - closing output file descriptor\n");
					close(cf->fd_out);
					event_delete( ctx, cf->fd_out, EH_NONE );
					cf->fd_out = -1;
					// Program termination already signalled
					if( cf->state == EXEC_STATE_STOPPING ) {
						if( (cf->flags & (EXEC_RESPAWN|EXEC_TERMINATING)) == EXEC_RESPAWN ) {
							cf->state = EXEC_STATE_IDLE;
							return emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
						} else
							return context_terminate( ctx );
					}
					// Process may have terminated, but you cannot assume output has drained.
					cf->state = EXEC_STATE_STOPPING;
				}
			}
			break;

		case EVENT_TICK:
			{
				time( & cf->last_tick );
				if( cf->flags & EXEC_TERMINATING ) {
					if(( time(0L) - cf->termination_timestamp ) > (EXEC_PROCESS_TERMINATION_TIMEOUT*2) ) {
						d_printf("REALLY Pushing it along with a SIGKILL\n");
						kill( cf->pid, SIGKILL );
					} else if(( time(0L) - cf->termination_timestamp ) > EXEC_PROCESS_TERMINATION_TIMEOUT ) {
						d_printf("Pushing it along with a SIGTERM\n");
						kill( cf->pid, SIGTERM );
					}
				}
			}
			break;

		default:
			d_printf("\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
	return 0;
}
