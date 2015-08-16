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

context_t *start_driver( const char *driver_name, const config_t *parent_config )
{
	const driver_t *driver = find_driver(driver_name);
	const config_t *driver_config = config_get_section(driver_name);

	if( driver ) {
		context_t *ctx = context_create(driver_name, parent_config, driver, driver_config);
		d_printf("context_create(%s) returned %p\n", driver_name, ctx);
		d_printf("find_driver(%s) returned %p\n", driver_name,  driver);
		if (driver->init(ctx)) {
			d_printf("emit(\"init\") to driver %s\n", driver_name);
			ctx->flags = CTX_RUNNING;
			ctx->driver->emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
			return ctx;
		} else
			context_delete(ctx, NULL);
	} else
		d_printf("Unable to locate driver %s for %s\n",driver_name,parent_config->section);

	d_printf("start driver \"%s\" returning failure\n",driver_name );
	return 0L;
}

context_t *start_service( const char *name, const config_t *parent_config )
{
	context_t *ctx = 0L;

	if( find_driver( name ) )
		return start_driver( name, parent_config);

	d_printf("Checking for service config by the name of %s\n",name );

	const config_t *service_config = config_get_section( name );
	d_printf("get_section returned %s\n",service_config?service_config->section:"(no name)" );

	if( service_config ) {
		const char     **driver_list = config_get_itemlist(service_config, "driver");
		d_printf("driver_list = %p\n", driver_list);
		while( driver_list && *driver_list ) {
			d_printf("starting driver %s\n",*driver_list);
			const char     *driver_name = *driver_list++;
			if( driver_name ) {
				d_printf("Driver = %s\n", driver_name);

				d_printf("Calling \"start_context\" with driver name %s\n",driver_name);
				// note the pointless recursion... should stop encouraging nested configs
				ctx = start_service( driver_name, service_config );
				d_printf("start_context( %s ) return %p\n",driver_name, ctx);

				if( ! ctx ) {
					d_printf("Failed to start server or driver %s\n",driver_name );
				}
			} else {
				d_printf("No driver specified for %s\n",name);
			}
			d_printf("Nuking remaining drivers from list\n");
			driver_list = 0L; // all failures end up here.
		}
	} else {
		d_printf("Failed to start service %s\n",name);
	}
	d_printf("Complete.. returning %p\n",ctx);
	return ctx;
}

void run()
{
	while (!event_loop(10000)) {
	}

	d_printf("I'm outa here!\n");
}

int main(int ac, char *av[])
{
	parse_cmdline(ac, av);
	/*
	 * dump_cmdline();
	 */

	config_read_file(config_file);

	const char *default_service = config_item( "global", "default" );
	d_printf("default_service = %s\n", default_service);

	if( !default_service )
		default_service = "coordinator";

	event_subsystem_init();

	context_t *coord = start_service(default_service, config_get_section( "global" ) );

	if( !coord )
		fprintf(stderr,"Failed to start default service (%s)\n",default_service );

	run();

	config_cleanup();
	return 1;
}
