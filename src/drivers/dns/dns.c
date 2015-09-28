/*
 *
 * File: dns.c
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
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#include "netmanage.h"
#include "driver.h"
#include "events.h"
#include "dns.h"
#include "dns_conf.h"
#include "logger.h"
#include "unicorn.h"
#include "clock.h"

int dns_init(context_t *context)
{
	dns_config_t *cf;

	if ( 0 == (cf = (dns_config_t *) calloc( sizeof( dns_config_t ) , 1 )))
		return 0;

	cf->state = DNS_STATE_IDLE;

	context->data = cf;

	return 1;
}

int dns_shutdown( context_t *ctx)
{
	x_printf(ctx,"shutdown called...\n");
	dns_config_t *cf = (dns_config_t *) ctx->data;

	if( cf->sock_fd >= 0 )
		close( cf->sock_fd);

	if( cf->current_host )
		free( cf->current_host );

	if( cf->dns_timer >= 0 )
		event_alarm_delete( ctx, cf->dns_timer );

	free( cf );

	return 1;
}

ssize_t dns_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	dns_config_t *cf = (dns_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	switch( event ) {
		case EVENT_INIT:
			if( event_data->type == TYPE_CUSTOM && event_data->event_custom ) {
				dns_init_t *init = (dns_init_t *) (event_data->event_custom);
				dns_load_servers( ctx, init->dns_resolver );

				cf->dns_timeout = init->dns_timeout;
				cf->current_host = strdup( init->dns_host );
			} else {
				const char *resolver = config_get_item( ctx->config, "resolver" );
				const char *host = config_get_item( ctx->config, "host" );

				if( ! config_get_timeval( ctx->config, "timeout", & cf->dns_timeout ))
						cf->dns_timeout = DNS_DEFAULT_TIMEOUT;

				dns_load_servers(ctx, resolver);
				cf->current_host = strdup( host );
			}

			cf->sock_fd = dns_resolve_host( ctx, cf->current_host );

			if( cf->sock_fd >= 0 ) {
				cf->dns_retries = cf->dns_max_servers ;
				cf->dns_timer = event_alarm_add( ctx, cf->dns_timeout, ALARM_TIMER );
				event_add( ctx, cf->sock_fd, EH_READ );
				x_printf(ctx,"attempting to resolve hostname %s (%d attempts)\n",cf->current_host, cf->dns_retries );
			} else {
				x_printf(ctx,"Failed to send query to socket...\n");
			}

		case EVENT_START:
			cf->state = DNS_STATE_RUNNING;
			break;

		case EVENT_TERMINATE:
			context_terminate( ctx );
			break;

		case EVENT_ALARM:
			{
				if( event_data->event_alarm == cf->dns_timer ) {
#ifdef ENABLE_GETADDRINFO
					// This is entirely pointless, because it can't block!!
					if( (cf->flags & DNS_NETWORK_UP) && (cf->flags & DNS_DNS_ENABLE) ) {
						x_printf(ctx,"Attempting DNS resolver check for %s\n",cf->dns_host);
						struct addrinfo *addrinfo = NULL;
						int rc = getaddrinfo( cf->dns_host, NULL, NULL, &addrinfo );
						if( rc == 0 ) {
							x_printf(ctx,"Name resolver completed..\n");
							freeaddrinfo( addrinfo );
						} else {
							x_printf(ctx,"Failure performing DNS name resolution.   Disconnecting network\n");
							logger(ctx,"Failure performing DNS name resolution.   Disconnecting network");
						}
					}
#endif
					x_printf(ctx,"TIMEOUT: dns failed to respond, trying next server\n");
					if( cf->sock_fd >= 0 ) {
						close( cf->sock_fd );
						cf->sock_fd = -1;
						event_delete( ctx, cf->sock_fd, EH_NONE );
					}

					if( cf->dns_retries-- > 0 ) {
						cf->dns_current = ( cf->dns_current + 1 ) % cf->dns_max_servers;
						cf->sock_fd = dns_resolve_host( ctx, cf->current_host );

						if( cf->sock_fd >= 0 ) {
							cf->dns_timer = event_alarm_add( ctx, cf->dns_timeout, ALARM_TIMER );
							event_add( ctx, cf->sock_fd, EH_READ );
						}
					} else {
						x_printf(ctx,"DNS RETRIES EXHAUSTED. Terminating\n");
						context_owner_notify( ctx, CHILD_EVENT, 0 );
						context_terminate( ctx );
					}

				}
			}
			break;

		case EVENT_READ:
			if( event_data->event_request.fd == cf->sock_fd ) {
				x_printf(ctx,"Got a read event on file descriptor %ld\n",event_data->event_request.fd);
				in_addr_t rc = dns_handle_dns_response( ctx, &( event_data->event_request ));
				x_printf(ctx, "handle response returned %d (%s)\n",rc, inet_ntoa( *(struct in_addr *) &rc ));

				if( rc == (unsigned long) -1 ) {
					x_printf(ctx,"Error reading from socket, skipping\n");
				} else {
					event_alarm_delete( ctx, cf->dns_timer );
					event_delete( ctx, cf->sock_fd, EH_NONE );
					close( cf->sock_fd );
					cf->sock_fd = -1;

					context_owner_notify( ctx, CHILD_EVENT, rc );
					context_terminate( ctx );
				}
			}

			break;

        default:
            x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
    }
    return 0;
}

int dns_resolve_host(context_t *ctx, const char *host )
{
	dns_config_t *cf = (dns_config_t *) ctx->data;

    unsigned char *buf = alloca( sizeof( struct DNS_HEADER ) + sizeof( struct QUESTION ) + 100 );
	unsigned char *qname;

    struct sockaddr_in dest;

    struct DNS_HEADER *dns = NULL;
    struct QUESTION *qinfo = NULL;

    int s = socket(AF_INET , SOCK_DGRAM , IPPROTO_UDP);

    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    dest.sin_addr.s_addr = inet_addr(cf->dns_servers[cf->dns_current]);

    dns = (struct DNS_HEADER *)buf;

    dns->id = htons( (unsigned short) getpid());
    dns->qr = 0; //This is a query
    dns->opcode = 0; //This is a standard query
    dns->aa = 0; //Not Authoritative
    dns->tc = 0; //This message is not truncated
    dns->rd = 1; //Recursion Desired
    dns->ra = 0; //Recursion not available! hey we dont have it (lol)
    dns->z = 0;
    dns->ad = 0;
    dns->cd = 0;
    dns->rcode = 0;
    dns->q_count = htons(1); //we have only 1 question
    dns->ans_count = 0;
    dns->auth_count = 0;
    dns->add_count = 0;

    qname = buf+sizeof(struct DNS_HEADER);

    dns_dnsformat(ctx, qname , host);
    qinfo = (struct QUESTION*)(buf+sizeof(struct DNS_HEADER)+(strlen((const char*)qname) + 1));

    qinfo->qtype = htons( 1 );
    qinfo->qclass = htons( 1 );

    x_printf(ctx,"Sending DNS Request Packet...\n");
    if( sendto(s,(char*)buf,sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION),0,(struct sockaddr*)&dest,sizeof(dest)) < 0) {
        x_printf(ctx,"sendto() failed: %s\n",strerror(errno));
		close(s);
		s = -1;
    }
    //x_printf(ctx, "Done\n");

	return s;
}

in_addr_t dns_handle_dns_response(context_t *ctx, event_request_t *request)
{
	dns_config_t *cf = (dns_config_t *) ctx->data;

	size_t bytes;
    struct sockaddr_in dest;

	event_bytes( (int) request->fd, &bytes );

    unsigned char *buf = alloca( bytes+1 );

    size_t i = sizeof dest;

    x_printf(ctx, "Receiving answer...\n");

    if(recvfrom (cf->sock_fd,(char*)buf , 65536 , 0 , (struct sockaddr*)&dest , (socklen_t*)&i ) < 0) {
        x_printf(ctx,"recvfrom() failed: %s\n",strerror(errno));
		return (in_addr_t)(unsigned long)-1;
    }
    //x_printf(ctx, "Done\n");

    //struct DNS_HEADER *dns = (struct DNS_HEADER*) buf;

	unsigned char *ptr = buf + sizeof( struct DNS_HEADER );
	ptr += strlen( (char *) ptr ) + 1;
	ptr += sizeof( struct QUESTION );

	while (*ptr) {
		//x_printf(ctx, "Found %d (%02x)\n",*ptr, *ptr);
		if( *ptr >= 192 )
			ptr += 2;
		else
			ptr += strlen( (char *)ptr ) + 1;
	}

	struct R_DATA *r = (struct R_DATA *) ptr;

	ptr += sizeof( struct R_DATA );

	int type = ntohs( r->type );
	int length = ntohs( r->data_len );

#if 0
	x_printf(ctx, "type = %d\n",type);
	x_printf(ctx, "len = %d\n",length);
	x_printf(ctx, "Finished on %d (%02x)\n",*ptr, *ptr);
#endif

	if( type == 1 /* IPV4_A */ && length == sizeof( in_addr_t ) ) {
		return *(in_addr_t *)ptr;
	}

    return 0;
}

