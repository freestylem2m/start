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

#ifndef __DRIVER_SIGNAL_H_
#define __DRIVER_SIGNAL_H_

#include "events.h"

typedef enum {
	SIGNAL_STATE_IDLE,
	SIGNAL_STATE_RUNNING,
	SIGNAL_STATE_STOPPING,
	SIGNAL_STATE_ERROR
} signal_state_t;

typedef enum {
	SIGNAL_NONE,
	SIGNAL_TERMINATING = 1,
} signal_flags_t;

typedef struct signal_config_s {
	signal_state_t state;
	signal_flags_t flags;
} signal_config_t;

extern int signal_init(context_t *);
extern int signal_shutdown(context_t *);
extern ssize_t signal_handler(context_t *, event_t event, driver_data_t *event_data);
#endif
