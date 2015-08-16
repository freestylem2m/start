
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#include <stdio.h>

int main(int ac __attribute__((unused)), char *av[] __attribute__((unused)))
{
	struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

	fd_set in;
	fd_set out;
	fd_set except;
	int max_fd = 3;
	int pass = 0;

	while (1) {
		FD_ZERO(&in);
		FD_ZERO(&out);
		FD_ZERO(&except);

		FD_SET(0, &in);
		FD_SET(1, &in);
		FD_SET(2, &in);
		FD_SET(1, &out);
		FD_SET(2, &out);
		FD_SET(0, &except);
		FD_SET(1, &except);
		FD_SET(2, &except);

		int rc = select( max_fd, &in, &out, &except, &tv );

		if( rc < 0 ) {
			fprintf(stderr," select returnd %d, aka %s\n",rc, strerror(errno));
			return(1);
		}
		fprintf(stderr,"Select rc = %d\n", rc);

		char snot[2];
		snot[1] = 0;
		ssize_t snotr = 0;

		int id = 0;
		while(rc) {
			if( FD_ISSET(id, &in ) ) {
				fprintf(stderr,"Can read from fd %d\n",id);
				ssize_t bytes = 0;
				ioctl( id, FIONREAD, &bytes );
				fprintf(stderr,"FIONREAD returned %ld bytes\n",bytes);
				if( bytes > 0 ) {
					snotr = read(id, snot, 1);
					fprintf(stderr,"read returned %ld\n",snotr);
				}
				rc--;
			}
			if( FD_ISSET(id, &out) ) {
				fprintf(stderr,"Can write to fd %d\n",id);
				snotr = write(id, snot, 1);
				fprintf(stderr,"write returned %ld\n",snotr);
				rc--;
			}
			if( FD_ISSET(id, &except) ) {
				fprintf(stderr,"Exception on fd %d\n",id);
				rc--;
			}

			id++;
		}

		if( pass > 5 )
			close(1);
		if( pass > 10 )
			close(0);

		printf("tick\n");
		usleep(500000);
	}


	return 0;
}
