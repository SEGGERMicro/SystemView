/*********************************************************************
*                    SEGGER Microcontroller GmbH                     *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*            (c) 1995 - 2021 SEGGER Microcontroller GmbH             *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************
*                                                                    *
*       SEGGER SystemView * Real-time application analysis           *
*                                                                    *
**********************************************************************
*                                                                    *
* All rights reserved.                                               *
*                                                                    *
* SEGGER strongly recommends to not make any changes                 *
* to or modify the source code of this software in order to stay     *
* compatible with the SystemView and RTT protocol, and J-Link.       *
*                                                                    *
* Redistribution and use in source and binary forms, with or         *
* without modification, are permitted provided that the following    *
* condition is met:                                                  *
*                                                                    *
* o Redistributions of source code must retain the above copyright   *
*   notice, this condition and the following disclaimer.             *
*                                                                    *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
* DISCLAIMED. IN NO EVENT SHALL SEGGER Microcontroller BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR           *
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  *
* OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;    *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF      *
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT          *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE  *
* USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH   *
* DAMAGE.                                                            *
*                                                                    *
**********************************************************************
*                                                                    *
*       SystemView version: 3.30                                    *
*                                                                    *
**********************************************************************
-------------------------- END-OF-HEADER -----------------------------
File    : SEGGER_SYSVIEW_Config_embOS_iMX6.c
Purpose : Sample setup configuration of SystemView with embOS
          on NXP i.MX6 devices.

Additional information:
  SEGGER_SYSVIEW_TickCnt has to be declared in the module which handles
  the system tick and must be incremented in the SysTick_Handler.

  SEGGER_SYSVIEW_InterruptId has to be declared in the module which handles
  interrupts and must be set to the current interrupt Id as soon as an
  interrupt occurred.
*/

#include "RTOS.h"
#include "SEGGER_SYSVIEW.h"
#include "SEGGER_SYSVIEW_Conf.h"
#include "SEGGER_SYSVIEW_embOS.h"

//
// SEGGER_SYSVIEW_TickCnt has to be defined in the module which
// handles the SysTick and must be incremented in the System Tick
// handler before any SYSVIEW event is generated.
//
// Example in embOS RTOSInit.c:
//
// extern unsigned int SEGGER_SYSVIEW_TickCnt;
// void SysTick_Handler(void) {
// #if (OS_SUPPORT_PROFILE != 0)
//   SEGGER_SYSVIEW_TickCnt++;  // Increment SEGGER_SYSVIEW_TickCnt before calling OS_INT_EnterNestable().
// #endif
//   OS_TICK_Handle();
// }
//
unsigned int SEGGER_SYSVIEW_TickCnt;

//
// SEGGER_SYSVIEW_InterruptId has to be set in the IRQ handler
// to identify the active interrupt.
//
unsigned int SEGGER_SYSVIEW_InterruptId;

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifndef   OS_FSYS
  #define OS_FSYS                  (792000000u)
#endif

#ifndef   OS_PCLK_TIMER
  #define OS_PCLK_TIMER            (OS_FSYS / 2)
#endif

#ifndef   OS_TICK_FREQ
  #define OS_TICK_FREQ             (1000)
#endif

#ifndef   OS_TIMER_RELOAD
  #define OS_TIMER_RELOAD          (OS_PCLK_TIMER / OS_TICK_FREQ)
#endif

// The application name to be displayed in SystemViewer
#ifndef   SYSVIEW_APP_NAME
  #define SYSVIEW_APP_NAME        "embOS start project"
#endif

// The target device name
#ifndef   SYSVIEW_DEVICE_NAME
  #define SYSVIEW_DEVICE_NAME      "i.MX6"
#endif

// Frequency of the timestamp. Must match SEGGER_SYSVIEW_Conf.h
#ifndef   SYSVIEW_TIMESTAMP_FREQ
  #define SYSVIEW_TIMESTAMP_FREQ   (OS_PCLK_TIMER)
#endif

// System Frequency. SystemcoreClock is used in most CMSIS compatible projects.
#ifndef   SYSVIEW_CPU_FREQ
  #define SYSVIEW_CPU_FREQ         (OS_FSYS)
#endif

// The lowest RAM address used for IDs (pointers)
#ifndef   SYSVIEW_RAM_BASE
  #define SYSVIEW_RAM_BASE         (0x10000000)
#endif

