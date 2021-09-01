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

File    : SEGGER_SYSVIEW_COMM_IP_embOS_emNet.c
Purpose : SystemView Communication for IP Recorder with embOS and emNet.
Revision: $Rev: 15024 $

Additional Information
  emNet needs to be initialized by the application, ideally before calling SEGGER_SYSVIEW_X_StartComm.
  Example code:
    static OS_STACKPTR int          _IPStack[TASK_STACK_SIZE_IP_TASK/sizeof(int)];               // Stack of the IP_Task.
    static OS_TASK                  _IPTCB;                                                      // Task-Control-Block of the IP_Task.

    void MainTask(void) {
      //
      // Initialize emNet
      //
      IP_Init();
      OS_CREATETASK(&_IPTCB    , "IP_Task"  , IP_Task    , TASK_PRIO_IP_TASK   , _IPStack);      // Start the IP_Task.
      IP_Connect(IP_INFO_GetNumInterfaces() - 1);                                                // Connect the interface if necessary.
      //
      // Start SystemView communication
      //
      SEGGER_SYSVIEW_X_StartComm();

      ...
    }

*/
#include "SEGGER_SYSVIEW.h"
#include "SEGGER_SYSVIEW_Conf.h"
#include "SEGGER_RTT.h"

#include "RTOS.h"
#include "IP.h"
#include "IP_Int.h"

/*********************************************************************
*
*       Defines, OS
*
**********************************************************************
*/
#define SYSVIEW_COMM_TASK_STACK_SIZE   1024

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
#ifndef   SYSVIEW_COMM_TASK_PRIO
  #define SYSVIEW_COMM_TASK_PRIO        1         // Priority of the communication task
#endif

#ifndef   SYSVIEW_COMM_SEND_THRESHOLD
  #define SYSVIEW_COMM_SEND_THRESHOLD   512       // Minimum number of bytes to read and send from the RTT Buffer.
#endif

#ifndef   SYSVIEW_COMM_IDLE_DELAY
  #define SYSVIEW_COMM_IDLE_DELAY       100       // Delay if no data had to be sent.
#endif

#ifndef   SYSVIEW_COMM_POLL_INTERVAL
  #define SYSVIEW_COMM_POLL_INTERVAL    2         // Interval to check for buffer level.
#endif

#ifndef   SYSVIEW_COMM_SERVER_PORT
  #define SYSVIEW_COMM_SERVER_PORT      19111
#endif

#define _NO_NOTIFY

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define SYSVIEW_COMM_SERVER_HELLO_SIZE  4
#define SYSVIEW_COMM_TARGET_HELLO_SIZE  4

/*********************************************************************
*
*       Static data, OS
*
**********************************************************************
*/
static OS_STACKPTR int          _SysViewCommStack[SYSVIEW_COMM_TASK_STACK_SIZE/sizeof(int)];
static OS_TASK                  _SysViewCommTCB;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static int                      _ChannelID;
static const U8                 _abHelloMsg[SYSVIEW_COMM_TARGET_HELLO_SIZE] = { 'S', 'V', (SEGGER_SYSVIEW_VERSION / 10000), (SEGGER_SYSVIEW_VERSION / 1000) % 10 };  // "Hello" message expected by SysView: [ 'S', 'V', <PROTOCOL_MAJOR>, <PROTOCOL_MINOR> ]
static char                     _acBuf[SYSVIEW_COMM_SEND_THRESHOLD * 2];

#ifndef _NO_NOTIFY
static unsigned                 _NumBytesRecorded;
#endif

/*********************************************************************
*
*       Local functions, OS
*
**********************************************************************
*/
/*********************************************************************
*
*       _Delay
*
*  Function description
*    Delay for some time.
*
*/
static void _Delay(int Ticks) {
  OS_TASK_Delay(Ticks);
}

#ifdef _NO_NOTIFY
/*********************************************************************
*
*       _WaitPolling
*
*  Function description
*    Poll SystemView Buffer to reach threshold fill level.
*
*/
static void _WaitPolling(int Timeout) {
  int BytesInBuffer;
  do {
    BytesInBuffer = SEGGER_RTT_GetBytesInBuffer(_ChannelID);
    if (BytesInBuffer >= SYSVIEW_COMM_SEND_THRESHOLD) {
      break;
    }
    _Delay(SYSVIEW_COMM_POLL_INTERVAL);
    Timeout -= SYSVIEW_COMM_POLL_INTERVAL;
  } while (Timeout > 0);
}
#else
/*********************************************************************
*
*       _WaitNotify
*
*  Function description
*    Wait to be notified or the timeout runs out.
*
*/
static void _WaitNotify(int Timeout) {
  OS_TASKEVENT_GetTimed(1, Timeout);
}

