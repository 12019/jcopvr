/*
 * $Id$
 */

/*
 * Copyright (c) 2008 Kenichi Kanai
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*!
 * \file jcop_proxy.cpp
 * \brief JCOP Proxy (user-mode application for JCOP Simulation Virtual Reader Driver) - Main Module.
 * \author Kenichi Kanai
 */
#include <windows.h>
#include <tchar.h>
#include <stdio.h>

#include "shared_data.h"
#include "jcop_simul.h"
#include "t1.h"
#include "dbglog.h"

static char g_snd[JCOP_PROXY_BUFFER_SIZE];
static char g_rcv[JCOP_PROXY_BUFFER_SIZE];
static JCOP_PROXY_SHARED_EVENTS g_events;
static HANDLE g_hFile;
static HANDLE g_eventStop = NULL;

static void err_msg(char const *const pFmt, ...)
{
	va_list argList;
	va_start(argList, pFmt);
	TCHAR sz[1024];
	_vstprintf(sz, pFmt, argList);
	va_end(argList);

	MessageBox(
	    NULL,
	    sz,
	    _T("jcop_proxy"),
	    (MB_OK | MB_ICONWARNING)
	);
}

static void finalize_driver(void)
{
	if (g_events.hEventRcv != NULL) {
		dbg_log("CloseHandle(g_events.hEventRcv)");
		CloseHandle(g_events.hEventRcv);
		g_events.hEventRcv = NULL;
	}

	if (g_events.hEventSnd != NULL) {
		dbg_log("CloseHandle(g_events.hEventSnd)");
		CloseHandle(g_events.hEventSnd);
		g_events.hEventRcv = NULL;
	}

	dbg_log("CloseHandle(g_hFile)");
	CloseHandle(g_hFile);
	dbg_log("CloseHandle(g_hFile): end");
}

static void finalize(void)
{
	// set event receiving data completed.
	if (g_eventStop != NULL) {
		SetEvent(g_eventStop);
		CloseHandle(g_eventStop);
		g_eventStop = NULL;
	}

	dbg_log("JCOP_SIMUL_close()");
	JCOP_SIMUL_close();

	finalize_driver();
}

static int loop(void)
{

	while (true) {
		// wait for event.
		dbg_log("waiting for sending data event...");
		HANDLE handles[2];
		handles[0] = g_events.hEventSnd;	// WAIT_OBJECT_0
		handles[1] = g_eventStop;	// WAIT_OBJECT_0 + 1
		DWORD status = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
		if (status != WAIT_OBJECT_0) {
			switch (status) {
				case WAIT_OBJECT_0 + 1 :
					// Stoping thread event is set.
					dbg_log("WAIT_OBJECT_0 + 1");
					return 0;
				case WAIT_ABANDONED :
					dbg_log("WAIT_ABANDONED");
					break;
				case WAIT_TIMEOUT :
					dbg_log("WAIT_TIMEOUT");
					break;
				default:
					dbg_log("WAIT_XXXXX");
					break;
			}
			continue;
		}
		dbg_log("hEventSnd signalled.");

		// read sending data from kernel-mode driver.
		memset(g_snd, 0, sizeof(g_snd));
		unsigned long dwRead = 0;
		BOOL bStatus = ReadFile(g_hFile, g_snd, sizeof(g_snd), &dwRead, NULL);
		if (!bStatus) {
			err_msg("ReadFile failed! - status: 0x%08X", GetLastError());
			continue;
		}
		dbg_log("%d bytes read", dwRead);
		dbg_ba2s(g_snd, dwRead);
		if (dwRead > 0xFFFF) {
			err_msg("dwRead > 0xFFFF");
			continue;
		}

		// check MTY and dispatch process.
		int mty = g_snd[0];
		unsigned short rcvLen;
		switch (mty) {
			case 0x00 :
				dbg_log("MTY=0x00: Wait for card");
				memset(g_rcv, 0, sizeof(g_rcv));
				rcvLen = sizeof(g_rcv);	// expected length
				status = JCOP_SIMUL_powerUp(g_rcv, &rcvLen);
				dbg_log("JCOP_SIMUL_powerUp end with code %d", status);
				if (status != JCOP_SIMUL_NO_ERROR) {
					err_msg("JCOP_SIMUL_powerUp failed! - status: 0x%08X", GetLastError());
					continue;
				}
				// reset Card sequence No.
				T1_resetSeq();
				break;
			case 0x01 :
				dbg_log("MTY=0x01: T=0 Transmit APDU");
				memset(g_rcv, 0, sizeof(g_rcv));
				rcvLen = sizeof(g_rcv);	// expected length
				status = JCOP_SIMUL_transmit(g_snd, (unsigned short)dwRead, g_rcv, &rcvLen);
				dbg_log("JCOP_SIMUL_transmit end with code %d", status);
				if (status != JCOP_SIMUL_NO_ERROR) {
					err_msg("JCOP_SIMUL_transmit failed! - status: 0x%08X", status);
					continue;
				}
				break;
			case 0x11 :
				// This is the original MTY used only for this proxy application.
				dbg_log("MTY=0x11: T=1 Message");
				memset(g_rcv, 0, sizeof(g_rcv));
				rcvLen = sizeof(g_rcv);	// expected length
				status = T1_processMsg(g_snd, (unsigned short)dwRead, g_rcv, &rcvLen);
				dbg_log("T1_processMsg end with code %d", status);
				if (status != 0) {
					err_msg("T1_processMsg failed! - status: 0x%08X", status);
					continue;
				}
				break;
			case 0x7F :
				// This is the original MTY used only for this proxy application.
				dbg_log("MTY=0x7F: Close socket");
				JCOP_SIMUL_close();
				rcvLen = (unsigned short)dwRead;
				memcpy(g_rcv, g_snd, rcvLen);
				break;
			default:
				dbg_log("MTY UNKNOWN");
				continue;
		}

		// write received data to kernel-mode driver.
		dbg_ba2s(g_rcv, rcvLen);
		DWORD dwWritten = 0;
		bStatus = WriteFile(g_hFile, g_rcv, rcvLen, &dwWritten, NULL);
		if (!bStatus) {
			err_msg("WriteFile failed! - status: 0x%08X", GetLastError());
			continue;
		}
		dbg_log("%d bytes written", dwWritten);

		// set event receiving data completed.
		bStatus = SetEvent(g_events.hEventRcv);
		if (!bStatus) {
			err_msg("SetEvent failed! - status: 0x%08X", GetLastError());
			continue;
		}
		dbg_log("hEventRcv set.");
	}

	return 0;
}