int dns_load_servers( context_t *ctx, const char *filename )
{
	dns_config_t *cf = (dns_config_t *) ctx->data;

	FILE *fp;
	char line[200], *p;
	if((fp = fopen(filename ? filename : "/etc/resolv.conf" , "r")) == NULL) {
		logger(ctx, "Failed to open file: %s", filename ? filename : "/etc/resolv.conf" );
		return 0;
	}

	while(fgets(line , 200 , fp)) {
		if(line[0] == '#')
			continue;
		if(strncmp(line , "nameserver" , 10) == 0) {
			p = strtok(line , " ");
			p = strtok(NULL , " \r\n");
			x_printf(ctx,"Loaded DNS Server %s\n",p);
			strcpy(cf->dns_servers[cf->dns_max_servers++], p );
		}
	}
	fclose( fp );

	return 1;
}

void dns_dnsformat(context_t *ctx, unsigned char *dns, const char *host)
{
    unsigned int lock = 0 , i;
	(void)ctx;
#if 0
	char *ddd = (char *) dns;
#endif

    for(i = 0 ; i < strlen((char*)host)+1 ; i++) {
        if(host[i]=='.' || host[i] == '\0') {
            *dns++ = (u_char) (i-lock);
            for(;lock<i;lock++) {
                *dns++=(unsigned char) host[lock];
            }
            lock++;
        }
    }
    *dns++='\0';

#if 0
	char buffer[1000];
	i = 0;

	fprintf(stderr,"ddd = %p\n",ddd);
	while( *ddd ) {
		int n = *ddd++;
		fprintf(stderr,"n = %d\n",n);
		i+=(unsigned int) sprintf(buffer+i, "<%d>", n);
		while(n--)
			buffer[i++] = *ddd++;
	}
	buffer[i] = 0;
	x_printf(ctx,"dns string = %s\n",buffer);
#endif
}
