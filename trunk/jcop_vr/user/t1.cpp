/*
 * $Id: jcop_simul.cpp 8 2008-01-15 11:22:57Z kanai.kenichi $
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
 * \file t1.cpp
 * \brief Source for T=1 message.
 * \author Kenichi Kanai
 */

#include <string.h>

#include "t1.h"
#include "dbglog.h"

#include <windows.h>
#include "shared_data.h"
#include "jcop_simul.h"

#define IFS	0xFE
#define PCB_I_SEQ	0x40
#define PCB_I_MORE	0x20
#define PCB_S_CARD	0x20

static bool g_isSndChained = false;
static unsigned char g_sndSeq = PCB_I_SEQ;
static char g_sndBuf[JCOP_PROXY_BUFFER_SIZE];
static int g_sndBufOff = -1;
static int g_sndBufLen = -1;

static bool g_isRcvChained = false;
static unsigned char g_rcvSeq = PCB_I_SEQ;
static char g_rcvBuf[JCOP_PROXY_BUFFER_SIZE];
static int g_rcvBufOff = -1;
static int g_rcvBufLen = -1;


static unsigned short createT1Msg(
    char *const pMsg,
    unsigned char const nad,
    unsigned char const pcb,
    unsigned char const len,
    char const *const pData
)
{
	// set T=1 header.
	pMsg[0] = nad;
	pMsg[1] = pcb;
	pMsg[2] = len;
	memcpy(&pMsg[3], pData, len);

	// add T=1 EDC.
	int offEdc = len + 3;
	pMsg[offEdc] = 0x00;
	// calc LRC.
	for (int i = 0; i < offEdc; i++) {
		pMsg[offEdc] ^= pMsg[i];
	}
	return offEdc + 1;	// message length
}

/*!
 * \brief Function process T=1 message.<br>
 * <br>
 * \param [in] pSnd A pointer to first byte of message.
 * \param [in] iSndLen length of message.
 * \param [out] pRcv A pointer to buffer of received payload data.
 * \param [in][out] pRcvLen [in]length of pRcv. caller's expected Max length of receiving payload data.
		[out]actual lengh of received payload data.
 *
 * \retval 0
 */
int T1_processMsg(
    char *const pSnd,
    const unsigned short sndLen,
    char *const pRcv,
    unsigned short *const pRcvLen
)
{
	dbg_ba2s(pSnd, sndLen);

	unsigned char nad = pSnd[4];	// T=1 NAD
	unsigned char pcb = pSnd[5];	// PCB
	dbg_log("pcb: 0x%08X", pcb);

	int status;

	if ((pcb & 0xC0) == 0xC0) {
		// S-block

		// IFS req
		// pSnd: MTY NAD LNH LNL | NAD PCB LEN | DATA... | EDC
		// pSnd: 11000004 00C101 FE 3E
		// PCB E1: S-block IFS resp.

		// WTX req

		*pRcvLen = createT1Msg(pRcv, nad, (pcb | PCB_S_CARD), pSnd[6], &pSnd[7]);
		return 0;
	}

	if (g_isRcvChained) {

		if ((pcb & 0xC0) != 0x80) {
			// Not R-block (I-block)..
			char data[1] = { 0x00 };
			*pRcvLen = createT1Msg(pRcv, nad, 0x82, 0x00, data);	// R-block
			return 0;
		}

		// R-block
		int remain = g_rcvBufLen - g_rcvBufOff;
		if (remain > IFS) {
			// I-block resp chaining continue.
			*pRcvLen = createT1Msg(pRcv, nad, (PCB_I_MORE | g_rcvSeq), IFS, &g_rcvBuf[g_rcvBufOff]);
			g_rcvBufOff += IFS;
			g_rcvBufLen -= IFS;
			g_rcvSeq ^= PCB_I_SEQ;
		} else {
			// I-block resp chaining end.
			*pRcvLen = createT1Msg(pRcv, nad, (0x00 | g_rcvSeq), (remain & 0x00FF), &g_rcvBuf[g_rcvBufOff]);
			g_isRcvChained = false;
			g_rcvBufOff = -1;
			g_rcvBufLen = -1;
			g_rcvSeq = PCB_I_SEQ;
		}

	} else {

		if ((pcb & 0x80) != 0x00) {
			// Not I-block (R-block)..
			char data[1] = { 0x00 };
			*pRcvLen = createT1Msg(pRcv, nad, 0x82, 0x00, data);	// R-block
			return 0;
		}

		// I-block
		if ((pcb & PCB_I_MORE) == PCB_I_MORE) {
			// PCB has a MORE bit.
			char data[1] = { 0x00 };
			*pRcvLen = createT1Msg(pRcv, nad, 0x80, 0x00, data);	// R-block
			return 0;
		}

		// pSnd: MTY NAD LNH LNL | NAD PCB LEN | DATA... | EDC
		// pSnd: 11000009 000005 80CA9F7F00 AF

		// remove T=1 message and reconstruct socket message.
		pSnd[0] = 0x01;	// MTY=0x01:  Transmit APDU
		unsigned short len = sndLen - 4;	// remove T=1 header and EDC.
		unsigned short apduLen = len - 4;	// remove socket header.
		pSnd[2] = apduLen / 256;	// LNH High byte of payload length
		pSnd[3] = apduLen % 256;	// LNL Low byte of payload length
		memmove(&pSnd[4], &pSnd[4 + 3], apduLen);

		// pSnd: MTY NAD LNH LNL | DATA...
		// pSnd: 11000005 80CA9F7F00

		dbg_ba2s(pSnd, len);
		unsigned short respLen = *pRcvLen;
		// send command to JCOP simulator.
		status = JCOP_SIMUL_transmit(pSnd, len, &pRcv[3], &respLen);
		dbg_log("JCOP_SIMUL_transmit end with code %d", status);
		if (status != JCOP_SIMUL_NO_ERROR) {
			dbg_log("JCOP_SIMUL_transmit failed! - status: 0x%08X", status);
			return status;
		}

		if (respLen < IFS) {
			// I-block resp end.
			*pRcvLen = createT1Msg(pRcv, nad, pcb, (unsigned char)respLen, &pRcv[3]);
		} else {
			// I-block resp chaining start.
			*pRcvLen = createT1Msg(pRcv, nad, (pcb | PCB_I_MORE), IFS, &pRcv[3]);
			memcpy(g_rcvBuf, &pRcv[3], respLen);
			g_isRcvChained = true;
			g_rcvBufOff = IFS;
			g_rcvBufLen = respLen;
		}
	}

	dbg_ba2s(pRcv, *pRcvLen);

	return 0;
}
