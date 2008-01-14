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
 * \file dbglog.cpp
 * \brief Source file that contains debug output functions.
 * \author Kenichi Kanai
 */
#include "dbglog.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include <wdm.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
}
#endif

#ifdef MY_DEBUG

static CCHAR g_buf[8192];

void dbg_ba2s(char const *const cp, int const cnt)
{
	int n = sprintf(g_buf, "%s", "[JCOP_VR] ");
	for (int i = 0; i < cnt; i++) {
		n += sprintf(g_buf + n, "0x%02X:", cp[i] & 0xff);
	}
	// Log to Debug View(Windows Sysinternals).
	DbgPrint(g_buf);
}

void dbg_log(char const *const pFmt, ...)
{
	int n = sprintf(g_buf, "%s", "[JCOP_VR] ");
	va_list marker;
	va_start(marker, pFmt);
	vsprintf(g_buf + n, pFmt, marker);
	va_end(marker);
	// Log to Debug View(Windows Sysinternals).
	DbgPrint(g_buf);
}

#endif
