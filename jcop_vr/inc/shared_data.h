/*
 * $Id $
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
 * \file shared_data.h
 * \brief header file defines the data of shared with use-mode application. 
 * \author Kenichi Kanai
 */
#ifndef __SHARED_DATA__
#define __SHARED_DATA__

#include "devioctl.h"

typedef struct _JCOP_PROXY_SHARED_EVENTS {
    HANDLE  hEventSnd;
    HANDLE  hEventRcv;
} JCOP_PROXY_SHARED_EVENTS, *PJCOP_PROXY_SHARED_EVENTS;

#define IOCTL_JCOP_PROXY_SET_EVENTS \
   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x888, METHOD_BUFFERED, FILE_ANY_ACCESS)

// allocate 1024 bytes as linux version do.
#define JCOP_PROXY_BUFFER_SIZE 1024
#define JCOP_PROXY_MAX_ATR_SIZE 33

// I don't know how to resize Smartcard resource manager's IFSD to 0xFE...
//#define MAX_IFS 0xFE
#define MAX_IFS 0x93

#endif // __SHARED_DATA__
