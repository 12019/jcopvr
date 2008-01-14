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
 * \file jcop_simul.h
 * \brief prototypes for functions which communicate with JCOP Simulator
 * \author Kenichi Kanai
 */

#define JCOP_SIMUL_NO_ERROR		0x00
#define JCOP_SIMUL_ERROR_INITIALIZE		0x01
#define JCOP_SIMUL_ERROR_TIMEOUT		0x02
#define JCOP_SIMUL_ERROR_BUFFER_TOO_SMALL	0x03
#define JCOP_SIMUL_ERROR_OTHER			0x04

int JCOP_SIMUL_powerUp(char *const pAtr, unsigned short *const pAtrLen);
int JCOP_SIMUL_transmit(char const *const pSnd, const unsigned short sndLen, char *const pRcv, unsigned short *const pRcvLen);
void JCOP_SIMUL_close();