/*********************************************************************
*
*       _Notify
*
*  Function description
*    Notify the communication task.
*
*/
static void _Notify(void) {
  //
  // There might be SystemView Events before the communication task is created.
  // Only notify task when it has been created
  //
  if (OS_TASK_IsTask(&_SysViewCommTCB)) {
    OS_TASKEVENT_Set(&_SysViewCommTCB, 1);
  }
}
#endif

/*********************************************************************
*
*       Local functions, IP
*
**********************************************************************
*/
/*********************************************************************
*
*       _WaitForIPInit
*
*  Function description
*    Wait for IP Stack to be initialized.
*
*/
static void _WaitForIPInit(void) {
  while (IP_Global.InitCompleted == 0) {
    _Delay(2);
  }
}

/*********************************************************************
*
*       _ListenAtTcpPort()
*
* Function description
*   Creates a socket, binds it to a port and sets the socket into
*   listening state.
*
* Parameter
*   Port      - Port which should be to wait for connections.
*
* Return value
*   O.K. : Socket handle.
*   Error: SOCKET_ERROR .
*/
static int _ListenAtTcpPort(U16 Port) {
  int hSock;
  int r;
  struct sockaddr_in Addr;

  //
  // Create socket
  //
  hSock = socket(AF_INET, SOCK_STREAM, 0);
  if (hSock != SOCKET_ERROR) {
    //
    // Bind it to the port
    //
    IP_MEMSET(&Addr, 0, sizeof(Addr));
    Addr.sin_family      = AF_INET;
    Addr.sin_port        = htons(Port);
    Addr.sin_addr.s_addr = INADDR_ANY;
    r = bind(hSock, (struct sockaddr*)&Addr, sizeof(Addr));
    //
    // Start listening on the socket.
    //
    if (r != 0) {
      hSock = SOCKET_ERROR;
    } else {
      r = listen(hSock, 1);
      if (r != 0) {
        hSock = SOCKET_ERROR;
      }
    }
  }
  return hSock;
}

/*********************************************************************
*
*       _SocketIsReadable
*
*  Function description
*    Check if data can be read from the connected socket.
*
*  Parameters
*    hSocket  Socket to check.
*
*  Return value
*    == 1  O.K., socket readable
*    == 0  O.K., socket not readable
*/
static int _SocketIsReadable(int hSocket, int TimeoutMs) {
  I32 tv = TimeoutMs;
  IP_fd_set rfds;
  IP_fd_set efds;
  int v;

  FD_ZERO(&rfds);             // Zero init file descriptor list
  FD_SET(hSocket, &rfds);     // Add socket to file descriptor list to be monitored by select()
  FD_ZERO(&efds);
  FD_SET(hSocket, &efds);

  v = select(&rfds, NULL, &efds, tv);   // > 0: in case of success, == 0: Timeout, < 0: Error
  return v;
}

