/***************************************************************************//**
*   @file   timer.c
*   @brief  Timer and GIC control module source.
*   @author Andrei Drimbarean (andrei.drimbarean@analog.com)
********************************************************************************
* Copyright 2019(c) Analog Devices, Inc.
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*  - Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  - Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in
*    the documentation and/or other materials provided with the
*    distribution.
*  - Neither the name of Analog Devices, Inc. nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*  - The use of this software may or may not infringe the patent rights
*    of one or more patent holders.  This license does not release you
*    from the requirement that you obtain separate licenses from these
*    patent holders to use this software.
*  - Use of the software either in source or binary form, must be run
*    on or directly connected to an Analog Devices Inc. component.
*
* THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT, MERCHANTABILITY
* AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/

#include <xparameters.h>
#include <stdlib.h>
#include "error.h"
#include "timer.h"
#include "timer_extra.h"
#ifdef XPAR_XSCUTIMER_NUM_INSTANCES
#include <xscutimer.h>
#endif
#ifdef XPAR_XTMRCTR_NUM_INSTANCES
#include <xtmrctr.h>
#endif

/******************************************************************************/
/********************** Macros and Constants Definitions **********************/
/******************************************************************************/

#if defined(_XPARAMETERS_PS_H_)
#define CORE_PRIVATE_TIMER_CLOCK (XPAR_CPU_CORTEXA9_CORE_CLOCK_FREQ_HZ / 2)
#elif defined(XTMRCTR_H)
#define MB_AXI_TIMER_CLOCK XPAR_AXI_TIMER_CLOCK_FREQ_HZ
#endif

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

/**
 * Initialize hardware timer and the handler structure associated with it.
 *
 * @param [out] desc - Pointer to the reference of the device handler.
 * @param [in] init_param - Initialization structure.
 *
 * @return 0 in case of success, negative error code otherwise
 */
int32_t timer_init(struct timer_desc **desc,
		   struct timer_init_param *init_param)
{
	int32_t ret = 0;
	uint32_t load;
	struct timer_desc *dev;
	struct xil_timer_desc *xil_timer_dev;
	struct xil_timer_init_param *xil_timer_extra_init;

	dev = (struct timer_desc *)calloc(1, sizeof *dev);
	if(!dev)
		return FAILURE;

	xil_timer_dev = (struct xil_timer_desc *)calloc(1, sizeof *xil_timer_dev);
	if(!xil_timer_dev) {
		free(dev);

		return FAILURE;
	}

	xil_timer_extra_init = init_param->extra;
	dev->extra = xil_timer_dev;
	dev->freq_hz = init_param->freq_hz;
	dev->auto_reload = init_param->auto_reload;
	xil_timer_dev->device_id = xil_timer_extra_init->device_id;
	xil_timer_dev->type = xil_timer_extra_init->type;

