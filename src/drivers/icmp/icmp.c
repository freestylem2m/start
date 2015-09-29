/*
 *
 * File: ping.c
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
#include "logger.h"
#include "unicorn.h"
#include "clock.h"
#include "icmp.h"

int icmp_init(context_t *context)
{
	icmp_config_t *cf;

	if ( 0 == (cf = (icmp_config_t *) calloc( sizeof( icmp_config_t ) , 1 )))
		return 0;

	cf->state = ICMP_STATE_IDLE;

	context->data = cf;

	return 1;
}

int icmp_shutdown( context_t *ctx)
{
	icmp_config_t *cf = (icmp_config_t *) ctx->data;

	if( cf->icmp_sock >= 0 )
		close( cf->icmp_sock );

	if( cf->icmp_timer != -1 )
		event_alarm_delete( ctx, cf->icmp_timer );

	free( cf );

	return 1;
}

ssize_t icmp_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	icmp_config_t *cf = (icmp_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	switch( event ) {
		case EVENT_INIT:
			{
				u_char i;
				for( i = 0; i < ICMP_DATALEN; i++ )
					cf->icmp_out[i] = i;

				cf->ping.icmp_ident = getpid();

				if( event_data->type == TYPE_CUSTOM && event_data->event_custom ) {
					x_printf(ctx,"Importing config from icmp_conf structure\n");
					//icmp_conf_t *conf = (icmp_conf_t *) (event_data->event_custom);
				} else {

					x_printf(ctx,"Importing config from configuration section %s\n",ctx->config->section);

					if( ! config_get_timeval( ctx->config, "timeout", &cf->ping.icmp_timeout ) )
						cf->ping.icmp_timeout = 3000;

					if( ! config_get_intval( ctx->config, "retry", &cf->icmp_retries ) )
						cf->icmp_retries = 5;

					const char *target = config_get_item( ctx->config, "host" );
					memset( &cf->ping.icmp_dest, 0, sizeof( struct sockaddr_in ));
					cf->ping.icmp_dest.sin_family = AF_INET;

					if( ! inet_aton(target, &cf->ping.icmp_dest.sin_addr) ) {
						logger(ctx,"Unable to resolve ping host %s.  Disabling ping",target);
					} else {
						cf->icmp_sock = socket( AF_INET, SOCK_RAW, IPPROTO_ICMP );
						if( cf->icmp_sock < 0 ) {
							logger(ctx,"Failed to create raw socket.  errno = %d. Disabling ping",errno);
							return context_terminate( ctx );
						} else {
							event_add( ctx, cf->icmp_sock, EH_READ );
							cf->icmp_timer = event_alarm_add( ctx, cf->ping.icmp_timeout, ALARM_INTERVAL );
							icmp_send_ping(ctx);
						}
					}
				}
			}

		case EVENT_START:
			cf->state = ICMP_STATE_RUNNING;
			break;

		case EVENT_RESTART:
			break;

		case EVENT_CHILD:
			break;

		case EVENT_TERMINATE:
			context_terminate( ctx );
			break;

		case EVENT_SIGNAL:
			break;

		case EVENT_ALARM:
			{
				time_t t = time(0L);
				char buffer[128];
				strftime(buffer,128,"%T ", localtime(&t));
				x_printf(ctx,"alarm occurred at %s\n",buffer);

				if ( event_data->event_alarm == cf->icmp_timer ) {
					x_printf(ctx, "ICMP retry timer... retries = %d\n", cf->icmp_retries);
					if( cf->icmp_retries > 0 ) {
						icmp_send_ping(ctx);
					} else {
						event_alarm_delete( ctx, cf->icmp_timer );
						event_delete( ctx, cf->icmp_sock, EH_NONE );
						logger(ctx, "Failed ping test.");
						context_owner_notify( ctx, CHILD_EVENT, (unsigned long) 0 );
						context_terminate( ctx );
					}
				} 
			}
			break;

		case EVENT_READ:
			x_printf(ctx,"Got a read event on file descriptor %ld\n",event_data->event_request.fd);
			if( event_data->event_request.fd == cf->icmp_sock ) {
				struct sockaddr_in from;
				socklen_t fromlen;
				ssize_t bytes = recvfrom(cf->icmp_sock, cf->icmp_in, ICMP_PAYLOAD, 0, (struct sockaddr *)&from, &fromlen);
				if( bytes < 0 ) {
					if( errno != EINTR )
						logger(ctx,"Error receiving ping data from network");
				} else {
					x_printf(ctx,"received %d bytes from ICMP socket\n", (int) bytes);
					if( icmp_check_ping(ctx, (size_t) bytes) ) {
						x_printf( ctx, "ICMP reply received.  Terminating ping test.\n");
						if( cf->icmp_timer != -1 ) {
							event_alarm_delete( ctx, cf->icmp_timer );
							cf->icmp_timer = -1;
						}
						cf->flags &= ~(unsigned int)ICMP_PING_INPROGRESS;
						context_owner_notify( ctx, CHILD_EVENT, (unsigned long) cf->ping.icmp_sequence );
						context_terminate( ctx );
					}
				}
			}
			break;

        default:
            x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
    }
    return 0;
}

int icmp_send_ping(context_t * ctx)
{
	icmp_config_t *cf = (icmp_config_t *) ctx->data;

	x_printf(ctx,"Coordinator send_ping()\n");

	register struct icmphdr *icp;
	register size_t     cc;
	ssize_t             i;

	// Update sequence count before sending packet.
	cf->ping.icmp_sequence = (u_int16_t) ((cf->ping.icmp_sequence + 1) & ICMP_SEQUENCE_MASK);

	icp = (struct icmphdr *)cf->icmp_out;
	icp->type = ICMP_ECHO;
	icp->code = 0;
	icp->checksum = 0;
	icp->un.echo.sequence = (u_int16_t) cf->ping.icmp_sequence;
	icp->un.echo.id = (u_int16_t) cf->ping.icmp_ident;

	cc = (size_t) ICMP_DATALEN + sizeof(struct icmphdr);

	icp->checksum = icmp_cksum((u_short *) icp, cc);

	i = sendto(cf->icmp_sock, (char *)cf->icmp_out, cc, 0, (const struct sockaddr *)&cf->ping.icmp_dest, sizeof(struct sockaddr));

	if (i < 0)
		perror("ping: sendto");

	x_printf(ctx,"Ping Sent\n");

	if( cf->icmp_retries > 0 )
		cf->icmp_retries --;

	return 0;
}

int icmp_check_ping(context_t *ctx, size_t bytes)
{
	icmp_config_t *cf = (icmp_config_t *) ctx->data;

	x_printf(ctx,"Checking ping packet\n");

	struct icmphdr *icp;

	struct iphdr *ip = (struct iphdr *)cf->icmp_in;
	size_t hlen = (size_t) ip->ihl << 2;

	if( bytes < ICMP_MINLEN + ICMP_DATALEN ) {
		x_printf(ctx,"Packet from network too short");
		return 0;
	}
	bytes -= hlen;
	icp = (struct icmphdr *) &cf->icmp_in[hlen];

	x_printf(ctx,"icmp ident = %d (should be %d)\n",icp->un.echo.id, cf->ping.icmp_ident);
	x_printf(ctx,"icmp sequence = %d (should be %d)\n",icp->un.echo.sequence, cf->ping.icmp_sequence);
	x_printf(ctx,"icmp type = %d (should be %d)\n",icp->type, ICMP_ECHOREPLY);

	if( icp->un.echo.id != cf->ping.icmp_ident ) {
		// someone elses packet
		return 0;
	}

	if( icp->type != ICMP_ECHOREPLY ) {
		// some other ICMP packet type
		return 0;
	}

	// If my timeout was too short, there is a chance I'll get an older ICMP response..
	if( icp->un.echo.sequence > cf->ping.icmp_sequence )
		return 0;

	u_char *cp = (u_char *)icp + sizeof(struct icmphdr);
	u_char *dp = (u_char *)&cf->icmp_out[sizeof(struct icmphdr)];

	size_t i;
	for(i = 0; i < ICMP_DATALEN; i++, cp++, dp++ ) {
		if( *cp != *dp )
			x_printf(ctx,"mismatch at %i, %d instead of %d\n",(int) i,*cp,*dp);
	}

	return 1;
}

u_int16_t icmp_cksum(u_short * addr, size_t len)
{
	register size_t     nleft = len;
	register u_short   *w = addr;
	register int        sum = 0;
	u_short             answer = 0;

	/*
	 * Our algorithm is simple, using a 32 bit accumulator (sum), we add
	 * sequential 16 bit words to it, and at the end, fold back all the
	 * carry bits from the top 16 bits into the lower 16 bits.
	 */
	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}

	/*
	 * mop up an odd byte, if necessary
	 */
	if (nleft == 1) {
		*(u_char *) (&answer) = *(u_char *) w;
		sum += answer;
	}

	/*
	 * add back carry outs from top 16 bits to low 16 bits
	 */
	sum = (sum >> 16) + (sum & 0xffff);					   /* add hi 16 to low 16 */
	sum += (sum >> 16);									   /* add carry */
	answer = (u_short) ~sum;							   /* truncate to 16 bits */
	return (answer);
}
