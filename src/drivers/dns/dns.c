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
	dns_config_t *cf = (dns_config_t *) ctx->data;

	if( cf->sock_fd )
		close( cf->sock_fd);

	free( cf );

	return 1;
}

ssize_t dns_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	dns_config_t *cf = (dns_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	switch( event ) {
		case EVENT_INIT:
		case EVENT_START:

			cf->state = DNS_STATE_RUNNING;
			x_printf(ctx,"calling event ALARM add %d ALARM_INTERVAL (control file check)\n",DNS_CONTROL_CHECK_INTERVAL);

#ifndef NDEBUG
			x_printf(ctx,"calling event ALARM add %d ALARM_INTERVAL (emergency shutdown)\n",300000);
			event_alarm_add( ctx, 300000, ALARM_INTERVAL );
#endif
			break;

		case EVENT_RESTART:
			break;

		case EVENT_CHILD:
			break;

		case EVENT_TERMINATE:
			if( cf->dns_timer )
				event_alarm_delete( ctx, cf->dns_timer );

			context_terminate( ctx );
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			break;

		case EVENT_SIGNAL:
			break;

		case EVENT_ALARM:
			{
				if( event_data->event_alarm == cf->dns_timer ) {
#ifdef ENABLE_GETADDRINFO
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
				}
			}
			break;

		case EVENT_READ:
			x_printf(ctx,"Got a read event on file descriptor %ld",event_data->event_request.fd);
			break;

        default:
            x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
    }
    return 0;
}

int dns_resolve_host(context_t *ctx, char *host )
{
	dns_config_t *cf = (dns_config_t *) ctx->data;

    unsigned char buf[65536],*qname;
	//unsigned char *reader;
    int i;
 
    struct sockaddr_in dest;
 
    struct DNS_HEADER *dns = NULL;
    struct QUESTION *qinfo = NULL;
 
    int s = socket(AF_INET , SOCK_DGRAM , IPPROTO_UDP);
 
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    dest.sin_addr.s_addr = inet_addr(cf->dns_servers[0]);
 
    dns = (struct DNS_HEADER *)&buf;
 
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
 
    qname =(unsigned char*)&buf[sizeof(struct DNS_HEADER)];
 
    dns_dnsformat(ctx, qname , host);
    qinfo =(struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname) + 1)]; //fill it
 
    qinfo->qtype = htons( 1 );
    qinfo->qclass = htons(1); //its internet (lol)
 
    printf("\nSending Packet...");
    if( sendto(s,(char*)buf,sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION),0,(struct sockaddr*)&dest,sizeof(dest)) < 0)
    {
        perror("sendto failed");
    }
    printf("Done");
     
    //Receive the answer
    i = sizeof dest;
    printf("\nReceiving answer...");
    if(recvfrom (s,(char*)buf , 65536 , 0 , (struct sockaddr*)&dest , (socklen_t*)&i ) < 0)
    {
        perror("recvfrom failed");
    }
    printf("Done");
 
    dns = (struct DNS_HEADER*) buf;
 
    //move ahead of the dns header and the query field
    //reader = &buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)];
 
    return dns->ans_count;
}
 
int dns_load_dns( context_t *ctx )
{
	dns_config_t *cf = (dns_config_t *) ctx->data;

	FILE *fp;
	char line[200] , *p;
	if((fp = fopen("/etc/resolv.conf" , "r")) == NULL) {
		logger(ctx, "Failed to open /etc/resolv.conf file");
		return 0;
	}

	while(fgets(line , 200 , fp)) {
		if(line[0] == '#')
			continue;
		if(strncmp(line , "nameserver" , 10) == 0) {
			p = strtok(line , " ");
			p = strtok(NULL , " ");
			x_printf(ctx,"Loaded DNS Server %s\n",p);
			strcpy(cf->dns_servers[cf->dns_max_servers++], p );
		}
	}
	fclose( fp );

	return 1;
}
 
void dns_dnsformat(context_t *ctx, unsigned char* dns, char* host)
{
    unsigned int lock = 0 , i;
    strcat((char*)host,".");
	(void)ctx;
     
    for(i = 0 ; i < strlen((char*)host) ; i++) {
        if(host[i]=='.') {
            *dns++ = (u_char) (i-lock);
            for(;lock<i;lock++) {
                *dns++=(unsigned char) host[lock];
            }
            lock++;
        }
    }
    *dns++='\0';
}
