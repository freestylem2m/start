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

#include "config.h"
#include "driver.h"
#include "events.h"

context_t *start_driver( const char *driver_name, context_t *parent, const char *service_name, const config_t *service_config )
{
	const driver_t *driver = find_driver(driver_name);
	const config_t *driver_config = config_get_section(driver_name);

	if( driver ) {
		context_t *ctx = context_create(service_name, service_config, driver, driver_config);
		d_printf("context_create(%s) returned %p\n", service_name, ctx);
		d_printf("find_driver(%s) returned %p\n", driver_name,  driver);
		if (driver->init(ctx)) {
			//d_printf("emit(\"init\") to driver %s\n", driver_name);
			//emit( ctx, EVENT_INIT, DRIVER_DATA_NONE );
			if( parent ) {
				set_child( parent, ctx );
				set_child( ctx, parent );
			}
			return ctx;
		} else
			context_delete(ctx, NULL);
	} else
		d_printf("Unable to locate driver %s for %s\n",driver_name,service_name);

	d_printf("start driver \"%s\" returning failure\n",driver_name );
	return 0L;
}

context_t *start_context( const char *name, context_t *parent, const char *service_name, const config_t *service_config )
{
	context_t *ctx = 0L;

	if( find_driver( name ) )
		return start_driver( name, parent, service_name, service_config );

	d_printf("Checking for service config by the name of %s\n",name );

	const config_t *service = config_get_section( name );
	d_printf("get_section returned %s\n",service?service->section:"(no name)" );

	if( service ) {
		const char     **driver_list = config_get_itemlist(service, "driver");
		d_printf("driver_list = %p\n", driver_list);
		while( driver_list && *driver_list ) {
			d_printf("starting driver %s\n",*driver_list);
			const char     *driver_name = *driver_list++;
			if( driver_name ) {
				d_printf("Driver = %s\n", driver_name);

				d_printf("Calling \"start_context\" with driver name %s\n",driver_name);
				ctx = start_context( driver_name, parent, name, service );
				d_printf("start_context( %s ) return %p\n",driver_name, ctx);

				if( ctx ) {
					d_printf("Saving new driver as parent\n");
					parent = ctx;
					continue;
				} else {
					d_printf("Failed to start server or driver %s\n",driver_name );
				}
			} else {
				d_printf("No driver specified for %s\n",service_name);
			}
			d_printf("Nuking remaining drivers from list\n");
			driver_list = 0L; // all failures end up here.
		}
	}
	d_printf("Complete.. returning %p\n",ctx);
	return ctx;
}

void autostart_services()
{
	const char    **auto_start = config_itemlist("global", "auto_start");

	while (auto_start && *auto_start) {
		const char     *service_name = *auto_start;
		d_printf("Need to start service %s\n", service_name);

		context_t *ctx = start_context( service_name, 0L, 0L, 0L );

		if( ctx == 0L ) {
			d_printf("Failed to start service %s\n",service_name);
		} else {
			context_awaken( ctx );
		}

		auto_start++;
	}
}

void run()
{
	// Initial health check to clean up drivers which failed to start
	context_check_health();

	while (!event_loop(10000)) {
		context_check_health();
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

	autostart_services();

	run();

	config_cleanup();
	return 1;
}
