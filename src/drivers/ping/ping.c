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
#include "ping.h"
#include "logger.h"
#include "unicorn.h"
#include "clock.h"

int ping_init(context_t *context)
{
	ping_config_t *cf;

	if ( 0 == (cf = (ping_config_t *) calloc( sizeof( ping_config_t ) , 1 )))
		return 0;

	cf->state = PING_STATE_IDLE;

	context->data = cf;

	return 1;
}

int ping_shutdown( context_t *ctx)
{
	ping_config_t *cf = (ping_config_t *) ctx->data;

	if( cf->icmp_sock )
		close( cf->icmp_sock );

	free( cf );

	return 1;
}

ssize_t ping_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	ping_config_t *cf = (ping_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	switch( event ) {
		case EVENT_INIT:
			{
				x_printf(ctx,"calling event add SIGTERM\n");
				event_add( ctx, SIGTERM, EH_SIGNAL );

					u_char i;
					for( i = 0; i < ICMP_DATALEN; i++ )
						cf->icmp_out[i] = i;

					cf->ping.icmp_ident = getpid();

					if( ! config_get_intval( ctx->config, "ping_retry", &cf->ping.icmp_max ) )
						cf->ping.icmp_max = 5;
					if( !  config_get_timeval( ctx->config, "ping_ttl", &cf->ping.icmp_ttl ) )
						cf->ping.icmp_ttl = 3000;
					if( !  config_get_timeval( ctx->config, "ping_interval", &cf->ping.icmp_interval ) )
						cf->ping.icmp_interval = PING_ICMP_INTERVAL;

					const char *target = config_get_item( ctx->config, "ping_host" );

					memset( &cf->ping.icmp_dest, 0, sizeof( struct sockaddr_in ));
					cf->ping.icmp_dest.sin_family = AF_INET;
					if( ! inet_aton(target, &cf->ping.icmp_dest.sin_addr) ) {
						logger(ctx,"Unable to resolve ping host %s.  Disabling ping",target);
					} else {
						cf->icmp_sock = socket( AF_INET, SOCK_RAW, IPPROTO_ICMP );
						if( cf->icmp_sock < 0 ) {
							logger(ctx,"Failed to create raw socket.  errno = %d. Disabling ping",errno);
						} else {
							event_add( ctx, cf->icmp_sock, EH_READ );
						}
					}
			}

		case EVENT_START:
			cf->state = PING_STATE_RUNNING;
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
					if( !( cf->flags & PING_PING_INPROGRESS )) {

						if( cf->icmp_retry_timer != -1 )
							event_alarm_delete( ctx, cf->icmp_retry_timer );

						cf->icmp_retry_timer = event_alarm_add( ctx, cf->ping.icmp_ttl, ALARM_INTERVAL );
						cf->icmp_retries = cf->ping.icmp_max;
						cf->flags |= PING_PING_INPROGRESS;
						ping_send_ping(ctx);
					}
				} else if ( event_data->event_alarm == cf->icmp_retry_timer ) {
					x_printf(ctx, "ICMP retry timer... retries = %d\n", cf->icmp_retries);
					if( cf->icmp_retries ) {
						ping_send_ping(ctx);
						cf->icmp_retries --;
					} else {
						event_alarm_delete( ctx, cf->icmp_retry_timer );
						logger(ctx, "Failed ping test.  Disconnecting network");
						event_alarm_delete( ctx, cf->icmp_retry_timer );
					}
				} 
			}
			break;

		case EVENT_READ:
			x_printf(ctx,"Got a read event on file descriptor %ld",event_data->event_request.fd);
			if( event_data->event_request.fd == cf->icmp_sock ) {
				struct sockaddr_in from;
				socklen_t fromlen;
				ssize_t bytes = recvfrom(cf->icmp_sock, cf->icmp_in, ICMP_PAYLOAD, 0, (struct sockaddr *)&from, &fromlen);
				if( bytes < 0 ) {
					if( errno != EINTR )
						logger(ctx,"Error receiving ping data from network");
				} else {
					x_printf(ctx,"received %d bytes from ICMP socket", (int) bytes);
					if( ping_check_ping(ctx, (size_t) bytes) ) {
						x_printf( ctx, "ICMP reply received.  Terminating ping test.");
						if( cf->icmp_retry_timer != -1 ) {
							event_alarm_delete( ctx, cf->icmp_retry_timer );
							cf->icmp_retry_timer = -1;
						}
						cf->flags &= ~(unsigned int)PING_PING_INPROGRESS;
					}
				}
			}
			break;

        default:
            x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
    }
    return 0;
}

int ping_send_ping(context_t * ctx)
{
	ping_config_t *cf = (ping_config_t *) ctx->data;

	x_printf(ctx,"Coordinator send_ping()\n");

	register struct icmphdr *icp;
	register size_t     cc;
	ssize_t             i;

	// Update sequence count before sending packet.
	cf->icmp_count = (u_int16_t) ((cf->icmp_count + 1) & ICMP_SEQUENCE_MASK);

	icp = (struct icmphdr *)cf->icmp_out;
	icp->type = ICMP_ECHO;
	icp->code = 0;
	icp->checksum = 0;
	icp->un.echo.sequence = cf->icmp_count;
	icp->un.echo.id = (u_int16_t) cf->ping.icmp_ident;

	cc = (size_t) ICMP_DATALEN + sizeof(struct icmphdr);

	icp->checksum = ping_cksum((u_short *) icp, cc);

	i = sendto(cf->icmp_sock, (char *)cf->icmp_out, cc, 0, (const struct sockaddr *)&cf->ping.icmp_dest, sizeof(struct sockaddr));

	if (i < 0)
		perror("ping: sendto");

	x_printf(ctx,"Ping Sent\n");
	return 0;
}

int ping_check_ping(context_t *ctx, size_t bytes)
{
	ping_config_t *cf = (ping_config_t *) ctx->data;

	x_printf(ctx,"Checking ping packet");

	struct icmphdr *icp;

	struct iphdr *ip = (struct iphdr *)cf->icmp_in;
	size_t hlen = (size_t) ip->ihl << 2;

	if( bytes < ICMP_MINLEN + ICMP_DATALEN ) {
		x_printf(ctx,"Packet from network too short");
		return 0;
	}
	bytes -= hlen;
	icp = (struct icmphdr *) &cf->icmp_in[hlen];

	x_printf(ctx,"ICMP type = %d\n",icp->type );

	if( icp->un.echo.id != cf->ping.icmp_ident ) {
		// some one elses packet
		return 0;
	}

	if( icp->type != ICMP_ECHOREPLY ) {
		// some other ICMP packet type
		return 0;
	}

	u_char *cp = (u_char *)icp + sizeof(struct icmphdr);
	u_char *dp = (u_char *)&cf->icmp_out[sizeof(struct icmphdr)];

	size_t i;
	for(i = 0; i < ICMP_DATALEN; i++, cp++, dp++ ) {
		if( *cp != *dp )
			x_printf(ctx,"mismatch at %i, %d instead of %d\n",(int) i,*cp,*dp);
	}

	x_printf(ctx,"icmp ident = %d (should be %d)",icp->un.echo.id, cf->ping.icmp_ident);
	x_printf(ctx,"icmp sequence = %d (should be %d)",icp->un.echo.sequence, cf->icmp_count);
	x_printf(ctx,"icmp type = %d (should be %d)",icp->type, ICMP_ECHOREPLY);

	return 1;
}

u_int16_t ping_cksum(u_short * addr, size_t len)
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
