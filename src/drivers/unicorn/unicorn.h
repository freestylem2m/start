/******************************************************************************************************
*
* Freestyle Technology Pty Ltd
*
* Copyright (c) 2015 Freestyle Technology Pty Ltd
*
* Name:            modemdrv.c
*
* Description:     A driver program managing the HVC-50x 3G/LTE modem WAN interface to the carrier network.
*
*                  This program is responsible for initialising the modem, establishing a data connection
*                  to the network, piping data over the connection between the HVC and the network, and
*                  connection disestablishment when required.
*
*                  The driver provides separate data and control channels to the HVC. The control channel
*                  enables the HVC to control the operation of the modem and determine the connection status.
*
*                  The driver is intended to be invoked by the WAN Interface Management Daemon process.
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


#ifndef __DRIVER_UNICORN_H_
#define __DRIVER_UNICORN_H_

#include <stdint.h>
#include "ringbuf.h"
#include "driver.h"

#define FD_READ 0
#define FD_WRITE 1

#define KEEPALIVE_INCLUDES_OUTGOING

// ** Host Command Interface for Modem management software

#define SIZE_BUFF_FRAME         1024
#define BUFF_SIZE_FRAMEBUFF 1024
#define MAGIC_NUMBER  0xFEAB1e00
// Host interface commands.
typedef enum cmdHost_e
{
    CMD_NONE = 0,
    CMD_READY,
    CMD_SHUTDOWN,
    CMD_CONNECT,
    CMD_DISCONNECT,
    CMD_KEEPALIVE,
    CMD_STATE,
    CMD_CONFIGURE,
    CMD_DATA,
    CMD_DELAY,
	CMD_ABORT,

} cmdHost_t;

// Host interface states - Reported in command responses
// and on change of state.
typedef enum cmdState_e
{
    CMD_ST_OFFLINE = 0,
    CMD_ST_ERROR,
    CMD_ST_INPROGRESS,
    CMD_ST_ONLINE,
    CMD_ST_UNKNOWN = 0xFFFF

} cmdState_t;

typedef struct frmHdr_s
{
    uint32_t    magicNo;
    uint16_t    cmd;
    uint16_t    length;
    uint16_t    state;

} __attribute__((packed)) frmHdr_t;

// If modem driver takes more than this many seconds, kill it.
#define UNICORN_PROCESS_TERMINATION_TIMEOUT	10*1000
#define UNICORN_CONNECT_TIMEOUT				120*1000
#define UNICORN_RESTART_DELAY				120*1000
#define UNICORN_CALLBARRING_DELAY			12*60*1000
#define UNICORN_SHADOWZONE_DELAY			120*1000
#define UNICORN_KEEPALIVE_TIMEOUT			120*1000
#define UNICORN_KEEPALIVE_RETRY_MAX         2

// After reading a header, if the data frame takes longer than this many
// milliseconds, reset state and clear buffer.
#define FRAME_TIMEOUT						1000

// ** Driver State

typedef enum {

	UNICORN_STATE_IDLE,
	UNICORN_STATE_RUNNING,
	UNICORN_STATE_STOPPING,
	UNICORN_STATE_ERROR,

} unicorn_state_t;

typedef enum {

	// These are states sent to the parent.
	UNICORN_MODE_UNKNOWN,
	UNICORN_MODE_OFFLINE,
	UNICORN_MODE_ONLINE,

} unicorn_mode_t;

typedef enum {

	UNICORN_NONE = 0,
	UNICORN_ONLINE = 1,					// Set when modem driver reports online status
	UNICORN_EXPECTING_DATA = 2,
	UNICORN_CLOSING_CONNECTION = 4,
	UNICORD_UNEXPECTED_EXIT = 8,
	UNICORN_WAITING_FOR_CONNECT = 16,
	UNICORN_RESTARTING = 32,
	UNICORN_RECONNECTING = 64,
	UNICORN_DISABLE = 128,
	UNICORN_TERMINATING = 256,
	UNICORN_FIRST_START = 512,
	UNICORN_LAGGED = 1024,

} unicorn_flags_t;

typedef enum {

	UNICORN_RESULT_UNKNOWN,
	UNICORN_RESULT_OK,
	UNICORN_RESULT_CPIN_ERR,
	UNICORN_RESULT_NO_SERVICE,
	UNICORN_RESULT_CALL_BARRED,
	UNICORN_RESULT_DIAL_ERR,
	UNICORN_RESULT_CD_LOST,
	UNICORN_RESULT_RST_ERROR,
	UNICORN_RESULT_MDM_ERROR,

} unicorn_result_t;

typedef struct unicorn_config_t {

	unicorn_state_t state;
	unicorn_flags_t flags;
	unicorn_result_t result;

	context_t *modem;
	u_ringbuf_t input;
	time_t last_message;
	time_t pending_action_timeout;
	time_t retry_time;
	time_t retry_count;

	const char *driver;
	const char *pid_file;

	frmHdr_t  msgHdr;
	size_t   data_length;
	uint16_t driver_state;

    // Find these services..
    context_t *logger;

} unicorn_config_t;

extern int unicorn_init(context_t *);
extern int unicorn_shutdown(context_t *);
extern ssize_t unicorn_handler(context_t *, event_t event, driver_data_t *event_data);
#endif

