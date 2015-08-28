/******************************************************************************************************
*
* Freestyle Technology Pty Ltd
*
* Copyright (c) 2015 Freestyle Technology Pty Ltd
*
* Name:            hvc_util.c
*
* Description:     Utility functions for HVC management
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

#ifndef __HVC_UTIL_H__
#define __HVC_UTIL_H__

#ifdef mips
#include "libHVC.h"
#endif

int hvc_getTemperature(void)
{
#ifdef mips
	return hvcGetTemperature();
#else
	return -1;
#endif
}

#endif
