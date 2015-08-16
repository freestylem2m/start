/******************************************************************************************************
*
* Freestyle Technology Pty Ltd
*
* Copyright (c) 2015 Freestyle Technology Pty Ltd
*
* Name:            ringbuf.h
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

#include <sys/types.h>
#include <unistd.h>

// ***************************************************************************************************
// Warning - Ensure RINGBUFFER_MAX is a power of 2, "RINGBUFFER_MAX-1" is used as a bitmask
// ***************************************************************************************************
#define RINGBUFFER_MAX    ((size_t)4096)
#define RINGBUFFER_MODULO ((size_t)((RINGBUFFER_MAX-1)))

#define RINGBUFFER_OVERFLOW 16384

typedef struct u_ringbuf_s {
	size_t  write_ptr;
	size_t  read_ptr;
	char    buffer[RINGBUFFER_MAX];
} u_ringbuf_t ;

//
// Various ringbuffer related functions.
//

extern void u_ringbuf_init( u_ringbuf_t *rb );
extern int u_ringbuf_empty(u_ringbuf_t *rb);
extern int u_ringbuf_full(u_ringbuf_t *rb);
extern size_t u_ringbuf_avail( u_ringbuf_t *rb );
extern ssize_t u_ringbuf_write_fd( u_ringbuf_t *rb, int fd);
extern ssize_t u_ringbuf_read( u_ringbuf_t *rb, char *buffer, size_t length);
extern ssize_t u_ringbuf_write( u_ringbuf_t *rb, char *buffer, size_t length);
