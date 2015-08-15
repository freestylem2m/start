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

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "exec.h"

int exec_init(context_t *context)
{
	d_printf("Hello from EXEC INIT!\n");

	// register "emit" as the event handler of choice
	
	exec_config_t *cf;
	if ( 0 == (context->event = register_event_handler( context->name, context, exec_emit, EH_WANT_TICK )))
		return 0;

	if ( 0 == (cf = (exec_config_t *) calloc( sizeof( exec_config_t ) , 1 )))
		return 0;

	context->data = cf;

	if( config_istrue( context->config, "respawn" ) )
		cf->flags |= EXEC_RESPAWN;

	return 1;
}

int exec_shutdown( context_t *context)
{
	(void)(context);
	d_printf("Goodbye from EXEC INIT!\n");
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

int exec_launch( const context_t *context )
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

		event_add( context->event, cf->fd_in, EH_READ );
		event_add( context->event, cf->fd_out, EH_EXCEPTION );

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
int exec_emit(context_t *ctx, event_t event, driver_data_t *event_data )
{
	UNUSED(event_data);

	fd_list_t *fd = 0;
	exec_config_t *cf = (exec_config_t *) ctx->data;

	if( event_data->type == TYPE_FD )
		fd = event_data->fd_data;

	switch( event ) {
		case EVENT_INIT:
			d_printf( "INIT event triggered\n");
			{
				//event_add( ctx->event, 0, EH_READ );
				event_add( ctx->event, SIGQUIT, EH_SIGNAL );
				event_add( ctx->event, SIGCHLD, EH_SIGNAL );

				if( ! exec_launch( ctx ) ) {
					cf->state = EXEC_STATE_RUNNING;
				} else {
					d_printf("exec() failed to start process\n");
					cf->state = EXEC_STATE_ERROR;
				}
				return -1;
			}
			break;

		case EVENT_READ:
			{
				d_printf("Read event triggerred for fd = %d\n",fd->fd);
				size_t bytes;
				event_bytes( fd->fd, &bytes );
				d_printf("exec state = %d\n",cf->state );
				if( bytes ) {
					d_printf("Read event for fd = %d (%ld bytes)\n",fd->fd, bytes);
					char read_buffer[MAX_READ_BUFFER];

					ssize_t result = event_read( fd->fd, read_buffer, bytes);
					d_printf("Read event returned %ld bytes of data\n",bytes);

					if( result >= 0 ) {
						ssize_t rc;
						if( fd->fd == 0 )
							rc = write(cf->fd_out,read_buffer,(size_t) result);
						else
							rc = write(1,read_buffer,(size_t) result);
						if( rc != result )
							printf("write returned %ld\n",rc);
					}
				} else {

					d_printf("EOF on input. Cleaning up\n");
					d_printf("read = %d\n",cf->fd_in);
					d_printf("write = %d\n",cf->fd_out);
					d_printf("event file descriptor (%d)\n",fd->fd);

					if( fd->fd == cf->fd_in ) {
						d_printf("\n *\n * child program has terminated\n *\n\n");

						event_delete( ctx->event, cf->fd_in, EH_NONE );
						close( cf->fd_in );

						event_delete( ctx->event, cf->fd_out, EH_NONE );
						close( cf->fd_out );

						// Complete termination is a two step process.
						// 1) file descriptor event causing the closure of all file descriptors and disabling of file events
						// 2) sigchld event causing the reaping of the child and disabling of further sigchild events.
						if( cf->state == EXEC_STATE_STOPPING ) {
							// program termination already signalled
							if( (cf->flags & EXEC_RESPAWN) ) {
								d_printf("attempting to respawn\n");
								cf->state = EXEC_STATE_IDLE;
								return emit( ctx, EVENT_INIT, DRIVER_NONE );
							} else {
								d_printf("done here. cleaning up.\n");
								context_terminate( ctx );
								return 0;
							}
						}
						cf->state = EXEC_STATE_STOPPING;
					}
				}

			}
			break;

		case EVENT_EXCEPTION:
			d_printf("Got an exception on FD %d\n",fd->fd);
			break;

		case EVENT_SIGNAL:
			d_printf("Woa! Got a sign from the gods... %d\n",fd->fd);
			if( fd->fd == SIGCHLD ) {
				d_printf("Reaping deceased child (expecting pid %d)\n",cf->pid);
				int status;

				int rc = event_waitchld(&status, cf->pid);

				// Process has terminated but output has not yet drained (probably)
				if( rc == cf->pid ) {
					// disable further events being recognized for this process
					cf->pid = 0;
					if( cf->state == EXEC_STATE_STOPPING ) {
						// program termination already signalled
						d_printf(" ** now is a good time to die ** \n");
						if( (cf->flags & EXEC_RESPAWN) ) {
							cf->state = EXEC_STATE_IDLE;
							return emit( ctx, EVENT_INIT, DRIVER_NONE );
						} else {
							d_printf("done here. cleaning up.\n");
							context_terminate( ctx );
							return 0;
						}
					}
					cf->state = EXEC_STATE_STOPPING;
				}
			}
			break;

		case EVENT_TICK:
			{
				char buffer[64];
				time_t now = time(0L);
				strftime(buffer,64,"%T",localtime(&now));
				d_printf("%s:   ** Tick (%ld seconds) **\n", buffer, cf->last_tick ? time(0L)-cf->last_tick : -1);
				time( & cf->last_tick );
			}
			break;

		default:
			d_printf("Emitted some kind of event (%d)\n",event);
	}
	return 0;
}
