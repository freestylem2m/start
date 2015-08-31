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

context_t *safe_start_service( context_t **pctx, const char *name, const config_t *parent_config, context_t *owner, void *pdata, int depth )
{
	context_t *ctx = 0L;

	if( pctx )
		printf("pctx = %p\n",*pctx);

	if( find_driver( name ) ) {

		d_printf("Calling start driver %s\n",name);
		context_t *m = start_driver( pctx, name, parent_config->section, parent_config, owner, pdata );
		d_printf("Start driver %s returned %p\n",name,m);
		return m;

		//return start_driver( pctx, name, parent_config->section, parent_config, owner, pdata );
	}

	const config_t *service_config = config_get_section( name );

    if( service_config == parent_config || depth > CONFIG_MAX_DEPTH ) {
        fprintf(stderr, "Error starting service %s: recursion in service configuration\n", name);
        exit (0);
    }

	if( service_config ) {
		const char     **driver_list = config_get_itemlist(service_config, "driver");
		while( driver_list && *driver_list ) {
			const char     *driver_name = *driver_list++;
			if( driver_name )
				if( (ctx = start_driver( pctx, driver_name, name, service_config, owner, pdata )) )
                    continue;

			d_printf("last driver returned %p (pctx = %p)\n",ctx, *pctx);
			if( pctx && *pctx )
				d_printf("last driver aka %s\n",(*pctx)->name) ;
			driver_list = 0L; // all failures end up here.
		}
	} else {
        fprintf(stderr, "Unable to find service %s\n", name);
        exit (0);
	}
	printf("Returning ctx = %p\n",ctx);
	return ctx;
}

context_t *start_service( context_t **pctx, const char *name, const config_t *parent_config, context_t *owner, void *pdata )
{
    return safe_start_service( pctx, name, parent_config, owner, pdata, 0 );
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

	if( config_read_file(config_file) < 0) {
		fprintf(stderr,"Unable to read config file %s\n",config_file);
		exit(-1);
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
			exit(0);
		}

		default_service ++;
	}


	run();

	config_cleanup();
	return 1;
}
