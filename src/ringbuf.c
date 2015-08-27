#ifndef NDEBUG
//#define NDEBUG
#endif
/******************************************************************************************************
*
* Freestyle Technology Pty Ltd
*
* Copyright (c) 2015 Freestyle Technology Pty Ltd
*
* Name:            ringbuf.c
*
* Description:     Cheap and nasty circular I/O buffer implementation
*
* Project:         Freestyle Micro Engine
* Owner:           Freestyle Technology Pty Ltd
*
* THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
******************************************************************************************************/

// ***************************************************************************************************
// Crappy output buffer management. This has so many holes it should be named after cheese
// ***************************************************************************************************

#include <stdio.h>
#include <string.h>
#include <alloca.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "ringbuf.h"
#include "netmanage.h"

//
// Various ringbuffer related functions.
//

// Initialise the ring buffer with maximum and reset all counters.
void u_ringbuf_init( u_ringbuf_t *rb )
{
	rb->read_ptr = rb->write_ptr = 0;
}

// return 'true' if the buffer is empty
int u_ringbuf_empty(u_ringbuf_t *rb)
{
	return (rb->read_ptr == rb->write_ptr);
}

// return 'true' if the buffer is full
int u_ringbuf_full(u_ringbuf_t *rb)
{
	return ((rb->write_ptr - rb->read_ptr) >= RINGBUFFER_MAX );
}

// return the number of bytes of free space in the ring buffer
size_t u_ringbuf_avail( u_ringbuf_t *rb )
{
	return RINGBUFFER_MAX - (rb->write_ptr-rb->read_ptr);
}

// return the number of bytes ready to read
size_t u_ringbuf_ready( u_ringbuf_t *rb )
{
	return rb->write_ptr - rb->read_ptr;
}

// stream ring buffer data (all of it) to a file descriptor.
// Failure to write all data will terminate streaming and return
// with the number of bytes written so far, or -1 for error

ssize_t u_ringbuf_write_fd( u_ringbuf_t *rb, int fd)
{
	ssize_t bytes = 0;

	while(rb->read_ptr < rb->write_ptr) {
		size_t avail = rb->write_ptr - rb->read_ptr;
		size_t fixed_read = rb->read_ptr & RINGBUFFER_MODULO;

		if( ( fixed_read + avail ) > RINGBUFFER_MAX )
			avail = RINGBUFFER_MAX - fixed_read;

		//d_printf("write(%d,%p,%d)\n",fd,rb->buffer + fixed_read, avail );
		ssize_t written = write( fd, rb->buffer + fixed_read, avail );
		//d_printf("bytes written = %d\n", written);

		if( written > 0 ) {
			bytes += written;
			rb->read_ptr += (size_t)written;
		} else {
			if( written == 0 || errno == EAGAIN)
				break;

			return -1;
		}
	}

	if( rb->read_ptr == rb->write_ptr )
		rb->read_ptr = rb->write_ptr = 0;

	return bytes;
}

//
// Read data from the ring buffer.
//
// Like the 'write' and 'write_fd' functions, this copies as much as possible
// until the end of the buffer, adjusts itself, then restarts at the beginning
// of the buffer.
//
ssize_t u_ringbuf_read( u_ringbuf_t *rb, void *buffer, size_t length)
{
	size_t bytes = 0;

	while( length && rb->read_ptr < rb->write_ptr )
	{
		size_t avail = RINGBUFFER_MAX - (rb->write_ptr-rb->read_ptr);
		size_t fixed_read = rb->read_ptr & RINGBUFFER_MODULO;

		if( avail > length )
			avail = length;

		if( (fixed_read + avail ) > RINGBUFFER_MAX )
			avail = RINGBUFFER_MAX - fixed_read;

		memcpy( buffer, rb->buffer + fixed_read, avail );
		rb->read_ptr += avail;
		buffer += avail;
		bytes  += avail;
		length -= avail;
	}

	return (ssize_t)bytes;
}

//
// Write data into the ring buffer.
//
// WARNING:  "length" must be <= "u_ringbuf_avail( rb )" or it will
//           overwrite data not yet read from the buffer.
//
ssize_t u_ringbuf_write( u_ringbuf_t *rb, void *buffer, size_t length)
{
	size_t bytes = 0;

	// Truncate length to the number of bytes available in the buffer
	size_t avail = RINGBUFFER_MAX - (rb->write_ptr-rb->read_ptr);

	if( length > avail )
		length = avail;

	while( length ) {
		size_t fixed_write = rb->write_ptr & RINGBUFFER_MODULO;
		size_t eb = RINGBUFFER_MAX - fixed_write ;

		if( eb > length )
			eb = length;

		memcpy( rb->buffer + fixed_write, buffer, eb );
		rb->write_ptr += eb;
		buffer += eb;
		bytes  += eb;
		length -= eb;
	}
	return (ssize_t)bytes;
}

#ifdef TEST_MAIN
#define READ_MAX 4096

int main(int ac __attribute__((unused)), char *av[] __attribute__((unused)) )
{
	fd_set         *in = alloca(sizeof(fd_set));
	fd_set         *out = alloca(sizeof(fd_set));
	u_ringbuf_t    *rb = alloca(sizeof(u_ringbuf_t));
	char           *buffer = alloca(READ_MAX);

	int             eof_on_input = 0;

	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	fcntl(1, F_SETFL, fcntl(1, F_GETFL) | O_NONBLOCK);

	u_ringbuf_init(rb);

	while (1) {
		FD_ZERO(in);
		FD_ZERO(out);

		if (eof_on_input && u_ringbuf_empty(rb))
			break;

		if (!(eof_on_input || u_ringbuf_full(rb)))
			FD_SET(0, in);

		if (!u_ringbuf_empty(rb))
			FD_SET(1, out);

		//struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

		int             rc = select(3, in, out, 0L, 0L);

		// write first - make space

		if (FD_ISSET(1, out)) {
			fprintf(stderr, "want write..\n");
			u_ringbuf_write_fd(rb, 1);
		}

		if (FD_ISSET(0, in)) {
			size_t          bytes = 0;
			rc = ioctl(0, FIONREAD, &bytes);
			fprintf(stderr, "ioctl returned %d (%ld bytes) (%s)\n", rc, bytes, strerror(errno));
			if (bytes > 0) {
				size_t          available_space = u_ringbuf_avail(rb);
				fprintf(stderr, "Have %ld bytes available\n", available_space);

				// truncate to available space
				if (bytes > available_space)
					bytes = available_space;

				// truncate again if available space is larger than local buffer
				if (bytes > READ_MAX)
					bytes = READ_MAX;

				if (bytes) {
					fprintf(stderr, "Attempting to read %ld bytes\n", bytes);
					ssize_t         read_bytes = read(0, buffer, bytes);
					if (read_bytes < 0) {
						fprintf(stderr, "Failed to read something... (%s)\n", strerror(errno));
					} else
						u_ringbuf_write(rb, buffer, (size_t) read_bytes);
				}
			} else {
				eof_on_input = 1;
				fprintf(stderr, "EOF ON INPUT..\n");
			}
		}

	}
}
#endif

