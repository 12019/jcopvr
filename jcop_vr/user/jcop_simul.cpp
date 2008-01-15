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
 * \file jcop_simul.cpp
 * \brief Source file that contains the functions which communicate with JCOP Simulator.
 * \author Kenichi Kanai
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <winsock.h>

#ifdef __cplusplus
}
#endif

#include "jcop_simul.h"
#include "dbglog.h"
#include "shared_data.h"

#define JCOP_PORT 8050
#define JCOP_HOST "127.0.0.1"
#define JCOP_BUF_SIZE JCOP_PROXY_BUFFER_SIZE
#define MAX_ATR_SIZE JCOP_PROXY_MAX_ATR_SIZE

static SOCKET g_socket = INVALID_SOCKET;
static char g_rcv[JCOP_BUF_SIZE];

/*!
 * \brief Close socket function.<br>
 */
static void close_socket()
{
	closesocket(g_socket);
	g_socket = INVALID_SOCKET;
	WSACleanup();
}

/*!
 * \brief This function opens and connects to JCOP simulation server.<br>
 *
 * \retval JCOP_SIMUL_NO_ERROR
 * \retval JCOP_SIMUL_ERROR_INITIALIZE
 */
 static int open_socket() {
	WSADATA wsaData;
	int status;
	status = WSAStartup(MAKEWORD(2, 0), &wsaData);
	if (status != 0) {
		dbg_log("WSAStartup failed");
		return JCOP_SIMUL_ERROR_INITIALIZE;
	}

	g_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (g_socket == INVALID_SOCKET) {
		dbg_log("socket : %d", WSAGetLastError());
		close_socket();
		return JCOP_SIMUL_ERROR_INITIALIZE;
	}

	sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(JCOP_PORT);
	server.sin_addr.S_un.S_addr = inet_addr(JCOP_HOST);

	// connect to JCOP simulator.
	status = connect(g_socket, (sockaddr *)&server, sizeof(server));
	if (status != 0) {
		dbg_log("connect : %d", WSAGetLastError());
		close_socket();
		return JCOP_SIMUL_ERROR_INITIALIZE;
	}

	return JCOP_SIMUL_NO_ERROR;
}

/*!
 * \brief Message exchange function communicate with JCOP simulation server.<br>
 * <br>
 * \param [in] pSnd A pointer to first byte of message.
 * \param [in] iSndLen length of message.
 * \param [out] pRcv A pointer to buffer to receive.
 * \param [in][out] pRcvLen [in]length of pRcv. caller's expected Max length of 
		receiving data. [out]actual lengh of received data.
 * \param [in] pDueTime A pointer duration to time out. if it is NULL, 
		the routine waits indefinitely.
 *
 * \retval JCOP_SIMUL_NO_ERROR
 * \retval JCOP_SIMUL_ERROR_TIMEOUT
 * \retval JCOP_SIMUL_ERROR_OTHER
 */
static int send_receive(
	char const *const pSnd, 
	unsigned short const iSndLen, 
	char *const pRcv, 
	unsigned short *const pRcvLen,
	timeval *pDueTime
)
{
	// send data.
	send(g_socket, pSnd, iSndLen, 0);

	// set file descriptor.
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(g_socket, &fds);

	int n = select(0, &fds, NULL, NULL, pDueTime);
	if (n == 0) {
		dbg_log("timeout");
		close_socket();
		return JCOP_SIMUL_ERROR_TIMEOUT;
	}
	// check if fd is set.
	if (!FD_ISSET(g_socket, &fds)) {
		dbg_log("fd is not set");
		close_socket();
		return JCOP_SIMUL_ERROR_OTHER;
	}

	// receive data.
	int expectedLen = *pRcvLen;
	memset(pRcv, 0, expectedLen);
	n = recv(g_socket, pRcv, expectedLen, 0);
	if (n < 0) {
		dbg_log("recv failed!: 0x%08X", WSAGetLastError());
		close_socket();
		return JCOP_SIMUL_ERROR_OTHER;
	}
	dbg_log("%d bytes Received.", n);
	dbg_ba2s(pRcv, n);

	*pRcvLen = (unsigned short)n;
	return JCOP_SIMUL_NO_ERROR;
}

