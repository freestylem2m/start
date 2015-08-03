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

void launch_paired_services(const config_t * driver)
{
	d_printf("Not doing this yet...\n");
	exit(0);
	driver = 0L;
}

void autostart_services()
{
	const char    **auto_start = config_itemlist("global", "auto_start");

	while (auto_start && *auto_start) {
		const char     *service_name = *auto_start;
		d_printf("Need to start service %s\n", service_name);

		const config_t *service_config = config_get_section(service_name);

		if (service_config) {
			if (config_get_entry(service_config, "pipe")) {
				launch_paired_services(service_config);
			} else {
#ifndef NDEBUG
				config_entry_t *e = service_config->config;
				d_printf("[%s]\n", service_name);
				while (e) {
					d_printf("      Entry: %s=%s\n", e->item, e->value);
					e = e->next;
				}
#endif
				d_printf("Found service %s\n", service_config->section);

				const char     *driver_name = config_get_item(service_config, "driver");
				if( driver_name ) {
					d_printf("Driver = %s\n", driver_name);

					const driver_t *driver = find_driver(driver_name);
					const config_t *driver_config = config_get_section(driver_name);

					if( driver ) {
						context_t      *ctx = context_create(service_name, service_config, driver, driver_config);
						d_printf("context_create() returned %p\n", ctx);
						d_printf("find_driver() returned %p\n", driver);
						if (driver->init(ctx)) {
							d_printf("emit(\"init\") to driver %s\n", driver_name);
							emit( ctx, EVENT_INIT, NULL );
						} else
							context_delete(ctx, NULL);
					} else
						d_printf("Unable to locate driver %s for %s\n",driver_name,service_name);
				} else {
					d_printf("No driver specified for %s\n",service_name);
				}
			}
		} else {
			fprintf(stderr, "Unable to locate autostart service %s\n", service_name);
			exit(-1);
		}

		auto_start++;
	}
}

void run()
{
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
