/*
 *
 * File: coordinator.c
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
#include "coordinator.h"
#include "logger.h"
#include "unicorn.h"
#include "clock.h"

int coordinator_init(context_t *context)
{
	coordinator_config_t *cf;

	if ( 0 == (cf = (coordinator_config_t *) calloc( sizeof( coordinator_config_t ) , 1 )))
		return 0;

	cf->state = COORDINATOR_STATE_IDLE;

	context->data = cf;

	return 1;
}

int coordinator_shutdown( context_t *ctx)
{
	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	if( cf->network )
		context_terminate( cf->network );
	if( cf->unicorn )
		context_terminate ( cf->unicorn );
	if( cf->icmp_sock )
		close( cf->icmp_sock );

	free( cf );

	return 1;
}

ssize_t coordinator_handler(context_t *ctx, event_t event, driver_data_t *event_data )
{
	event_data_t *data = 0L;
	event_child_t *child = 0L;
	context_t *source = event_data->source;

	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	x_printf(ctx, "<%s> Event = \"%s\" (%d)\n", ctx->name, event_map[event], event);

	if( event_data->type == TYPE_DATA )
		data = & event_data->event_data;
	else if( event_data->type == TYPE_CHILD )
		child = & event_data->event_child;

	switch( event ) {
		case EVENT_INIT:
			{
				cf->modem_driver = config_get_item( ctx->config, "modemdriver" );
				cf->network_driver = config_get_item( ctx->config, "networkdriver" );
				cf->vpn_driver = config_get_item( ctx->config, "vpndriver" );

				cf->control_modem = config_get_item( ctx->config, "control" );
				cf->control_vpn = config_get_item( ctx->config, "vpncontrol" );

				x_printf(ctx,"calling event add SIGTERM\n");
				event_add( ctx, SIGTERM, EH_SIGNAL );

				if( config_istrue( ctx->config, "ping", 0 ) ) {
					cf->flags |= COORDINATOR_PING_ENABLE;

					u_char i;
					for( i = 0; i < ICMP_DATALEN; i++ )
						cf->icmp_out[i] = i;

					cf->ping.icmp_ident = getpid();

					if( ! config_get_intval( ctx->config, "ping_retry", &cf->ping.icmp_max ) )
						cf->ping.icmp_max = 5;
					if( !  config_get_timeval( ctx->config, "ping_ttl", &cf->ping.icmp_ttl ) )
						cf->ping.icmp_ttl = 3000;
					if( !  config_get_timeval( ctx->config, "ping_interval", &cf->ping.icmp_interval ) )
						cf->ping.icmp_interval = COORDINATOR_ICMP_INTERVAL;

					const char *target = config_get_item( ctx->config, "ping_host" );

					memset( &cf->ping.icmp_dest, 0, sizeof( struct sockaddr_in ));
					cf->ping.icmp_dest.sin_family = AF_INET;
					if( ! inet_aton(target, &cf->ping.icmp_dest.sin_addr) ) {
						logger(ctx,"Unable to resolve ping host %s.  Disabling ping",target);
						cf->flags &= ~(unsigned int) COORDINATOR_PING_ENABLE;
					} else {
						cf->icmp_sock = socket( AF_INET, SOCK_RAW, IPPROTO_ICMP );
						if( cf->icmp_sock < 0 ) {
							logger(ctx,"Failed to create raw socket.  errno = %d. Disabling ping",errno);
							cf->flags &= ~(unsigned int) COORDINATOR_PING_ENABLE;
						} else {
							event_add( ctx, cf->icmp_sock, EH_READ );
						}
					}
				}

				if( config_istrue( ctx->config, "dns", 0 ) ) {
					if( !  config_get_timeval( ctx->config, "dns_interval", &cf->dns_interval ) )
						cf->dns_interval = 3000;
					const char *target = config_get_item( ctx->config, "dns_host" );
					if( target ) {
						cf->dns.dns_host = target;
						cf->flags |= COORDINATOR_DNS_ENABLE;
					}
				}

				if( config_istrue( ctx->config, "vpnalways", 0 ) )
					cf->flags |= COORDINATOR_VPN_STANDALONE;

				cf->flags |= COORDINATOR_NETWORK_DISABLE|COORDINATOR_VPN_DISABLE;
			}
		case EVENT_START:

			cf->state = COORDINATOR_STATE_RUNNING;
			x_printf(ctx,"calling event ALARM add %d ALARM_INTERVAL (control file check)\n",COORDINATOR_CONTROL_CHECK_INTERVAL);
			cf->timer_fd = event_alarm_add( ctx, COORDINATOR_CONTROL_CHECK_INTERVAL, ALARM_INTERVAL );

			if( cf->flags & COORDINATOR_PING_ENABLE ) {
				x_printf(ctx,"calling event ALARM add %ld ALARM_INTERVAL (ping)\n",cf->ping.icmp_interval);
				cf->icmp_timer = event_alarm_add( ctx, cf->ping.icmp_interval, ALARM_INTERVAL );
			}

			if( cf->flags & COORDINATOR_DNS_ENABLE ) {
				x_printf(ctx,"calling event ALARM add %ld ALARM_INTERVAL (ping)\n",cf->dns_interval);
				cf->dns_timer = event_alarm_add( ctx, cf->dns_interval, ALARM_INTERVAL );
			}

#ifndef NDEBUG
			x_printf(ctx,"calling event ALARM add %d ALARM_INTERVAL (emergency shutdown)\n",300000);
			event_alarm_add( ctx, 300000, ALARM_INTERVAL );
#endif
			check_control_files( ctx );
			break;

		case EVENT_RESTART:
			if( source == cf->unicorn ) {
				if( event_data->event_child.action == CHILD_EVENT ) {
					x_printf(ctx,"Unicorn driver notifying me that the modem driver has restarted.. terminating network driver\n");
					if( cf->network )
						emit( cf->network, EVENT_TERMINATE, 0L );
				}
			}
			break;

		case EVENT_CHILD:
			if( child->ctx == cf->unicorn ) {
				switch( child->action ) {
					case CHILD_STOPPED:
						x_printf(ctx,"Unicorn driver has exited.  Terminating\n");
						cf->unicorn = 0L;
						cf->flags &= ~(unsigned int) (COORDINATOR_MODEM_UP|COORDINATOR_MODEM_ONLINE);

						if( cf->network )
							emit( cf->network, EVENT_TERMINATE, 0L );

						if( (cf->flags & COORDINATOR_TERMINATING) && !cf->network && !cf->vpn )
							context_terminate( ctx );
						break;
					case CHILD_EVENT:
						switch( child->status ) {
							case UNICORN_MODE_ONLINE:
								cf->flags |= COORDINATOR_MODEM_ONLINE;
								if( cf->network ) {
									// Restart network driver maybe
									uint8_t sig = SIGKILL;
									driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
									notification.event_custom = &sig;
									emit( cf->network, EVENT_RESTART, &notification );
								} else {
									start_service( &cf->network, cf->network_driver, ctx->config, ctx, 0L );
									if( !cf->network )
										cf->flags |= COORDINATOR_TERMINATING;
								}
								break;
							case UNICORN_MODE_OFFLINE:
								cf->flags &= ~(unsigned int)COORDINATOR_MODEM_ONLINE;
								if( cf->network )
									emit( cf->network, EVENT_TERMINATE, 0L );
								break;
						}

						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->network ) {
				switch( child->action ) {
					case CHILD_STARTED:
						break;
					case CHILD_EVENT:
						cf->flags |= COORDINATOR_NETWORK_UP;
						x_printf(ctx,"NETWORK STATE CHANGED TO UP\n");

						if( !( cf->flags & COORDINATOR_VPN_STANDALONE )) {
							cf->flags |= COORDINATOR_VPN_DISABLE;
							if( cf->vpn )
								emit( cf->vpn, EVENT_TERMINATE, 0L );
						}

						break;
					case CHILD_STOPPED:
						x_printf(ctx,"NETWORK STATE CHANGED TO DOWN\n");
						cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_UP;
						cf->network = 0L;

						if( cf->vpn && !(cf->flags & COORDINATOR_VPN_STANDALONE) )
							emit( cf->vpn, EVENT_TERMINATE, 0L );

						if( cf->unicorn ) {
							if( cf->flags & COORDINATOR_MODEM_ONLINE ) {
								driver_data_t notification = { TYPE_CUSTOM, ctx, {} };
								emit( cf->unicorn, EVENT_RESTART, &notification );
							}
						}
						else if (cf->flags & COORDINATOR_TERMINATING)
							context_terminate(ctx);
						break;
					default:
						break;
				}
			}

			if( child->ctx == cf->vpn ) {
				switch( child->action ) {
					case CHILD_STARTED:
						cf->flags |= COORDINATOR_VPN_UP;
						break;
					case CHILD_STOPPED:
						cf->flags &= ~(unsigned int)COORDINATOR_VPN_UP;
						cf->vpn = 0L;

						cf->flags |= COORDINATOR_VPN_DISABLE;

						if( cf->flags & COORDINATOR_TERMINATING )
							context_terminate(ctx);
						break;
					default:
						break;
				}
			}
			break;

		case EVENT_TERMINATE:
			if( cf->vpn )
				emit( cf->vpn, EVENT_TERMINATE, 0L );

			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );

			if( cf->unicorn )
				emit( cf->unicorn, EVENT_TERMINATE, 0L );

			if( cf->state == COORDINATOR_STATE_RUNNING ) {
				cf->flags |= COORDINATOR_TERMINATING;
			} else
				context_terminate( ctx );
			break;

		case EVENT_DATA_INCOMING:
		case EVENT_DATA_OUTGOING:
			if( data ) {
				if( source == cf->unicorn && cf->network )
					return emit( cf->network, event, event_data );
				if( source == cf->network && cf->unicorn )
					return emit( cf->unicorn, event, event_data );
			}
			break;

		case EVENT_SIGNAL:
			if( event_data->event_signal == SIGTERM ) {
				kill(0,SIGTERM);
				exit(0);
			}
			break;

		case EVENT_ALARM:
			{
				time_t t = time(0L);
				char buffer[128];
				strftime(buffer,128,"%T ", localtime(&t));
				x_printf(ctx,"alarm occurred at %s\n",buffer);

				if( event_data->event_alarm == cf->timer_fd ) {
					check_control_files(ctx);

					if( cf->flags & COORDINATOR_VPN_STARTING ) {
						x_printf(ctx,"VPN Starting up soon...\n");
						time_t now = rel_time(0L);
						if( (now - cf->vpn_startup_pending) > VPN_STARTUP_DELAY ) {
							cf->flags &= ~(unsigned int)COORDINATOR_VPN_STARTING;
							if( ! start_service( &cf->vpn, cf->vpn_driver, ctx->config, ctx, 0L ) ) {
								x_printf(ctx,"Failed to start VPN.  Disabling - this will cause a retry\n");
								cf->flags |= COORDINATOR_VPN_DISABLE;
							}
						}
					}
				}
				else if ( event_data->event_alarm == cf->icmp_timer ) {
					if( (cf->flags & COORDINATOR_NETWORK_UP) && (cf->flags & COORDINATOR_PING_ENABLE) ) {
						if( !( cf->flags & COORDINATOR_PING_INPROGRESS )) {

							if( cf->icmp_retry_timer != -1 )
								event_alarm_delete( ctx, cf->icmp_retry_timer );

							cf->icmp_retry_timer = event_alarm_add( ctx, cf->ping.icmp_ttl, ALARM_INTERVAL );
							cf->icmp_retries = cf->ping.icmp_max;
							cf->flags |= COORDINATOR_PING_INPROGRESS;
							coordinator_send_ping(ctx);
						}
					}
				} else if ( event_data->event_alarm == cf->icmp_retry_timer ) {
					x_printf(ctx, "ICMP retry timer... retries = %d\n", cf->icmp_retries);
					if( cf->icmp_retries ) {
						coordinator_send_ping(ctx);
						cf->icmp_retries --;
					} else {
						event_alarm_delete( ctx, cf->icmp_retry_timer );
						logger(ctx, "Failed ping test.  Disconnecting network");
						event_alarm_delete( ctx, cf->icmp_retry_timer );
						if( cf->network )
							emit( cf->network, EVENT_TERMINATE, 0L );
					}
				} else if ( event_data->event_alarm == cf->dns_timer ) {
#ifdef ENABLE_GETADDRINFO
					if( (cf->flags & COORDINATOR_NETWORK_UP) && (cf->flags & COORDINATOR_DNS_ENABLE) ) {
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
#ifndef NDEBUG
				else {
					x_printf(ctx,"Triggering emergency termination\n");
					kill(0,SIGTERM);
					exit(0);
				}
#endif
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
					if( coordinator_check_ping(ctx, (size_t) bytes) ) {
						x_printf( ctx, "ICMP reply received.  Terminating ping test.");
						if( cf->icmp_retry_timer != -1 ) {
							event_alarm_delete( ctx, cf->icmp_retry_timer );
							cf->icmp_retry_timer = -1;
						}
						cf->flags &= ~(unsigned int)COORDINATOR_PING_INPROGRESS;
					}
				}
			}
			break;

        default:
            x_printf(ctx,"\n *\n *\n * Emitted some kind of event \"%s\" (%d)\n *\n *\n", event_map[event], event);
    }
    return 0;
}

int check_control_files(context_t *ctx)
{
	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

	struct stat info;

	int modem_enable = stat( cf->control_modem, &info ) ? 1:0;
	int vpn_enable   = stat( cf->control_vpn,   &info ) ? 0:1;

	if( cf->flags & COORDINATOR_NETWORK_DISABLE ) {
		if( modem_enable ) {
			// ensure connection happens
			cf->flags &= ~(unsigned int)COORDINATOR_NETWORK_DISABLE;
			if( cf->modem_driver )
				start_service( &cf->unicorn, cf->modem_driver, ctx->config, ctx, 0L );
		}
	} else {
		if( !modem_enable ) {
			// ensure connection happens
			cf->flags |= COORDINATOR_NETWORK_DISABLE;
			if( cf->network )
				emit( cf->network, EVENT_TERMINATE, 0L );
			if( cf->unicorn )
				emit( cf->unicorn, EVENT_TERMINATE, 0L );
		}
	}

	if( cf->flags & COORDINATOR_VPN_DISABLE ) {
		x_printf(ctx,"VPN marked disabled\n");
		if( vpn_enable ) {
			x_printf(ctx,"Control file exists.. enabling\n");
			cf->flags &= ~(unsigned int)COORDINATOR_VPN_DISABLE;
			if( cf->flags & (COORDINATOR_VPN_STANDALONE|COORDINATOR_NETWORK_UP) ) {
				x_printf(ctx,"Setting up to launch vpn service %s after startup delay of %d\n", cf->vpn_driver, VPN_STARTUP_DELAY);
				if( !cf->vpn ) {
					cf->flags |= COORDINATOR_VPN_STARTING;
					cf->vpn_startup_pending = rel_time(0L);
				}
			}
		}
	} else {
		if( !vpn_enable ) {
			cf->flags |= COORDINATOR_VPN_DISABLE;
			if( cf->vpn )
				emit( cf->vpn, EVENT_TERMINATE, 0L );
		}
	}

	return 0;
}

int coordinator_send_ping(context_t * ctx)
{
	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

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

	icp->checksum = coordinator_cksum((u_short *) icp, cc);

	i = sendto(cf->icmp_sock, (char *)cf->icmp_out, cc, 0, (const struct sockaddr *)&cf->ping.icmp_dest, sizeof(struct sockaddr));

	if (i < 0)
		perror("ping: sendto");

	x_printf(ctx,"Ping Sent\n");
	return 0;
}

int coordinator_check_ping(context_t *ctx, size_t bytes)
{
	coordinator_config_t *cf = (coordinator_config_t *) ctx->data;

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

u_int16_t coordinator_cksum(u_short * addr, size_t len)
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
