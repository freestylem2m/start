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
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "driver.h"
#include "events.h"

context_t *safe_start_service( const char *name, const config_t *parent_config, context_t *owner, int depth )
{
	context_t *ctx = 0L;

	if( find_driver( name ) )
		return start_driver( name, parent_config->section, parent_config, owner );

	const config_t *service_config = config_get_section( name );

    if( service_config == parent_config || depth > CONFIG_MAX_DEPTH ) {
        fprintf(stderr, "Error starting service %s: recursion in service configuration\n", name);
        exit (0);
    }

	if( service_config ) {
		const char     **driver_list = config_get_itemlist(service_config, "driver");
		while( driver_list && *driver_list ) {
			const char     *driver_name = *driver_list++;
			if( driver_name ) {
				ctx = start_driver( driver_name, name, service_config, owner );

				if( ctx )
                    continue;
			}
			driver_list = 0L; // all failures end up here.
		}
	}
	return ctx;
}

context_t *start_service( const char *name, const config_t *parent_config, context_t *owner )
{
	d_printf("start_service(%s) called\n", name );
    return safe_start_service( name, parent_config, owner, 0 );
}

void run()
{
	while (!event_loop(1000))
	{
	}

	d_printf("I'm outa here!\n");
}

int main(int ac, char *av[])
{
	parse_cmdline(ac, av);
	// dump_cmdline();

	if( config_read_file(config_file) < 0) {
		fprintf(stderr,"Unable to read config file %s\n",config_file);
		exit(-1);
	}

	const char **default_service = config_itemlist( "global", "default" );
	//d_printf("default_service = %s\n", default_service);

	if( !default_service ) {
		fprintf(stderr,"There was a problem with the configuration.  No default services to start\n");
	}

	event_subsystem_init();

	while( *default_service ) {
		context_t *coord = start_service(*default_service, config_get_section( "global" ), 0L );
		if( !coord ) {
			fprintf(stderr,"Failed to start default service (%s)\n", *default_service );
			exit(0);
		}
		default_service ++;
	}


	run();

	config_cleanup();
	return 1;
}