/*********************************************************************
*
*       Local functions, SystemView Communication
*
**********************************************************************
*/
/*********************************************************************
*
*       _SysViewCommTask
*
*  Function description
*    Task that handles TCP/IP connection and communication 
*    with SystemView App.
*
*/
static void _SysViewCommTask(void) {
  int hSockListen;
  int hSockSV;
  int v;
  int r;
  int NumBytes;
  char acRecv[16];
  //
  _ChannelID = SEGGER_SYSVIEW_GetChannelID();
  hSockSV = SOCKET_ERROR;
  //
  // Wait for IP Stack to be initialized.
  //
  _WaitForIPInit();
  //
  // Try until we get a valid listening socket.
  //
  while (1) {
    hSockListen = _ListenAtTcpPort(SYSVIEW_COMM_SERVER_PORT);
    if (hSockListen == SOCKET_ERROR) {
      _Delay(100);
      continue;  // Error, try again.
    }
    break;
  }
  //
  // Wait for a connection and process the communication.
  //
  do {
    //
    // Check for a new connection.
    //
    hSockSV = accept(hSockListen, NULL, NULL);
    if (hSockSV == SOCKET_ERROR) {
      continue;               // Error, try again.
    }
    //
    // Successful connection? => Start systemview session
    // First, receive <Hello> message from SysView and send back own <Hello> message
    //
    r = recv(hSockSV, acRecv, SYSVIEW_COMM_SERVER_HELLO_SIZE, 0);
    if (r != SYSVIEW_COMM_SERVER_HELLO_SIZE) {      // Failed to receive Hello message from SystemView App
      closesocket(hSockSV);
      hSockSV = SOCKET_ERROR;
    }
    r = send(hSockSV, (const char*)_abHelloMsg, SYSVIEW_COMM_TARGET_HELLO_SIZE, 0);
    if (r != SYSVIEW_COMM_TARGET_HELLO_SIZE) {      // Failed to send Hello message to SystemView App
      closesocket(hSockSV);
      hSockSV = SOCKET_ERROR;
    }
    //
    // After a succesful connection, poll RTT buffer for data
    //
    while (hSockSV != SOCKET_ERROR) {
#ifdef _NO_NOTIFY
      _WaitPolling(SYSVIEW_COMM_IDLE_DELAY);      // Wait until there is "enough" data in the buffer to be sent.
#else
      _WaitNotify(SYSVIEW_COMM_IDLE_DELAY);       // Wait for event to be signaled when there is "enough" data in the buffer to be sent.
      _NumBytesRecorded = 0;                      // Reset counter when we are going to send current RTT Buffer
#endif
      //
      // Check for data to be received from SystemView App
      //
      r = _SocketIsReadable(hSockSV, 0);
      if (r == 1) {                               // Data to read from SysView available?
        r = recv(hSockSV, acRecv, 1, 0);          // Receive <NumBytes> to read
        if (r != 1) {                             // Failed to receive data? => Connection lost
          break;
        }
        v = acRecv[0];
        r = recv(hSockSV, acRecv, v, 0);          // Receive all data
        if (r != v) {                             // Failed to receive data? => Connection lost
          break;
        }
        NumBytes = SEGGER_RTT_WriteDownBufferNoLock(_ChannelID, &acRecv[0], r);  // Write data into corresponding RTT buffer for application to read and handle accordingly
      }
      //
      // Read available RTT data into send bufer and send it.
      //
      NumBytes = SEGGER_RTT_ReadUpBufferNoLock(_ChannelID, &_acBuf[0], sizeof(_acBuf));
      if (NumBytes > 0) {
        //
        // Send data to SystemView App
        //
        r = send(hSockSV, _acBuf, NumBytes, 0);
        if (NumBytes != r) {                        // Failed to send data? => Connection lost
          break;
        }
      }
    }
    //
    // Processing done. close socket handle.
    //
    if (hSockSV != SOCKET_ERROR) {
      closesocket(hSockSV);
      hSockSV = SOCKET_ERROR;
    }
  } while (1);
}

/*********************************************************************
*
*       Global functions
*
**********************************************************************
*/

/*********************************************************************
*
*       SEGGER_SYSVIEW_X_StartComm()
*
*  Function description
*    Setup communication channel.
*/
void SEGGER_SYSVIEW_X_StartComm(void) {
  //
  // Initialize SystemView communication
  //
  OS_TASK_CREATE(&_SysViewCommTCB, "SysView Comm", SYSVIEW_COMM_TASK_PRIO, _SysViewCommTask, _SysViewCommStack);
}

#ifndef _NO_NOTIFY
/*********************************************************************
*
*       SEGGER_SYSVIEW_X_OnEventRecorded()
*
*  Function description
*    Macro callback to be called when an event has been stored in the
*    SystemView RTT Buffer.
*
*  Parameters
*    NumBytes:    Number of bytes stored in the buffer.
*/
void SEGGER_SYSVIEW_X_OnEventRecorded(unsigned NumBytes) {
  //
  // Notify SystemView Communication Task when the send threshold is reached.
  //
  _NumBytesRecorded += NumBytes;
  if (_NumBytesRecorded >= SYSVIEW_COMM_SEND_THRESHOLD) {
    _NumBytesRecorded = 0;
    _Notify();
  }
}
#endif

/*************************** End of file ****************************/