/*!
 * \brief Function resets a smart card and return ATR.<br>
 * <br>
 * \param [out] pAtr A pointer to the ATR.
 * \param [out] pAtr A pointer to the ATR.
 *
 * \retval JCOP_SIMUL_NO_ERROR
 * \retval JCOP_SIMUL_ERROR_INITIALIZE
 */
int JCOP_SIMUL_powerUp(char *const pAtr, unsigned short *const pAtrLen)
{

	if (g_socket != INVALID_SOCKET) {
		close_socket();
	}

	int status = open_socket();
	if (status != 0) {
		return JCOP_SIMUL_ERROR_INITIALIZE;
	}

	char pSnd[8];
	pSnd[0] = 0x00;	// MTY 0x00(Wait for card)
	pSnd[1] = 0x21;	// NAD
	pSnd[2] = 0x00;	// LNH High byte of payload length
	pSnd[3] = 0x04;	// LNL Low byte of payload length
	pSnd[4] = 0x00;	// PY0 First byte of payload (interpretation depends on message type)
	pSnd[5] = 0x00;
	pSnd[6] = 0x00;
	pSnd[7] = 0x00;
	dbg_ba2s(pSnd, 8);

	// set duration to time out.
	timeval tv;
	tv.tv_sec = 0;	// 0sec.
	tv.tv_usec = 0;

	status = send_receive(pSnd, sizeof(pSnd), g_rcv, pAtrLen, &tv);
	dbg_log("*pAtrLen: %d", *pAtrLen);
	dbg_ba2s(g_rcv, *pAtrLen);
	if (status != 0) {
		dbg_log("send_receive failed! : 0x%X", status);
		close_socket();
		return status;
	}

	// first 4 byte of received data is header.
	// 00 00 00 0F 3B E6 00 FF 81 31 FE 45 4A 43 4F 50 32 30 06
	*pAtrLen -= 4;
	memcpy(pAtr, g_rcv + 4, *pAtrLen);	// copy data from payload.

	return JCOP_SIMUL_NO_ERROR;
}

/*!
 * \brief Function transmits C-APDU to a smart card and return R-APDU.<br>
 * <br>
 * \param [in] pSnd A pointer to first byte of message.
 * \param [in] iSndLen length of message.
 * \param [out] pRcv A pointer to buffer of received payload data. 
 * \param [in][out] pRcvLen [in]length of pRcv. caller's expected Max length of receiving payload data.
		[out]actual lengh of received payload data.
 *
 * \retval JCOP_SIMUL_NO_ERROR
 * \retval JCOP_SIMUL_ERROR_INITIALIZE
 * \retval JCOP_SIMUL_ERROR_TIMEOUT
 * \retval JCOP_SIMUL_ERROR_OTHER
 */
int JCOP_SIMUL_transmit(
	char const *const pSnd, 
	const unsigned short sndLen, 
	char *const pRcv, 
	unsigned short *const pRcvLen
)
{

	if (g_socket == INVALID_SOCKET) {
		return JCOP_SIMUL_ERROR_INITIALIZE;
	}

	dbg_ba2s(pSnd, sndLen);

	int status = send_receive(pSnd, sndLen, g_rcv, pRcvLen, NULL);
	dbg_log("*pRcvLen: %d", *pRcvLen);
	dbg_ba2s(g_rcv, *pRcvLen);
	if (status != 0) {
		dbg_log("send_receive failed! : 0x%X", status);
		close_socket();
		return status;
	}

	// first 4 byte of received data is header.
	// 01 00 00 02 90 00
	// 01 00 00 1D 6F 19 84 08 A0 00 00 00 03 00 00 00 A5 0D 9F 6E 06 40 51 40 36 20 17 9F 65 01 FF 90 00
	*pRcvLen -= 4;
	memcpy(pRcv, g_rcv + 4, *pRcvLen);	// copy data from payload.

	return JCOP_SIMUL_NO_ERROR;
}

/*!
 * \brief Function turn off a smart card.<br>
 */
void JCOP_SIMUL_close()
{
	close_socket();
}
