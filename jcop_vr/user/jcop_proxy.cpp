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

#include "jcop_simul.h"
#include "dbglog.h"
#include "shared_data.h"
#include "t1.h"

static char g_snd[JCOP_PROXY_BUFFER_SIZE];
static char g_rcv[JCOP_PROXY_BUFFER_SIZE];

int __cdecl main(int argc, char* argv[])
{
	JCOP_PROXY_SHARED_EVENTS events;

	// create event for sending data.
	events.hEventSnd = CreateEvent(NULL, FALSE, FALSE, "JCopVRSnd");
	if (events.hEventSnd == INVALID_HANDLE_VALUE) {
		dbg_log("CreateEvent failed! - status: 0x%08X", GetLastError());
		return -1;
	}

	// create event for receiving data.
	events.hEventRcv = CreateEvent(NULL, FALSE, FALSE, "JCopVRRcv");
	if (events.hEventRcv == INVALID_HANDLE_VALUE) {
		dbg_log("CreateEvent failed! - status: 0x%08X", GetLastError());
		return -1;
	}

	// read kernel-mode driver file.
	HANDLE hFile = CreateFile("\\\\.\\JCopVirtualReader",
	                          GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		dbg_log("CreateFile failed! - status: 0x%08X", GetLastError());
		dbg_log("driver not installed properly.");
		return -1;
	}

	// send IOCT_SET_EVENTS IO control code.
	DWORD dwReturn;
	BOOL bStatus = DeviceIoControl(
	                   hFile,					// Handle to device
	                   IOCTL_JCOP_PROXY_SET_EVENTS,		// IO Control code
	                   &events,				// Input Buffer to driver.
	                   sizeof(JCOP_PROXY_SHARED_EVENTS),	// Length of input buffer in bytes.
	                   NULL,					// Output Buffer from driver.
	                   0,						// Length of output buffer in bytes.
	                   &dwReturn,				// Bytes placed in buffer.
	                   NULL					// synchronous call
	               );
	if (!bStatus) {
		dbg_log("Ioctl failed! - status: 0x%08X", GetLastError());
		return -1;
	}

	while (true) {
		// wait for event.
		dbg_log("waiting for sending data event...");
		DWORD status = WaitForSingleObject(events.hEventSnd, INFINITE);
		if (status != WAIT_OBJECT_0) {
			switch (status) {
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
		bStatus = ReadFile(hFile, g_snd, sizeof(g_snd), &dwRead, NULL);
		if (!bStatus) {
			dbg_log("ReadFile failed! - status: 0x%08X", GetLastError());
			continue;
		}
		dbg_log("%d bytes read", dwRead);
		dbg_ba2s(g_snd, dwRead);
		if (dwRead > 0xFFFF) {
			dbg_log("dwRead > 0xFFFF");
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
				dbg_log("JCOP_SIMUL_powerUp end with code %d", dwReturn);
				if (status != JCOP_SIMUL_NO_ERROR) {
					dbg_log("JCOP_SIMUL_powerUp failed! - status: 0x%08X", GetLastError());
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
					dbg_log("JCOP_SIMUL_transmit failed! - status: 0x%08X", status);
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
					dbg_log("T1_processMsg failed! - status: 0x%08X", status);
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
		bStatus = WriteFile(hFile, g_rcv, rcvLen, &dwWritten, NULL);
		if (!bStatus) {
			dbg_log("WriteFile failed! - status: 0x%08X", GetLastError());
			continue;
		}
		dbg_log("%d bytes written", dwWritten);

		// set event receiving data completed.
		bStatus = SetEvent(events.hEventRcv);
		if (!bStatus) {
			dbg_log("SetEvent failed! - status: 0x%08X", GetLastError());
			continue;
		}
		dbg_log("hEventRcv set.");
	}

	if (events.hEventRcv != INVALID_HANDLE_VALUE) {
		dbg_log("CloseHandle(events.hEventRcv)");
		CloseHandle(events.hEventRcv);
	}

	if (events.hEventSnd != INVALID_HANDLE_VALUE) {
		dbg_log("CloseHandle(events.hEventSnd)");
		CloseHandle(events.hEventSnd);
	}

	dbg_log("CloseHandle(hFile)");
	CloseHandle(hFile);
	dbg_log("CloseHandle(hFile): end");
	return 0;
}

