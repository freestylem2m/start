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