	switch(xil_timer_dev->type) {
	case TIMER_PL:
		;
#ifdef XTMRCTR_H
		uint8_t tmr_options = 0;

		load = MB_AXI_TIMER_CLOCK / dev->freq_hz;
		xil_timer_dev->active_tmr = xil_timer_extra_init->active_tmr;

		xil_timer_dev->instance = (XTmrCtr *)calloc(1, sizeof(XTmrCtr));

		ret = XTmrCtr_Initialize(xil_timer_dev->instance,
					 xil_timer_dev->device_id);
		if(ret != 0)
			goto pl_error;

		ret = XTmrCtr_SelfTest(xil_timer_dev->instance,
				       xil_timer_dev->device_id);
		if(ret != 0)
			goto pl_error;

		XTmrCtr_SetHandler(xil_timer_dev->instance,
				   xil_timer_extra_init->callback_routine, xil_timer_dev);
		tmr_options = XTC_DOWN_COUNT_OPTION | XTC_INT_MODE_OPTION;
		tmr_options |= dev->auto_reload ? XTC_AUTO_RELOAD_OPTION : 0;
		XTmrCtr_SetOptions(xil_timer_dev->instance, xil_timer_dev->active_tmr,
				   tmr_options);

		XTmrCtr_SetResetValue(xil_timer_dev->instance,
				      xil_timer_dev->active_tmr, load);

		break;
pl_error:
		free(xil_timer_dev->instance);
#endif
		goto error;
	case TIMER_PS:
#ifdef XSCUTIMER_H
		load = CORE_PRIVATE_TIMER_CLOCK / dev->freq_hz;

		xil_timer_dev->instance = (XScuTimer *)calloc(1, sizeof(XScuTimer));
		xil_timer_dev->config = XScuTimer_LookupConfig(xil_timer_dev->
					device_id);

		ret = XScuTimer_CfgInitialize(xil_timer_dev->instance,
					      xil_timer_dev->config,
					      ((XScuTimer_Config *)xil_timer_dev->config)->BaseAddr);
		if(ret != 0)
			goto ps_error;

		XScuTimer_LoadTimer((XScuTimer *)xil_timer_dev->instance, load);

		if(dev->auto_reload)
			XScuTimer_EnableAutoReload((XScuTimer *)xil_timer_dev->instance);

		XScuTimer_EnableInterrupt((XScuTimer *)xil_timer_dev->instance);

		break;
ps_error:
		free(xil_timer_dev->instance);
#endif
		goto error;
	default:
		goto error;
	}

	*desc = dev;

	return ret;
error:
	free(xil_timer_dev);
	free(dev);

	return ret;
}

/**
 * Free the memory allocated by timer_setup().
 *
 * timer_setup() function needs to be called first.
 *
 * @param [in] desc - Pointer to the device handler.
 *
 * @return 0 in case of success, negative error code otherwise
 */
int32_t timer_remove(struct timer_desc *desc)
{
	struct xil_timer_desc *xil_timer_dev;

	if(!desc)
		return FAILURE;

	xil_timer_dev = desc->extra;

	switch(xil_timer_dev->type) {
	case TIMER_PL:
#ifdef XTMRCTR_H
		XTmrCtr_Stop(xil_timer_dev->instance, xil_timer_dev->active_tmr);

		break;
#endif
	case TIMER_PS:
#ifdef XSCUTIMER_H
		XScuTimer_Stop(xil_timer_dev->instance);

		break;
#endif
	default:
		return FAILURE;
	}

	free(xil_timer_dev->instance);
	free(xil_timer_dev);
	free(desc);

	return SUCCESS;
}

/**
 * Start a timer.
 *
 * timer_setup() function needs to be called first.
 *
 * @param [in] desc - Pointer to the device handler.
 *
 * @return void
 */
void timer_start(struct timer_desc *desc)
{
	struct xil_timer_desc *xil_timer_dev = desc->extra;

	xil_timer_dev->timer_counter_intr = 0;
	switch(xil_timer_dev->type) {
	case TIMER_PL:
#ifdef XTMRCTR_H
		XTmrCtr_Start(xil_timer_dev->instance, xil_timer_dev->active_tmr);

		break;
#endif
	case TIMER_PS:
#ifdef XSCUTIMER_H
		XScuTimer_Start(xil_timer_dev->instance);

		break;
#endif
	default:
		break;
	}
}

/**
 * Stop a timer from counting.
 *
 * timer_setup() function needs to be called first.
 *
 * @param [in] desc - Pointer to the device handler.
 *
 * @return void
 */
void timer_stop(struct timer_desc *desc)
{
	struct xil_timer_desc *xil_timer_dev = desc->extra;

	switch(xil_timer_dev->type) {
	case TIMER_PL:
#ifdef XTMRCTR_H
		XTmrCtr_Stop(xil_timer_dev->instance, xil_timer_dev->active_tmr);

		break;
#endif
	case TIMER_PS:
#ifdef XSCUTIMER_H
		XScuTimer_Stop(xil_timer_dev->instance);

		break;
#endif
	default:
		break;
	}
}
