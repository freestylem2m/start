/*
 * File: netmanage.c
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
 *
 */

#include "netmanage.h"
#include "cmdline.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "config.h"
#include "driver.h"
#include "events.h"

context_t *safe_start_service( context_t **pctx, const char *name, const config_t *parent_config, context_t *owner, void *pdata, int depth )
{
	context_t *ctx = 0L;

	if( find_driver( name ) ) {

		//d_printf("Calling start driver %s\n",name);
		context_t *m = start_driver( pctx, name, parent_config->section, parent_config, owner, pdata );
		//d_printf("Start driver %s returned %p\n",name,m);
		return m;
	}

	const config_t *service_config = config_get_section( name );

    if( service_config == parent_config || depth > CONFIG_MAX_DEPTH ) {
        fprintf(stderr, "Error starting service %s: recursion in service configuration\n", name);
        cleanup (0);
    }

	if( service_config ) {
		const char     **driver_list = config_get_itemlist(service_config, "driver");
		while( driver_list && *driver_list ) {
			const char     *driver_name = *driver_list++;
			if( driver_name )
				if( (ctx = start_driver( pctx, driver_name, name, service_config, owner, pdata )) )
                    continue;

			//d_printf("last driver returned %p (pctx = %p)\n",ctx, *pctx);
#if 0
			if( pctx && *pctx )
				d_printf("last driver aka %s\n",(*pctx)->name) ;
#endif
			driver_list = 0L; // all failures end up here.
		}
	} else {
        fprintf(stderr, "Unable to find service %s\n", name);
        cleanup (0);
	}
	return ctx;
}

context_t *start_service( context_t **pctx, const char *name, const config_t *parent_config, context_t *owner, void *pdata )
{
    return safe_start_service( pctx, name, parent_config, owner, pdata, 0 );
}

#define PID_BUFFER_MAX 32
int check_pid_file(void)
{
    const char *pid_file = config_item( "global", "pidfile" );
    struct stat info;
    char pid_buffer[PID_BUFFER_MAX];
    int pid, pid_fd;
	ssize_t pid_length;

    if( !pid_file )
        return 0;

#if 0
	d_printf("Checking PID file %s\n", pid_file );
#endif
    if( ! stat( pid_file, &info ) ) {
        pid_fd = open( pid_file, O_RDONLY );
        if( pid_fd ) {
            pid_length = read( pid_fd, pid_buffer, PID_BUFFER_MAX-1 );
            close( pid_fd );

            if(pid_length >= 0) {
                pid_buffer[pid_length] = 0;
                pid = atoi( pid_buffer );

				if( !kill(pid, 0 ) )
					return pid;
            }
        }
        unlink( pid_file );
    }

    pid_fd = open( pid_file, O_CREAT|O_TRUNC|O_WRONLY, 0644 );
    pid_length = snprintf(pid_buffer,PID_BUFFER_MAX, "%d\n",getpid());

    if( pid_fd >= 0 ) {
        if( pid_length >= 0 )
            if( write( pid_fd, pid_buffer, (size_t) pid_length ) < 0 )
				fprintf(stderr,"Failed to write pid file %s: %s\n", pid_file, strerror( errno ));

        close( pid_fd );
    }

    return 0;
}

void cleanup_pid_file(void)
{
    const char *pid_file = config_item( "global", "pidfile" );

	if( pid_file )
		unlink( pid_file );
}

void run()
{
	while (!event_loop(1000))
	{
	}

	d_printf("I'm outa here!\n");
}

void cleanup(int code)
{
	driver_cleanup();
	config_cleanup();
	cleanup_pid_file();

	exit( code );
}

int main(int ac, char *av[])
{
	int last_pid = 0;

	parse_cmdline(ac, av);

	if( config_read_file(config_file) < 0) {
		fprintf(stderr,"Unable to read config file %s\n",config_file);
		exit(-1);
	}

	if( (last_pid = check_pid_file()) ) {
		fprintf(stderr,"%s process already running (pid = %d)\n",av[0],last_pid);
		cleanup( 1 );
	}

	const char **default_service = config_itemlist( "global", "default" );

	if( !default_service ) {
		fprintf(stderr,"There was a problem with the configuration.  No default services to start\n");
		exit(-1);
	}

	event_subsystem_init();

	while( *default_service ) {
		context_t *coord = start_service( 0L, *default_service, config_get_section( "global" ), 0L, 0L );

		if( !coord ) {
			fprintf(stderr,"Failed to start default service (%s)\n", *default_service );
			cleanup(1);
		}

		default_service ++;
	}


	run();

	cleanup(0);
	/* NOT_REACHED */
	return 0;
}