static int initialize_jcop(void)
{
	memset(g_rcv, 0, sizeof(g_rcv));
	unsigned short rcvLen = sizeof(g_rcv);	// expected length
	int status = JCOP_SIMUL_powerUp(g_rcv, &rcvLen);
	dbg_log("JCOP_SIMUL_powerUp end with code %d", status);
	if (status != JCOP_SIMUL_NO_ERROR) {
		JCOP_SIMUL_close();
		dbg_log("JCOP_SIMUL_powerUp failed! - status: 0x%08X", status);
		return -1;
	}

	return 0;
}

static int initialize_driver(void)
{
	// create event for sending data.
	g_events.hEventSnd = CreateEvent(NULL, FALSE, FALSE, "JCopVRSnd");
	if (g_events.hEventSnd == NULL) {
		dbg_log("CreateEvent failed! - status: 0x%08X", GetLastError());
		return -1;
	}

	// create event for receiving data.
	g_events.hEventRcv = CreateEvent(NULL, FALSE, FALSE, "JCopVRRcv");
	if (g_events.hEventRcv == NULL) {
		dbg_log("CreateEvent failed! - status: 0x%08X", GetLastError());
		finalize_driver();
		return -1;
	}

	// read kernel-mode driver file.
	g_hFile = CreateFile("\\\\.\\JCopVirtualReader",
	                     GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (g_hFile == INVALID_HANDLE_VALUE) {
		dbg_log("CreateFile failed! - status: 0x%08X", GetLastError());
		finalize_driver();
		return -1;
	}

	// send IOCT_SET_EVENTS IO control code.
	DWORD dwReturn;
	BOOL bStatus = DeviceIoControl(
	                   g_hFile,					// Handle to device
	                   IOCTL_JCOP_PROXY_SET_EVENTS,		// IO Control code
	                   &g_events,				// Input Buffer to driver.
	                   sizeof(JCOP_PROXY_SHARED_EVENTS),	// Length of input buffer in bytes.
	                   NULL,					// Output Buffer from driver.
	                   0,						// Length of output buffer in bytes.
	                   &dwReturn,				// Bytes placed in buffer.
	                   NULL					// synchronous call
	               );
	if (!bStatus) {
		dbg_log("Ioctl failed! - status: 0x%08X", GetLastError());
		finalize_driver();
		return -1;
	}

	return 0;
}

static int initialize(void)
{
	g_eventStop = CreateEvent(NULL, FALSE, FALSE, "JCopProxyStopThread");
	if (g_eventStop == INVALID_HANDLE_VALUE) {
		dbg_log("CreateEvent failed! - status: 0x%08X", GetLastError());
		err_msg("CreateEvent failed!");
		return -1;
	}

	int status;

	// Driver File
	status = initialize_driver();
	if (status != 0) {
		err_msg("the driver file (jcop_vr.sys) is not installed properly.");
		return -1;
	}

	// JCOP Simulator
	status = initialize_jcop();
	if (status != 0) {
		err_msg("JCOP Simulator seems not to be invoked!\ninvoke the JCOP Simulator and \"/close\" the JCOP Shell.");
		return -1;
	}

	return 0;
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR    lpCmdLine,
                       int       nCmdShow)
{

	if (_tcscmp(lpCmdLine, _T("start")) == 0) {

		HANDLE ev = OpenEvent(EVENT_MODIFY_STATE, FALSE, "JCopProxyStopThread");
		if (ev != NULL) {
			err_msg("jcop_proxy is already started!");
			return -1;
		}
		int status = initialize();
		if (status != 0) {
			return status;
		}
		MessageBox(
		    NULL,
		    _T("jcop_proxy is successfully invoked.\ndon't forget to restart 'Smart Card' service."),
		    _T("jcop_proxy"),
		    (MB_OK | MB_ICONINFORMATION)
		);
		status = loop();
		finalize();
		if (status != 0) {
			err_msg("loop() failed! - status: 0x%08X", status);
			return status;
		}
		MessageBox(
		    NULL,
		    _T("jcop_proxy is successfully stopped."),
		    _T("jcop_proxy"),
		    (MB_OK | MB_ICONINFORMATION)
		);
		return 0;

	} else if (_tcscmp(lpCmdLine, _T("stop")) == 0) {

		HANDLE ev = OpenEvent(EVENT_MODIFY_STATE, FALSE, "JCopProxyStopThread");
		if (ev == NULL) {
			err_msg("jcop_proxy is already stopped!");
			return -1;
		}
		SetEvent(ev);
		return 0;

	} else {

		err_msg("usage: jcop_proxy <start|stop>");
		return -1;
	
	}

	return 0;
}
