/*
 * File: sshvpn.c
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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "sshvpn.h"
#include "logger.h"
#include "clock.h"

int sshvpn_init(context_t *context)
{
	sshvpn_config_t *cf;

	if ( 0 == (cf = (sshvpn_config_t *) calloc( sizeof( sshvpn_config_t ) , 1 )))
		return 0;

	cf->state = SSHVPN_STATE_IDLE;

	context->data = cf;

	return 1;
}

int sshvpn_shutdown( context_t *ctx)
{
	sshvpn_config_t *cf = (sshvpn_config_t *) ctx->data;

	if( cf->network )
		context_terminate( cf->network );
	if( cf->transport )
		context_terminate( cf->transport );

	if( cf->resolver_file )
		sshvpn_manage_resolver(ctx, RESOLVER_RESTORE);

	if( cf->sock_fd >= 0 )
		close( cf->sock_fd );

	sshvpn_manage_resolver(ctx, RESOLVER_RELEASE);

	free( cf );

	return 1;
}

int sshvpn_manage_resolver(context_t *ctx, sshvpn_resolver_action_t action)
{
	sshvpn_config_t *cf = (sshvpn_config_t *) ctx->data;

	if( !cf->resolver_file )
		return 0;

	struct stat info;

	switch(action) {
		case RESOLVER_BACKUP:
			x_printf(ctx,"RESOLVER BACKUP:\n");
			if( !stat(cf->resolver_file, &info ) ) {

				if( cf->resolver_data ) {
					free( cf->resolver_data );
					cf->resolver_data = 0L;
					cf->resolver_data_size = -1;
				}

				cf->resolver_data = calloc( (size_t) info.st_size + 1, 1 );

				if( cf->resolver_data ) {
					int fd = open( cf->resolver_file, O_RDONLY );
					if( fd < 0 ) {
						free( cf->resolver_data );
						cf->resolver_data = 0L;
						return -1;
					}


					cf->resolver_data_size = (ssize_t) read( fd, cf->resolver_data, (size_t) info.st_size );
					x_printf(ctx,"Backed up %d bytes\n",(int)cf->resolver_data_size);
					x_printf(ctx,"Resolver file contains [%s]\n",cf->resolver_data);

					close( fd );
				}
			}
			break;

		case RESOLVER_RESTORE:
			x_printf(ctx,"RESOLVER RESTORE:\n");
			if( cf->resolver_data && (cf->resolver_data_size > 0)) {
				int fd = open( cf->resolver_file, O_CREAT|O_TRUNC|O_WRONLY );
				if( fd < 0 )
					return -1;
				write( fd, cf->resolver_data, (size_t) cf->resolver_data_size );
				close( fd );
			}
			break;

		case RESOLVER_RELEASE:
			x_printf(ctx,"RESOLVER RELEASE:\n");
			if( cf->resolver_data ) {
				free( cf->resolver_data );
				cf->resolver_data = 0L;
				cf->resolver_data_size = -1;
			}
			break;

		default:
			return -1;
			break;
	}
	return 0;
}

int sshvpn_find_network_interface( context_t *ctx )
{
#define SSHVPN_IF_BUFFER 1024
	sshvpn_config_t *cf = (sshvpn_config_t *) ctx->data;

	char *buf = alloca(SSHVPN_IF_BUFFER);
	struct ifconf ifc;
	struct ifreq  ifr;
	struct ifreq *pifr;
	int           nInterfaces;
	int           i;

	ifc.ifc_len = SSHVPN_IF_BUFFER;
	ifc.ifc_buf = buf;

	if( cf->sock_fd < 0 )
		return 0;

	if(ioctl(cf->sock_fd, SIOCGIFCONF, &ifc) < 0)
		return 0;

	pifr         = ifc.ifc_req;
	nInterfaces = ifc.ifc_len / (int) sizeof(struct ifreq);
	for(i = 0; i < nInterfaces; i++)
	{
		struct ifreq *item = &pifr[i];
		if( !strcasecmp( item->ifr_name, cf->interface ) ) {

			memset( &ifr, 0, sizeof( ifr ));
			strncpy( ifr.ifr_ifrn.ifrn_name, cf->interface, IFNAMSIZ );

			if( ioctl( cf->sock_fd, SIOCGIFDSTADDR, &ifr ) < 0 )
				d_printf("SIOCGIFDSTADDR failz %s\n",strerror(errno));

			cf->interface_addr = ((struct sockaddr_in *)&(ifr.ifr_ifru.ifru_dstaddr))->sin_addr.s_addr;
			d_printf("Interface address = %08x\n", cf->interface_addr);
			return 1;
		}
	}

	return 0;
}

in_addr_t sshvpn_route_to_host(const char *rt)
{
	struct in_addr in;

	char *prt = alloca(strlen(rt)+1);
	strcpy(prt,rt);

	char *mask = strchr(prt, '/' );
	if( mask )
		*mask = 0;

	if( inet_aton( prt, &in ) )
		return in.s_addr;
	
	return 0;
}

in_addr_t sshvpn_route_to_mask(const char *rt)
{
	char *mask;
	int prefix = 24;

	if( (mask = strchr(rt,'/')))
		prefix = atoi(++mask);

	return htonl(0xFFFFFFFF << ( 32 - prefix ));
}

int sshvpn_manage_routes(context_t *ctx)
{
	sshvpn_config_t *cf = (sshvpn_config_t *) ctx->data;

	const char **routes = cf->route_info;

	struct rtentry  rm;

	while( *routes ) {
		memset(&rm, 0, sizeof(rm));

		((struct sockaddr_in *)&rm.rt_gateway)->sin_family = AF_INET;
		((struct sockaddr_in *)&rm.rt_gateway)->sin_addr.s_addr = cf->interface_addr;
		((struct sockaddr_in *)&rm.rt_gateway)->sin_port = 0;
		((struct sockaddr_in *)&rm.rt_dst)->sin_family = AF_INET;
		((struct sockaddr_in *)&rm.rt_dst)->sin_addr.s_addr = sshvpn_route_to_host(*routes);
		((struct sockaddr_in *)&rm.rt_dst)->sin_port = 0;
		((struct sockaddr_in *)&rm.rt_genmask)->sin_family = AF_INET;
		((struct sockaddr_in *)&rm.rt_genmask)->sin_addr.s_addr = sshvpn_route_to_mask(*routes);
		((struct sockaddr_in *)&rm.rt_genmask)->sin_port = 0;

		rm.rt_dev = (char *) cf->interface;
		rm.rt_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;

		d_printf("route gateway = %08x\n", ((struct sockaddr_in *)&rm.rt_gateway)->sin_addr.s_addr);
		d_printf("route address = %08x\n", ((struct sockaddr_in *)&rm.rt_dst)->sin_addr.s_addr);
		d_printf("route mask = %08x\n", ((struct sockaddr_in *)&rm.rt_genmask)->sin_addr.s_addr);
		d_printf("route interface = %s\n", rm.rt_dev);

		if (ioctl(cf->sock_fd, SIOCADDRT, &rm) < 0) {
			x_printf(ctx,"SIOCADDRT failed - %s\n",strerror(errno));
			return 0;
		}

		routes ++;
	}
	return 1;
}

ssize_t sshvpn_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_data_t *data = 0L;
	event_child_t *child = 0L;
	context_t *source = event_data->source;

	sshvpn_config_t *cf = (sshvpn_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;
	else if( event_data->type == TYPE_CHILD )
		child = & event_data->event_child;

	switch( event ) {
		case EVENT_INIT:
			cf->transport_driver = config_get_item( ctx->config, "transportdriver" );
			cf->network_driver = config_get_item( ctx->config, "networkdriver" );
			cf->resolver_file = config_get_item( ctx->config, "resolver" );
			cf->interface = config_get_item( ctx->config, "interface" );
			cf->route_info = config_get_itemlist( ctx->config, "routes" );

			x_printf(ctx, "transport driver = %s\n",cf->transport_driver );
			x_printf(ctx, "network driver = %s\n",cf->network_driver );

			x_printf(ctx,"calling event 1000 EH_WANT_TICK\n");
			event_add( ctx, 1000, EH_WANT_TICK );

			cf->sock_fd = socket( AF_INET, SOCK_DGRAM, 0 );
			cf->state = SSHVPN_STATE_RUNNING;

		case EVENT_START:
			if( !cf->network_driver )
				context_terminate( ctx );
			else {
				sshvpn_manage_resolver(ctx, RESOLVER_BACKUP);

				if( cf->transport_driver )
					start_service( &cf->transport, cf->transport_driver, ctx->config, ctx, 0L );
				else
					start_service( &cf->network,  cf->network_driver, ctx->config, ctx, 0L );
			}

			break;

		case EVENT_CHILD:
			x_printf(ctx,"got event_child from %s\n",source->name);

			if( child->ctx == cf->transport ) {
				x_printf(ctx,"noting change of state for transport layer: %d\n", child->action);
				switch( child->action ) {
					case CHILD_STOPPED:
						cf->flags &= ~(unsigned int)SSHVPN_TRANSPORT_UP;
						x_printf(ctx,"transport driver has exited.  Terminating\n");
						cf->transport = 0L;

						if( cf->network ) {
							x_printf(ctx,"Telling network layer to terminate\n");
							emit( cf->network, EVENT_TERMINATE, 0L );
						} else
							context_terminate( ctx );
						break;
					case CHILD_EVENT:
						cf->flags |= SSHVPN_TRANSPORT_UP;
						x_printf(ctx,"Got a CHILD_EVENT status = %d, launching network driver\n",child->status);

						if( !cf->network ) {
							x_printf(ctx,"calling start_service(%s)\n",cf->network_driver);
							start_service( &cf->network, cf->network_driver, ctx->config, ctx, 0L );
						} else {
							x_printf(ctx,"network driver is already running?\n");
						}

						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->network ) {
				x_printf(ctx,"noting change of state for network layer: %d\n",child->action);
				switch( child->action ) {
					case CHILD_STARTED:
						cf->flags |= SSHVPN_NETWORK_UP;
						break;
					case CHILD_STOPPED:
						cf->flags &= ~(unsigned int)SSHVPN_NETWORK_UP;
						cf->network = 0L;

						if( cf->transport ) {
							x_printf(ctx,"telling transport layer to restart\n");
							emit( cf->transport, EVENT_TERMINATE, 0l );
						} else
							context_terminate(ctx);
						break;

					case CHILD_EVENT:
						if( ctx->owner )
							x_printf(ctx,"Notifying parent (%s) that the vpn is up\n",ctx->owner->name);
						context_owner_notify( ctx, CHILD_EVENT, 0 );
						break;
					default:
						break;
				}
			}
			break;

		case EVENT_RESTART:
		case EVENT_TERMINATE:
			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );

			if( cf->transport )
				emit( cf->transport, EVENT_TERMINATE, 0L );

			if( cf->state == SSHVPN_STATE_RUNNING ) {
				cf->flags |= SSHVPN_TERMINATING;
			} else
				context_terminate( ctx );
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			{
#ifndef NDEBUG
				unsigned char *dptr = (unsigned char *)(data->data);
				char binary[128];
				unsigned char text[40];
				size_t p = data->bytes;
				size_t i = 0;
				x_printf(ctx,"INCOMING DATA - source = %s\n",source->name );
				while( i < p ) {
					char *ptr = binary;
					unsigned char *tptr = text;
					size_t j;
					for( j = 0; j < 16; j++ ) {
						if( j+i < p ) {
							ptr += sprintf(ptr, " %02x", dptr[j+i]);
							*tptr++ = (unsigned char) isprint((int)(dptr[j+i]))?dptr[j+i]:'.';
						} else {
							ptr += sprintf(ptr,"   ");
							*tptr++ = ' ';
						}
					}
					*tptr = 0;
					printf("%04x: %s %s\n",(unsigned int)i,binary,text);
					i+=j;
				}
#endif
				if( (source == cf->transport) && cf->network )
					return emit( cf->network, event, event_data );

				if( (source == cf->network) && cf->transport )
					return emit( cf->transport, event, event_data );

				x_printf(ctx,"Tunnel not available - discarding data\n");
				return (ssize_t) data->bytes;
			}
			break;

		case EVENT_TICK:
		case EVENT_ALARM:

			if( !(cf->flags & SSHVPN_IF_UP) ) {
				x_printf(ctx,"checking if vpn interface is up\n");
				if( sshvpn_find_network_interface( ctx ) ) {
					x_printf(ctx,"Found it !!!!\n");
					if( sshvpn_manage_routes(ctx) )
						cf->flags |= SSHVPN_IF_UP;
				}
			}
			break;

		default:
			x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
	}
    return 0;
}