#ifndef   SYSVIEW_SYSDESC0
  #define SYSVIEW_SYSDESC0         "I#29=OS_ISR_Tick"
#endif

// Define as 1 to immediately start recording after initialization to catch system initialization.
#ifndef   SYSVIEW_START_ON_INIT
  #define SYSVIEW_START_ON_INIT   0
#endif

//#ifndef   SYSVIEW_SYSDESC1
//  #define SYSVIEW_SYSDESC1      ""
//#endif

//#ifndef   SYSVIEW_SYSDESC2
//  #define SYSVIEW_SYSDESC2      ""
//#endif

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
//
// Core private timer
//
#define ARM_MP_BASE_ADDR             (0x00A00000u)
#define CORE_PRIV_TIMER_BASE_ADDR    (ARM_MP_BASE_ADDR + 0x0600u)
#define CORE_PRIV_TIMER_COUNT        (*(volatile unsigned int*)(CORE_PRIV_TIMER_BASE_ADDR + 0x04u))
#define CORE_PRIV_TIMER_INT_ID       (29u)
#define TIMER_INTERRUPT_PENDING()    (OS_GIC_IsPending(CORE_PRIV_TIMER_INT_ID))

/*********************************************************************
*
*       _cbSendSystemDesc()
*
*  Function description
*    Sends SystemView description strings.
*/
static void _cbSendSystemDesc(void) {
  SEGGER_SYSVIEW_SendSysDesc("N=" SYSVIEW_APP_NAME ",O=embOS,D=" SYSVIEW_DEVICE_NAME );
#ifdef SYSVIEW_SYSDESC0
  SEGGER_SYSVIEW_SendSysDesc(SYSVIEW_SYSDESC0);
#endif
#ifdef SYSVIEW_SYSDESC1
  SEGGER_SYSVIEW_SendSysDesc(SYSVIEW_SYSDESC1);
#endif
#ifdef SYSVIEW_SYSDESC2
  SEGGER_SYSVIEW_SendSysDesc(SYSVIEW_SYSDESC2);
#endif
}

/*********************************************************************
*
*       Global functions
*
**********************************************************************
*/
void SEGGER_SYSVIEW_Conf(void) {
  SEGGER_SYSVIEW_Init(SYSVIEW_TIMESTAMP_FREQ, SYSVIEW_CPU_FREQ,
                      &SYSVIEW_X_OS_TraceAPI, _cbSendSystemDesc);
  SEGGER_SYSVIEW_SetRAMBase(SYSVIEW_RAM_BASE);
  OS_TRACE_SetAPI(&embOS_TraceAPI_SYSVIEW);  // Configure embOS to use SYSVIEW.
#if SYSVIEW_START_ON_INIT
  SEGGER_SYSVIEW_Start();                    // Start recording to catch system initialization.
#endif
}

/*********************************************************************
*
*       SEGGER_SYSVIEW_X_GetTimestamp()
*
* Function description
*   Returns the current timestamp in cycles using the system tick
*   count and the SysTick counter.
*   All parameters of the SysTick have to be known and are set via
*   configuration defines on top of the file.
*
* Return value
*   The current timestamp.
*
* Additional information
*   SEGGER_SYSVIEW_X_GetTimestamp is always called when interrupts are
*   disabled. Therefore locking here is not required.
*/
U32 SEGGER_SYSVIEW_X_GetTimestamp(void) {
  U32 TickCount;
  U32 Cycles;

  //
  // Get the cycles of the current system tick.
  // Sample timer is down-counting, subtract the current value from the number of cycles per tick.
  //
  Cycles = OS_TIMER_RELOAD - CORE_PRIV_TIMER_COUNT;
  //
  // Get the system tick count.
  //
  TickCount = SEGGER_SYSVIEW_TickCnt;
  //
  // If a SysTick interrupt is pending, re-read timer and adjust result
  //
  if (TIMER_INTERRUPT_PENDING() != 0) {
    TickCount++;
    Cycles = OS_TIMER_RELOAD - CORE_PRIV_TIMER_COUNT;
  }
  Cycles += TickCount * OS_TIMER_RELOAD;
  return Cycles;
}

/*********************************************************************
*
*       SEGGER_SYSVIEW_X_GetInterruptId()
*/
U32 SEGGER_SYSVIEW_X_GetInterruptId(void) {
  return SEGGER_SYSVIEW_InterruptId;
}

/*************************** End of file ****************************/
