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
 * \file t1.cpp
 * \brief Source file for T=1 message functions.
 * \author Kenichi Kanai
 */

#include <string.h>

#include "t1.h"
#include "dbglog.h"

#include <windows.h>
#include "shared_data.h"
#include "jcop_simul.h"

#define PCB_I_SEQ	0x40
#define PCB_I_MORE	0x20
#define PCB_R_SEQ	0x10
#define PCB_S_CARD	0x20

static unsigned char g_sndISeq = 0x00;
static char g_sndBuf[JCOP_PROXY_BUFFER_SIZE];
static int g_sndBufOff = 0;

static bool g_isRcvChaining = false;
static char g_rcvBuf[JCOP_PROXY_BUFFER_SIZE];
static int g_rcvBufOff = 0;
static int g_rcvBufLen = 0;

/*!
 * \brief Function creates T=1 message.<br>
 * <br>
 * \param [out] pMsg A pointer to output buffer of T=1 message.
 * \param [in] nad NAD.
 * \param [in] pcb PCB.
 * \param [in] len LEN. INF length.
 * \param [in] pInf A pointer to buffer of INF.
 *
 * \retval length of created message.
 */
static unsigned short createT1Msg(
    char *const pMsg,
    unsigned char const nad,
    unsigned char const pcb,
    unsigned char const len,
    char const *const pInf
)
{
	// set T=1 header.
	pMsg[0] = nad;
	pMsg[1] = pcb;
	pMsg[2] = len;
	memcpy(&pMsg[3], pInf, len);

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
 * \brief reset ICC I-block sequence counter.<br>
 */
void T1_resetSeq()
{
	g_sndISeq = 0x00;
}

/*!
 * \brief Function process T=1 message.<br>
 * <br>
 * \param [in] pSnd A pointer to first byte of message.
 * \param [in] iSndLen length of message.
 * \param [out] pRcv A pointer to buffer of received payload data.
 * \param [in][out] pRcvLen [in]length of pRcv. caller's expected Max
		length of receiving payload data.
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
		// pSnd: MTY NAD LNH LNL | NAD PCB LEN | INF... | EDC
		// pSnd: 11000004 00C101 FE 3E
		// PCB E1: S-block IFS resp.

		// WTX req, ABORT req..

		// RESYNCH req
		if ((pcb & 0x1F) == 0x01) {
			T1_resetSeq();
		}

		*pRcvLen = createT1Msg(
		               pRcv,
		               nad,
		               (pcb | PCB_S_CARD),
		               pSnd[6],
		               &pSnd[7]
		           );
		return 0;
	}

	if (g_isRcvChaining) {

		if ((pcb & 0xC0) != 0x80) {
			// Not R-block (I-block)..
			char data[1] = { 0x00 };
			*pRcvLen = createT1Msg(
			               pRcv,
			               nad,
			               0x82,	// R-block(other err)
			               0x00,
			               data
			           );
			return 0;
		}

		// R-block
		int remain = g_rcvBufLen - g_rcvBufOff;
		unsigned char rSeq = 0x00;
		if ((pcb & PCB_R_SEQ) == PCB_R_SEQ) {
			// set sequence bit.
			// mirror sequence bit.
			rSeq = PCB_I_SEQ;
		}

		if (remain > MAX_IFS) {
			// I-block resp chaining continue.
			*pRcvLen = createT1Msg(
			               pRcv,
			               nad,
			               (PCB_I_MORE | rSeq),
			               MAX_IFS,
			               &g_rcvBuf[g_rcvBufOff]
			           );
			g_isRcvChaining = true;
			g_rcvBufOff += MAX_IFS;
			g_rcvBufLen -= MAX_IFS;
		} else {
			// I-block resp chaining end.
			*pRcvLen = createT1Msg(
			               pRcv,
			               nad,
			               (0x00 | rSeq),
			               (remain & 0x00FF),
			               &g_rcvBuf[g_rcvBufOff]
			           );
			g_isRcvChaining = false;
			g_rcvBufOff = 0;
			g_rcvBufLen = 0;
		}

		// set sequence bit for next I-block.
		// invert sequence bit.
		g_sndISeq = rSeq ^ PCB_I_SEQ;


	} else {

		if ((pcb & 0x80) != 0x00) {
			// Not I-block (R-block)..
			char data[1] = { 0x00 };
			*pRcvLen = createT1Msg(
			               pRcv,
			               nad,
			               0x82,	// R-block(other err)
			               0x00,
			               data
			           );
			return 0;
		}

		// I-block

		// remove socket header & T=1 header and EDC..
		unsigned short apduLen = sndLen - 4 - 4;
		memcpy(&g_sndBuf[g_sndBufOff], &pSnd[4 + 3], apduLen);
		g_sndBufOff += apduLen;

		dbg_ba2s(pSnd, apduLen + 4);

		if ((pcb & PCB_I_MORE) == PCB_I_MORE) {
			// PCB has a MORE bit.

			unsigned char rSeq = 0x00;
			if ((pcb & PCB_I_SEQ) != PCB_I_SEQ) {
				// set sequence bit.
				rSeq = PCB_R_SEQ;
			}

			// R-block
			char data[1] = { 0x00 };
			*pRcvLen = createT1Msg(
			               pRcv,
			               nad,
			               (0x80 | rSeq),
			               0x00,
			               data
			           );
			return 0;
		}

		// reconstruct socket message.
		// pSnd: MTY NAD LNH LNL | NAD PCB LEN | INF... | EDC
		// pSnd: 11000009 000005 80CA9F7F00 AF
		pSnd[0] = 0x01;	// MTY=0x01:  Transmit APDU
		pSnd[2] = g_sndBufOff / 256;	// LNH High byte of payload length
		pSnd[3] = g_sndBufOff % 256;	// LNL Low byte of payload length
		memcpy(&pSnd[4], g_sndBuf, g_sndBufOff);
		// pSnd: MTY NAD LNH LNL | DATA...
		// pSnd: 01000005 80CA9F7F00
		dbg_ba2s(pSnd, g_sndBufOff + 4);

		// send command to JCOP simulator.
		unsigned short respLen = *pRcvLen;
		status = JCOP_SIMUL_transmit(
		             pSnd,
		             g_sndBufOff + 4,
		             &pRcv[3],
		             &respLen
		         );
		dbg_log("JCOP_SIMUL_transmit end with code %d", status);
		if (status != JCOP_SIMUL_NO_ERROR) {
			dbg_log("JCOP_SIMUL_transmit failed! - status: 0x%08X", status);
			*pRcvLen = 0;
			return status;
		}

		if (respLen < MAX_IFS) {
			// I-block resp end.
			*pRcvLen = createT1Msg(
			               pRcv,
			               nad,
			               g_sndISeq,
			               (unsigned char)respLen,
			               &pRcv[3]);
		} else {
			// I-block resp chaining start.
			memcpy(g_rcvBuf, &pRcv[3], respLen);
			g_isRcvChaining = true;
			g_rcvBufOff = MAX_IFS;
			g_rcvBufLen = respLen;
			*pRcvLen = createT1Msg(
			               pRcv,
			               nad,
			               (g_sndISeq | PCB_I_MORE),
			               MAX_IFS,
			               &pRcv[3]
			           );
		}

		// set sequence bit for next I-block.
		// invert sequence bit.
		g_sndISeq ^= PCB_I_SEQ;

		// I-block req chaining end.
		g_sndBufOff = 0;
	}

	dbg_ba2s(pRcv, *pRcvLen);
	return 0;
}
