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
 * \file jcop_vr.cpp
 * \brief JCOP Simulation Virtual Reader Driver - Main Module
 * \author Kenichi Kanai
 */
#ifdef __cplusplus
extern "C"
{
#endif

#include <wdm.h>

	NTSTATUS DriverEntry(PDRIVER_OBJECT, PUNICODE_STRING);

#ifdef __cplusplus
}
#endif

#include <smclib.h>

#include "dbglog.h"
#include "shared_data.h"

#define VR_DEVICE_NAME L"\\Device\\JCopVirtualReader"
#define VR_DOS_DEVICE_NAME L"\\DosDevices\\JCopVirtualReader"

#define VR_VENDOR_NAME "JCOP Simulation"
#define VR_IFD_TYPE "Virtual Reader"
#define VR_UNIT_NO 0

#define SMARTCARD_POOL_TAG 'poCJ'

typedef struct _DEVICE_EXTENSION {
	SMARTCARD_EXTENSION smartcardExtension;
	UNICODE_STRING linkName;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

typedef struct _READER_EXTENSION {
	HANDLE hEventSnd;
	unsigned short iSndLen;
	PCHAR pSndBuffer;
	HANDLE hEventRcv;
	unsigned short iRcvLen;
	PCHAR pRcvBuffer;
} READER_EXTENSION, *PREADER_EXTENSION;


///////////////////////////////////////////////////////////////////////////////
// JCOP proxy user-mode application message exchange function.
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Message exchange function communicate with user-mode application.<br>
 * <br>
 * \param [in] pSmartcardExtension A pointer to the smart card extension,
		SMARTCARD_EXTENSION, of the device.
 * \param [in] mty MTY: Message type.
 * \param [in] nad NAD: Node addess
 * \param [in] pSnd PY0: A pointer to first byte of payload.
 * \param [in] sndLen LN: length of payload.
 * \param [out] pRcv A pointer to buffer of received data.
 * \param [in] rcvLenExp length of pRcv. caller's expected Max length of receiving data.
 * \param [out] pRcvLen actual lengh of received data.
 * \param [in] pDueTime wait time duration in LARGE_INTEGER. if it is NULL, 
	the routine waits indefinitely.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 * \retval STATUS_IO_TIMEOUT The request timed out.
 * \retval STATUS_BUFFER_TOO_SMALL Expected ATR Length is too small.
 */
static int sendMessage(
    PREADER_EXTENSION pReaderExtension,
    unsigned char const mty,
    unsigned char const nad,
    char const *const pSnd,
    unsigned short const sndLen,
    char *const pRcv,
    unsigned short const rcvLenExp,
    unsigned short *const pRcvLen,
	PLARGE_INTEGER pDueTime)
{
	dbg_log("sendMessage start");

	NTSTATUS status;

	// set exchanging messages in pReaderExtension->pSndBuffer.
	if (pReaderExtension == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		dbg_log("pReaderExtension == NULL");
		return status;
	}
	if (pReaderExtension->pSndBuffer == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		dbg_log("pReaderExtension->pSndBuffer == NULL");
		return status;
	}
	// set message header.
	pReaderExtension->pSndBuffer[0] = mty;			// MTY
	pReaderExtension->pSndBuffer[1] = nad;			// NAD
	pReaderExtension->pSndBuffer[2] = sndLen / 256;	// LNH High byte of payload length
	pReaderExtension->pSndBuffer[3] = sndLen % 256;	// LNL Low byte of payload length
	// set message payload.
	RtlCopyMemory(pReaderExtension->pSndBuffer + 4, pSnd, sndLen);
	// set whole message length.
	pReaderExtension->iSndLen = sndLen + 4;

	// notify to the user-mode application.
	if (pReaderExtension->hEventSnd == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		dbg_log("pReaderExtension->hEventSnd == NULL");
		return status;
	}
	KeSetEvent((PKEVENT)pReaderExtension->hEventSnd, 0, FALSE);

	// wait for the process completion of user-mode application as follows:
	//  1. invoke ReadFile and get command data in pReaderExtension->pSndBuffer.
	//  2. communicate with JCOP simulator.
	//  3. invoke WriteFile and set response data in pReaderExtension->pRcvBuffer.
	//  4. set event pReaderExtension->hEventRcv.

	if (pReaderExtension->hEventRcv == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		dbg_log("pReaderExtension->hEventRcv == NULL");
		return status;
	}
	// wait for event.
	status = KeWaitForSingleObject(
	             (PKEVENT)pReaderExtension->hEventRcv,
	             Executive,
	             KernelMode,
	             FALSE,
	             pDueTime
	         );
	if (status != STATUS_SUCCESS) {
		switch (status) {
			case STATUS_ALERTED :
				dbg_log("STATUS_ALERTED\r\n");
				break;
			case STATUS_USER_APC :
				dbg_log("STATUS_USER_APC \r\n");
				break;
			case STATUS_TIMEOUT :
				dbg_log("STATUS_TIMEOUT \r\n");
				break;
			case STATUS_ABANDONED_WAIT_0 :
				dbg_log("STATUS_ABANDONED_WAIT_0 \r\n");
				break;
			default:
				dbg_log("STATUS_XXXXX \r\n");
				break;
		}
		return status;
	}

	// get data from pReaderExtension->pRcvBuffer.
	if (rcvLenExp < pReaderExtension->iRcvLen) {
		dbg_log("STATUS_BUFFER_TOO_SMALL - *pRcvLen: %d", *pRcvLen);
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (pReaderExtension->pRcvBuffer == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		dbg_log("pReaderExtension->pRcvBuffer == NULL");
		return status;
	}
	*pRcvLen = pReaderExtension->iRcvLen;
	RtlCopyMemory(pRcv, pReaderExtension->pRcvBuffer, pReaderExtension->iRcvLen);

	dbg_log("pReaderExtension->iRcvLen: %d", pReaderExtension->iRcvLen);
	dbg_ba2s(pRcv, pReaderExtension->iRcvLen);

	status = STATUS_SUCCESS;
	dbg_log("sendMessage end - status: 0x%08X", status);
	return status;
}


///////////////////////////////////////////////////////////////////////////////
// Smart Card Driver Library Callback Routines(RDF_XXXXX).
// http://msdn2.microsoft.com/en-us/library/ms801318.aspx
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Function resets a smart card and return ATR.<br>
 * <br>
 * \param [in] pSmartcardExtension A pointer to the smart card extension,
		SMARTCARD_EXTENSION, of the device.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 * \retval STATUS_IO_TIMEOUT The request timed out.
 * \retval STATUS_BUFFER_TOO_SMALL Expected ATR Length is too small.
 * \retval STATUS_NO_MEDIA Other errors during initalization(No smart card is
	inserted in the reader).
 */
static NTSTATUS resetCard(PSMARTCARD_EXTENSION pSmartcardExtension)
{
	dbg_log("resetCard start");
	NTSTATUS status = STATUS_SUCCESS;

	if (pSmartcardExtension->IoRequest.ReplyBufferLength < JCOP_PROXY_MAX_ATR_SIZE) {
		dbg_log(
		    "STATUS_BUFFER_TOO_SMALL - pSmartcardExtension->IoRequest.ReplyBufferLength: %d",
		    pSmartcardExtension->IoRequest.ReplyBufferLength
		);
		return STATUS_BUFFER_TOO_SMALL;
	}

	PREADER_EXTENSION pReaderExtension = pSmartcardExtension->ReaderExtension;

	// send "Wait for card" message.
	unsigned char mty = 0x00;	// MTY 0x00(Wait for card)
	unsigned char nad = 0x21;	// NAD
	char pSnd[4];	// PY0 payload (interpretation depends on message type)
	RtlZeroMemory(pSnd, 4);

	unsigned short atrLen;
	char atr[JCOP_PROXY_MAX_ATR_SIZE];

	long msec = 1000;	// wait for 1sec.
	LARGE_INTEGER dueTime;
	dueTime.QuadPart = -10000 * msec;

	status = sendMessage(
	             pReaderExtension,
	             mty,
	             nad,
	             pSnd,
	             4,
	             atr,
	             (unsigned short)pSmartcardExtension->IoRequest.ReplyBufferLength,
	             &atrLen,
				 &dueTime
	         );
	if (status != STATUS_SUCCESS) {
		dbg_log("sendResetMessage failed! - status: 0x%08X", status);
		switch (status) {
			case STATUS_IO_TIMEOUT :
				return STATUS_IO_TIMEOUT;
			case STATUS_BUFFER_TOO_SMALL :
				return STATUS_BUFFER_TOO_SMALL;
			default :
				return STATUS_NO_MEDIA;
		}
	}

	// On output, the structure pointed to by SmartcardExtension should
	// have the following values:
	//
	//  - IoRequest.ReplyBuffer
	// 		Receives the ATR that is returned by the smart card. In addition,
	// 		you must transfer the ATR to SmartcardExtension->CardCapabilities.ATR.Buffer
	// 		so that the library can parse the ATR.
	//  - IoRequest.Information
	// 		Receives the length of the ATR.
	//  - CardCapabilities.ATR.Length
	// 		Contains the length of the ATR.
	//
	// http://msdn2.microsoft.com/en-us/library/ms801315.aspx
	RtlCopyMemory(pSmartcardExtension->IoRequest.ReplyBuffer, atr, atrLen);
	*pSmartcardExtension->IoRequest.Information = atrLen;

	// set state the reader connected, but the card has been reset.
	pSmartcardExtension->ReaderCapabilities.CurrentState = SCARD_NEGOTIABLE;

	// we do not to parse ATR with SmartcardUpdateCardCapabilities.
	// The SmartcardUpdateCardCapabilities routine translates an answer-to-reset (ATR)
	// string into the SCARD_CARD_CAPABILITIES structure that the driver can use.
	// http://msdn2.microsoft.com/en-us/library/ms801323.aspx
	//
	//RtlCopyMemory(pSmartcardExtension->CardCapabilities.ATR.Buffer, atr, atrLen);
	//pSmartcardExtension->CardCapabilities.ATR.Length = (UCHAR)atrLen;
	//status = SmartcardUpdateCardCapabilities(pSmartcardExtension);
	//if (status != STATUS_SUCCESS) {
	//	dbg_log("SmartcardUpdateCardCapabilities failed! - status: 0x%08X", status);
	//	return status;
	//}
	
	pSmartcardExtension->CardCapabilities.Protocol.Selected = SCARD_PROTOCOL_T0;
	pSmartcardExtension->ReaderCapabilities.CurrentState = SCARD_SPECIFIC;

	dbg_log("resetCard end - status: 0x%08X", status);
	return status;
}

/*!
 * \brief Function turns off a smart card.<br>
 * <br>
 * \param [in] pSmartcardExtension A pointer to the smart card extension,
		SMARTCARD_EXTENSION, of the device.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 * \retval STATUS_IO_TIMEOUT The request timed out.
 */
static NTSTATUS powerDown(PSMARTCARD_EXTENSION pSmartcardExtension)
{
	dbg_log("powerDown start");
	NTSTATUS status = STATUS_SUCCESS;

	PREADER_EXTENSION pReaderExtension = pSmartcardExtension->ReaderExtension;

	// send "Close socket" message.
	unsigned char mty = 0x7F;	// MTY 0x7F(Close socket)
	unsigned char nad = 0x21;	// NAD
	char pSnd[4];	// PY0 payload (interpretation depends on message type)
	RtlZeroMemory(pSnd, 4);

	char pRcv[8];
	unsigned short rcvLen;

	long msec = 1000;	// wait for 1sec.
	LARGE_INTEGER dueTime;
	dueTime.QuadPart = -10000 * msec;

	status = sendMessage(
		pReaderExtension, 
		mty, 
		nad, 
		pSnd, 
		4, 
		pRcv, 
		8, 
		&rcvLen, 
		&dueTime
	);
	if (status != STATUS_SUCCESS) {
		dbg_log("sendPowerDownMessage failed! - status: 0x%08X", status);
		switch (status) {
			case STATUS_IO_TIMEOUT :
				return STATUS_IO_TIMEOUT;
			default :
				return STATUS_NO_MEDIA;
		}
	}

	// set state the reader connected, but a card is not powered.
	pSmartcardExtension->ReaderCapabilities.CurrentState = SCARD_PRESENT;
	status = STATUS_SUCCESS;

	dbg_log("powerDown end - status: 0x%08X", status);
	return status;
}


/*!
 * \brief Entry point for RDF_CARD_POWER.<br>
 * <br>
 * The RDF_CARD_POWER callback function resets or turns off an inserted smart card.
 * <br>
 * \param [in] pSmartcardExtension A pointer to the smart card extension,
		SMARTCARD_EXTENSION, of the device.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 * \retval STATUS_INVALID_DEVICE_REQUEST The MinorIoControlCode is an unknown code.
 */
NTSTATUS VR_RDF_PowerCard(IN PSMARTCARD_EXTENSION pSmartcardExtension)
{
	dbg_log("VR_RDF_PowerCard start");
	NTSTATUS status;

	switch (pSmartcardExtension->MinorIoControlCode) {
		case SCARD_POWER_DOWN :
			dbg_log("SCARD_POWER_DOWN");
			status = powerDown(pSmartcardExtension);
			break;
		case SCARD_COLD_RESET :
			dbg_log("SCARD_COLD_RESET");
			status = resetCard(pSmartcardExtension);
			break;
		case SCARD_WARM_RESET :
			dbg_log("SCARD_WARM_RESET");
			status = resetCard(pSmartcardExtension);
			break;
		default :
			dbg_log("SCARD_XXXXX(unknown): 0x%08X", pSmartcardExtension->MinorIoControlCode);
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	dbg_log("VR_RDF_PowerCard end - status: 0x%08X", status);
	return status;
}

/*!
 * \brief Entry point for RDF_SET_PROTOCOL.<br>
 * <br>
 * The RDF_SET_PROTOCOL callback function set a transmission protocol
	for the inserted smart card.
 * <br>
 * \param [in] pSmartcardExtension A pointer to the smart card extension,
		SMARTCARD_EXTENSION, of the device.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 * \retval STATUS_INVALID_DEVICE_REQUEST The mask contains an unknown protocol.
 */
NTSTATUS VR_RDF_SetProtocol(PSMARTCARD_EXTENSION pSmartcardExtension)
{
	dbg_log("VR_RDF_SetProtocol start");

	if (pSmartcardExtension->ReaderCapabilities.CurrentState == SCARD_SPECIFIC) {
		dbg_log("pSmartcardExtension->ReaderCapabilities.CurrentState has been already SCARD_SPECIFIC.");
		return STATUS_SUCCESS;
	}

	USHORT protocol = (USHORT)(pSmartcardExtension->MinorIoControlCode);
	//if ((protocol & (SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1)) == 0) {
	if ((protocol & SCARD_PROTOCOL_T0) == 0) {
		// protocol is not T=0.
		dbg_log("STATUS_INVALID_DEVICE_REQUEST - protocol is not T=0");
		dbg_log(
		    "pSmartcardExtension->MinorIoControlCode: 0x%08X",
		    pSmartcardExtension->MinorIoControlCode
		);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	// The request returns the following values:
	//
	//  - SmartcardExtension->IoRequest.ReplyBuffer
	//     Contains the selected protocol.
	//  - SmartcardExtension->IoRequest.Information
	//     Set to sizeof(ULONG).
	//
	// The caller can supply a mask of acceptable protocols. The driver's set protocol
	// callback routine selects one of the protocols in the mask and returns
	// the selected protocol in SmartcardExtension->IoRequest.ReplyBuffer.
	//
	// http://msdn2.microsoft.com/en-us/library/ms801313.aspx

	// select T=0.
	protocol = SCARD_PROTOCOL_T0;

	// return the selected protocol to the caller.
	*(PULONG)(pSmartcardExtension->IoRequest.ReplyBuffer) = protocol;
	*(pSmartcardExtension->IoRequest.Information) = sizeof(ULONG);
	pSmartcardExtension->CardCapabilities.Protocol.Selected = protocol;

	// set state the reader connected, but the card has been reset.
	pSmartcardExtension->ReaderCapabilities.CurrentState = SCARD_SPECIFIC;

	dbg_log("VR_RDF_SetProtocol end - status: 0x%08X", STATUS_SUCCESS);
	return STATUS_SUCCESS;
}


/*!
 * \brief Function performs data transmissions T=0.<br>
 * <br>
 * \param [in] pSmartcardExtension A pointer to the smart card extension,
		SMARTCARD_EXTENSION, of the device.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
static NTSTATUS transmitT0(IN PSMARTCARD_EXTENSION pSmartcardExtension)
{
	dbg_log("transmitT0 start");
	NTSTATUS status = STATUS_SUCCESS;

	status = SmartcardT0Request(pSmartcardExtension);
	if (status != STATUS_SUCCESS) {
		dbg_log("SmartcardT0Request failed! - status: 0x%08X", status);
		return status;
	}
	dbg_log("transmitT0 SEND: ");
	dbg_ba2s(
	    (char const *const)pSmartcardExtension->SmartcardRequest.Buffer,
	    pSmartcardExtension->SmartcardRequest.BufferLength
	);

	// send command to JCOP simulator.
	PREADER_EXTENSION pReaderExtension = pSmartcardExtension->ReaderExtension;

	// send "APDU" meessage.
	unsigned char mty = 0x01;	// MTY 0x01(APDU)
	unsigned char nad = 0x00;	// NAD

	status = sendMessage(
	             pReaderExtension,
	             mty,
	             nad,
	             (char *)pSmartcardExtension->SmartcardRequest.Buffer,
	             (unsigned short)pSmartcardExtension->SmartcardRequest.BufferLength,
	             (char *)pSmartcardExtension->SmartcardReply.Buffer,
	             (unsigned short)pSmartcardExtension->SmartcardReply.BufferSize,
	             (unsigned short *) & pSmartcardExtension->SmartcardReply.BufferLength,
				 NULL	// wait indefinitely
	         );
	dbg_log(
	    "pSmartcardExtension->SmartcardReply.BufferLength: %d",
	    pSmartcardExtension->SmartcardReply.BufferLength
	);
	dbg_ba2s(
	    (char *const)pSmartcardExtension->SmartcardReply.Buffer,
	    pSmartcardExtension->SmartcardReply.BufferLength
	);
	if (status != STATUS_SUCCESS) {
		dbg_log("sendApduMessage failed! - status: 0x%08X", status);
		return status;
	}

	status = SmartcardT0Reply(pSmartcardExtension);
	if (status != STATUS_SUCCESS) {
		dbg_log("SmartcardT0Reply failed! - status: 0x%08X", status);
		return status;
	}

	dbg_log("transmitT0 end - status: 0x%08X", status);
	return status;
}

/*!
 * \brief Entry point for RDF_TRANSMIT.<br>
 * <br>
 * The RDF_TRANSMIT callback function performs data transmissions.
 * <br>
 * \param [in] pSmartcardExtension A pointer to the smart card extension,
		SMARTCARD_EXTENSION, of the device.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 * \retval STATUS_IO_TIMEOUT The request timed out.
 * \retval STATUS_INVALID_DEVICE_REQUEST The protocol, defined by dwProtocol, is invalid.
 */
NTSTATUS VR_RDF_Transmit(IN PSMARTCARD_EXTENSION pSmartcardExtension)
{
	dbg_log("VR_RDF_Transmit start");
	// On input, the caller must pass the following values to the function:
	// - SmartcardExtension->MajorIoControlCode
	//     Contains IOCTL_SMARTCARD_TRANSMIT.
	// - SmartcardExtension->IoRequest.RequestBuffer
	//     A pointer to an SCARD_IO_REQUEST structure followed by data to
	//     transmit to the card.
	// - SmartcardExtension->IoRequest.RequestBufferLength
	//     The number of bytes to transmit to the card.
	// - SmartcardExtension->IoRequest.ReplyBufferLength
	//     The size, in bytes, of the reply buffer.
	//
	// The request returns the following values:
	// - SmartcardExtension->IoRequest.ReplyBuffer
	//     A pointer to the buffer that receives the SCARD_IO_REQUEST structure,
	//      plus the result of the card.
	// - SmartcardExtension->IoRequest.Information
	//     Receives the actual number of bytes returned by the smart card, plus
	//     the size of the SCARD_IO_REQUEST structure. For a definition of the
	//     SCARD_IO_REQUEST structure, see IOCTL_SMARTCARD_TRANSMIT.
	//
	// When this function is called, SmartcardExtension->IoRequest.RequestBuffer
	// points to an SCARD_IO_REQUEST structure followed by the data to transmit.
	//
	// http://msdn2.microsoft.com/en-us/library/ms801311.aspx
	NTSTATUS status;

	switch (pSmartcardExtension->CardCapabilities.Protocol.Selected) {
		case SCARD_PROTOCOL_T0 :
			dbg_log("SCARD_PROTOCOL_T0");
			status = transmitT0(pSmartcardExtension);
			break;
		case SCARD_PROTOCOL_T1 :
			dbg_log("SCARD_PROTOCOL_T1");
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		case SCARD_PROTOCOL_RAW :
			dbg_log("SCARD_PROTOCOL_RAW");
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		default :
			dbg_log(
			    "SCARD_PROTOCOL_XXXXX(unknown): 0x%08X",
			    pSmartcardExtension->CardCapabilities.Protocol.Selected
			);
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	dbg_log("VR_RDF_Transmit end - status: 0x%08X", status);
	return status;
}

/*!
 * \brief Cancel routine for RDF_CARD_TRACKING.<br>
 * <br>
 * \param [in] pDeviceObject Caller-supplied pointer to a DEVICE_OBJECT structure.
	This is the device object for the target device, previously created by
	the driver's AddDevice routine.
 * \param [in] pIrp Caller-supplied pointer to an IRP structure that describes
	the I/O operation to be canceled.
 *
 * \retval STATUS_CANCELLED
 */
NTSTATUS VR_RDF_Cancel(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
	dbg_log("VR_RDF_Cancel start");

	PDEVICE_EXTENSION pDeviceExtension = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	PSMARTCARD_EXTENSION pSmartcardExtension = &pDeviceExtension->smartcardExtension;

	// The reader driver must complete the request as soon as it detects that a smart
	// card has been inserted or removed. The reader driver completes the request by
	// calling IoCompleteRequest, after which, the reader driver must set the NotificationIrp
	// member of SmartcardExtension -> OsData back to NULL to inform the driver library
	// that the reader driver can accept further smart card tracking requests.
	// http://msdn2.microsoft.com/en-us/library/ms801317.aspx
	pSmartcardExtension->OsData->NotificationIrp = NULL;

	pIrp->IoStatus.Information = 0;
	pIrp->IoStatus.Status = STATUS_CANCELLED;

	// The I/O manager calls IoAcquireCancelSpinLock before calling a driver's Cancel routine,
	// so the Cancel routine must call IoReleaseCancelSpinLock at some point.
	// The routine should not hold the spin lock longer than necessary.
	// http://msdn2.microsoft.com/en-us/library/ms795319.aspx
	IoReleaseCancelSpinLock(pIrp->CancelIrql);
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	// The Cancel routine must set the I/O status block's Status member to STATUS_CANCELLED,
	// and set its Information member to zero. The routine must then complete the specified
	// IRP by calling IoCompleteRequest.
	dbg_log("VR_RDF_Cancel end - status: 0x%08X", STATUS_CANCELLED);
	return STATUS_CANCELLED;
}

/*!
 * \brief Entry point for RDF_CARD_TRACKING.<br>
 * <br>
 * The RDF_CARD_TRACKING callback function installs an event handler to
	track every time a card is inserted in or removed from a card reader.
 * <br>
 * \param [in] pSmartcardExtension A pointer to the smart card extension,
		SMARTCARD_EXTENSION, of the device.
 *
 * \retval STATUS_PENDING Smart card tracking has started.
 */
NTSTATUS VR_RDF_CardTracking(IN PSMARTCARD_EXTENSION pSmartcardExtension)
{
	dbg_log("VR_RDF_CardTracking start");
	KIRQL cancelIrql;

	// The corresponding WDM driver library adds a pointer to the request
	// in SmartcardExtension->OsData->NotificationIrp.
	// http://msdn2.microsoft.com/en-us/library/ms801317.aspx
	IoAcquireCancelSpinLock(&cancelIrql);
	IoSetCancelRoutine(pSmartcardExtension->OsData->NotificationIrp, VR_RDF_Cancel);
	IoReleaseCancelSpinLock(cancelIrql);

	dbg_log("VR_RDF_CardTracking end - status: 0x%08X", STATUS_PENDING);
	return STATUS_PENDING;
}

/*!
 * \brief Function creates a new smart card device instance.<br>
 * <br>
 *  - setup the device extension.<br>
 *  - setup the smartcard extension.<br>
 *  - invoke SmartcardInitialize function.<br>
 *  - invoke SmartcardCreateLink function.<br>
 * <br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pDeviceName A pointer to a UNICODE_STRING that contains
		the existing device name to create the link for.
 *
 * \retval STATUS_SUCCESS the routine successfully end.(SmartcardCreateLink)
 * \retval STATUS_INSUFFICIENT_RESOURCES The amount of memory that is required to
		allocate the buffers is not available.(SmartcardInitialize)
 * \retval STATUS_INVALID_PARAMETER_1 LinkName is NULL.(SmartcardCreateLink)
 * \retval STATUS_INVALID_PARAMETER_2 pDeviceName is NULL.(SmartcardCreateLink)
 * \retval STATUS_INSUFFICIENT_RESOURCES This routine could not allocate memory
		for the link name.(SmartcardCreateLink)
 */
static NTSTATUS createReaderDevice(IN PDEVICE_OBJECT pDeviceObject, IN PUNICODE_STRING pDeviceName)
{
	dbg_log("createReaderDevice start");

	PSMARTCARD_EXTENSION pSmartcardExtension;
	PDEVICE_EXTENSION pDeviceExtension;
	PREADER_EXTENSION pReaderExtension;
	NTSTATUS status = STATUS_SUCCESS;

	// set up the device extension.
	pDeviceExtension = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	RtlZeroMemory(pDeviceExtension, sizeof(DEVICE_EXTENSION));
	pSmartcardExtension = &pDeviceExtension->smartcardExtension;

	pReaderExtension = (PREADER_EXTENSION)ExAllocatePool(NonPagedPool, sizeof(READER_EXTENSION));
	if (pReaderExtension == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		dbg_log("ExAllocatePool failed! - pReaderExtension == NULL");
		return status;
	}
	pSmartcardExtension->ReaderExtension = pReaderExtension;

	// allocate the send & receive buffer.
	pReaderExtension->pSndBuffer = (PCHAR)ExAllocatePool(NonPagedPool, JCOP_PROXY_BUFFER_SIZE);
	if (pReaderExtension->pSndBuffer == NULL) {
		dbg_log("ExAllocatePoolWithTag Error! pSndBuffer == NULL\r\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	pReaderExtension->pRcvBuffer = (PCHAR)ExAllocatePool(NonPagedPool, JCOP_PROXY_BUFFER_SIZE);
	if (pReaderExtension->pRcvBuffer == NULL) {
		dbg_log("ExAllocatePoolWithTag Error! pRcvBuffer == NULL\r\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// setup smartcard extension - callback's.
	// implement only mandatory functions.
	// http://msdn2.microsoft.com/en-us/library/ms801428.aspx
	pSmartcardExtension->ReaderFunction[RDF_CARD_POWER] = VR_RDF_PowerCard;
	pSmartcardExtension->ReaderFunction[RDF_SET_PROTOCOL] = VR_RDF_SetProtocol;
	pSmartcardExtension->ReaderFunction[RDF_TRANSMIT] = VR_RDF_Transmit;
	pSmartcardExtension->ReaderFunction[RDF_CARD_TRACKING] = VR_RDF_CardTracking;

	// setup smartcard extension - vendor attribute
	RtlCopyMemory(pSmartcardExtension->VendorAttr.VendorName.Buffer,
	              VR_VENDOR_NAME,
	              sizeof(VR_VENDOR_NAME)
	             );
	pSmartcardExtension->VendorAttr.VendorName.Length = sizeof(VR_VENDOR_NAME);
	RtlCopyMemory(pSmartcardExtension->VendorAttr.IfdType.Buffer,
	              VR_IFD_TYPE,
	              sizeof(VR_IFD_TYPE)
	             );
	pSmartcardExtension->VendorAttr.IfdType.Length = sizeof(VR_IFD_TYPE);
	pSmartcardExtension->VendorAttr.UnitNo = VR_UNIT_NO;

	pSmartcardExtension->VendorAttr.IfdVersion.VersionMajor = 0;
	pSmartcardExtension->VendorAttr.IfdVersion.VersionMinor = 1;
	pSmartcardExtension->VendorAttr.IfdVersion.BuildNumber = 1;
	pSmartcardExtension->VendorAttr.IfdSerialNo.Length = 0;

	// setup smartcard extension - reader capabilities
	//pSmartcardExtension->ReaderCapabilities.ReaderType =
	//    SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1;
	pSmartcardExtension->ReaderCapabilities.ReaderType = SCARD_PROTOCOL_T0;
	pSmartcardExtension->ReaderCapabilities.SupportedProtocols = 
		SCARD_READER_TYPE_VENDOR;
	// set state the reader connected, but a card is not powered.
	pSmartcardExtension->ReaderCapabilities.CurrentState = SCARD_PRESENT;
    pSmartcardExtension->ReaderCapabilities.CLKFrequency.Default = 3580;
    pSmartcardExtension->ReaderCapabilities.CLKFrequency.Max = 3580;
    pSmartcardExtension->ReaderCapabilities.DataRate.Default = 9600;
    pSmartcardExtension->ReaderCapabilities.DataRate.Max = 9600;
	pSmartcardExtension->ReaderCapabilities.MaxIFSD = 254;

	// invoke SmartcardInitialize
	pSmartcardExtension->Version = SMCLIB_VERSION;
	pSmartcardExtension->SmartcardRequest.BufferSize = MIN_BUFFER_SIZE;
	pSmartcardExtension->SmartcardReply.BufferSize = MIN_BUFFER_SIZE;
	status = SmartcardInitialize(pSmartcardExtension);
	if (status != STATUS_SUCCESS) {
		dbg_log("SmartcardInitialize failed! - status: 0x%08X", status);
		return status;
	}

	// invoke SmartcardCreateLink
	status = SmartcardCreateLink(&(pDeviceExtension->linkName), pDeviceName);
	if (status != STATUS_SUCCESS) {
		dbg_log("SmartcardCreateLink failed! - status: 0x%08X", status);
		return status;
	}

	pSmartcardExtension->OsData->DeviceObject = pDeviceObject;

	dbg_log("createReaderDevice end - status: 0x%08X", status);
	return status;
}


///////////////////////////////////////////////////////////////////////////////
// Kernel-Mode Driver callback functions(IRP_MJ_XXXXX).
// http://msdn2.microsoft.com/en-us/library/ms806157.aspx
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Entry point for IRP_MJ_CREATE.<br>
 * <br>
 * The operating system sends an IRP_MJ_CREATE request to open a handle
	to a file object or device object.<br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pIrp Caller-supplied pointer to an IRP structure that describes
		the requested I/O operation.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
NTSTATUS VR_Create(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
	dbg_log("VR_Create start");
	return STATUS_SUCCESS;
}

/*!
 * \brief Entry point for IRP_MJ_CLOSE.<br>
 * <br>
 * called when the last handle of the file object that is associated
	with the target device object has been closed and released.<br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pIrp Caller-supplied pointer to an IRP structure that describes
		the requested I/O operation.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
NTSTATUS VR_Close(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
	dbg_log("VR_Close start");
	return STATUS_SUCCESS;
}

/*!
 * \brief Entry point for IRP_MJ_DEVICE_CONTROL.<br>
 * <br>
 * set kernel-mode events for exchanging message with user-mode application.<br>
 *  or <br>
 * pass all IOCTL requests to the SmartcardDeviceControl (WDM) driver library routine.<br>
 * http://msdn2.microsoft.com/en-us/library/ms801443.aspx
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pIrp Caller-supplied pointer to an IRP structure that describes
		the requested I/O operation.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
NTSTATUS VR_IoControl(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
	dbg_log("VR_IoControl start");

	NTSTATUS status = STATUS_NOT_SUPPORTED;
	PDEVICE_EXTENSION pDeviceExtension = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;

	PIO_STACK_LOCATION pIoStackIrp = IoGetCurrentIrpStackLocation(pIrp);
	if (!pIoStackIrp) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto IOCTL_FUNC_END;
	}

	if (pIoStackIrp->Parameters.DeviceIoControl.IoControlCode == IOCTL_JCOP_PROXY_SET_EVENTS) {

		// set events IO control code.

		PJCOP_PROXY_SHARED_EVENTS pEvents;

		dbg_log("IOCTL_SET_EVENTS\n");
		dbg_log(
		    "pIoStackIrp->Parameters.DeviceIoControl.IoControlCode: 0x%08X",
		    pIoStackIrp->Parameters.DeviceIoControl.IoControlCode
		);

		if (pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength < sizeof(JCOP_PROXY_SHARED_EVENTS)) {
			dbg_log("pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength < sizeof(JCOP_PROXY_SHARED_EVENTS)");
			status = STATUS_INVALID_PARAMETER;
			return status;
		}

		pEvents = (PJCOP_PROXY_SHARED_EVENTS)pIrp->AssociatedIrp.SystemBuffer;

		PSMARTCARD_EXTENSION pSmartcardExtension = &pDeviceExtension->smartcardExtension;
		PREADER_EXTENSION pReaderExtension = pSmartcardExtension->ReaderExtension;

		// set kernel-mode event for sending data.
		dbg_log("pEvents->hEventSnd: 0x%08X", pEvents->hEventSnd);
		status = ObReferenceObjectByHandle(pEvents->hEventSnd,
		                                   SYNCHRONIZE,
		                                   *ExEventObjectType,
		                                   pIrp->RequestorMode,
		                                   &pReaderExtension->hEventSnd,
		                                   NULL
		                                  );
		if (status != STATUS_SUCCESS) {
			dbg_log("ObReferenceObjectByHandle failed! - status: 0x%08X", status);
			return status;
		}
		dbg_log("pReaderExtension->hEventSnd: 0x%08X", pReaderExtension->hEventSnd);

		// set user-mode event for reciving data.
		dbg_log("pEvents->hEventRcv: 0x%08X", pEvents->hEventRcv);
		status = ObReferenceObjectByHandle(pEvents->hEventRcv,
		                                   SYNCHRONIZE,
		                                   *ExEventObjectType,
		                                   pIrp->RequestorMode,
		                                   &pReaderExtension->hEventRcv,
		                                   NULL
		                                  );
		if (status != STATUS_SUCCESS) {
			dbg_log("ObReferenceObjectByHandle failed! - status: 0x%08X", status);
			return status;
		}

		dbg_log("pReaderExtension->hEventRcv: 0x%08X", pReaderExtension->hEventRcv);
		status = STATUS_SUCCESS;

		pIrp->IoStatus.Status = status;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	} else {

		// smart card related IO control code.

		PSMARTCARD_EXTENSION pSmartcardExtension = &pDeviceExtension->smartcardExtension;
		switch (pSmartcardExtension->MajorIoControlCode) {
			case IOCTL_SMARTCARD_POWER :
				dbg_log("IOCTL_SMARTCARD_POWER");
				break;
			case IOCTL_SMARTCARD_GET_ATTRIBUTE :
				dbg_log("IOCTL_SMARTCARD_GET_ATTRIBUTE");
				break;
			case IOCTL_SMARTCARD_SET_ATTRIBUTE :
				dbg_log("IOCTL_SMARTCARD_SET_ATTRIBUTE");
				break;
			case IOCTL_SMARTCARD_CONFISCATE :
				dbg_log("IOCTL_SMARTCARD_CONFISCATE");
				break;
			case IOCTL_SMARTCARD_TRANSMIT :
				dbg_log("IOCTL_SMARTCARD_TRANSMIT");
				break;
			case IOCTL_SMARTCARD_EJECT :
				dbg_log("IOCTL_SMARTCARD_EJECT");
				break;
			case IOCTL_SMARTCARD_SWALLOW :
				dbg_log("IOCTL_SMARTCARD_SWALLOW");
				break;
			case IOCTL_SMARTCARD_IS_PRESENT :
				dbg_log("IOCTL_SMARTCARD_IS_PRESENT");
				break;
			case IOCTL_SMARTCARD_IS_ABSENT :
				dbg_log("IOCTL_SMARTCARD_IS_ABSENT");
				break;
			case IOCTL_SMARTCARD_SET_PROTOCOL :
				dbg_log("IOCTL_SMARTCARD_SET_PROTOCOL");
				break;
			case IOCTL_SMARTCARD_GET_STATE :
				dbg_log("IOCTL_SMARTCARD_GET_STATE");
				break;
			case IOCTL_SMARTCARD_GET_LAST_ERROR :
				dbg_log("IOCTL_SMARTCARD_GET_LAST_ERROR");
				break;
			case IOCTL_SMARTCARD_GET_PERF_CNTR :
				dbg_log("IOCTL_SMARTCARD_GET_PERF_CNTR");
				break;
			default :
				dbg_log("IOCTL_XXXXX(unknown): 0x%08X", pSmartcardExtension->MajorIoControlCode);
		}

		// SmartcardAcquireRemoveLock should be called whenever an entry-point
		// routine to the driver is called (for example, whenever the device I/O
		// control routine is called). SmartcardAcquireRemoveLock makes sure that
		// the driver does not unload while other driver code is being executed.
		// http://msdn2.microsoft.com/en-us/library/ms801336.aspx
		status = STATUS_SUCCESS;
		status = SmartcardAcquireRemoveLock(pSmartcardExtension);
		if (status != STATUS_SUCCESS) {
			dbg_log("SmartcardAcquireRemoveLock failed! - status: 0x%08X", status);
			pSmartcardExtension->IoRequest.Information = 0;
			return status;
		}

		// pass all IOCTL requests to the SmartcardDeviceControl (WDM) driver library routine.
		// http://msdn2.microsoft.com/en-us/library/ms801443.aspx
		status = SmartcardDeviceControl(pSmartcardExtension, pIrp);
		dbg_log("SmartcardDeviceControl returned - status: 0x%08X", status);

		SmartcardReleaseRemoveLock(pSmartcardExtension);
	}

IOCTL_FUNC_END:
	dbg_log("VR_IoControl end - status: 0x%08X", status);
	return status;
}


/*!
 * \brief Entry point for IRP_MJ_READ.<br>
 * <br>
 * called when ReadFile is called on the last handle of the file object
	that is associate with the target device.<br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pIrp Caller-supplied pointer to an IRP structure that describes
		the requested I/O operation.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
NTSTATUS VR_ReadBufferedIO(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	dbg_log("VR_ReadBufferedIO start");

	NTSTATUS status = STATUS_BUFFER_TOO_SMALL;
	PIO_STACK_LOCATION pIoStackIrp = NULL;
	PDEVICE_EXTENSION pDeviceExtension = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;

	pIoStackIrp = IoGetCurrentIrpStackLocation(pIrp);
	if (pIoStackIrp == NULL) {
		dbg_log("pIoStackIrp == NULL\r\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// use Buffered I/O.
	PCHAR pReadDataBuffer = (PCHAR)pIrp->AssociatedIrp.SystemBuffer;
	if (pReadDataBuffer == NULL) {
		dbg_log("pReadDataBuffer == NULL\r\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	PSMARTCARD_EXTENSION pSmartcardExtension = &pDeviceExtension->smartcardExtension;
	PREADER_EXTENSION pReaderExtension = pSmartcardExtension->ReaderExtension;

	int dwDataRead = pReaderExtension->iSndLen;
	if ((int)(pIoStackIrp->Parameters.Read.Length) < dwDataRead) {
		dbg_log("pIoStackIrp->Parameters.Read.Length < pReaderExtension->iSndLen\r\n");
		return STATUS_BUFFER_TOO_SMALL;
	}
	dbg_log("pReaderExtension->iSndLen: %d", dwDataRead);

	// copy data from pSndBuffer to user-mode ap's buffer.
	PCHAR pReturnData = pReaderExtension->pSndBuffer;
	RtlCopyMemory(pReadDataBuffer, pReturnData, dwDataRead);
	status = STATUS_SUCCESS;

	pIrp->IoStatus.Status = status;
	pIrp->IoStatus.Information = dwDataRead;

	IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	return status;
}

/*!
 * \brief Entry point for IRP_MJ_WRITE.<br>
 * <br>
 * called when WriteFile is called on the last handle of the file object
	that is associate with the target device.<br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pIrp Caller-supplied pointer to an IRP structure that describes
		the requested I/O operation.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
NTSTATUS VR_WriteBufferedIO(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	dbg_log("VR_WriteBufferedIO start");

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION pIoStackIrp = NULL;
	PDEVICE_EXTENSION pDeviceExtension = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;

	pIoStackIrp = IoGetCurrentIrpStackLocation(pIrp);
	if (pIoStackIrp == NULL) {
		dbg_log("pIoStackIrp == NULL\r\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// use Buffered I/O.
	PCHAR pWriteDataBuffer = (PCHAR)pIrp->AssociatedIrp.SystemBuffer;
	if (pWriteDataBuffer == NULL) {
		dbg_log("pWriteDataBuffer == NULL\r\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	int dwDataWritten = 0;
	if ((int)pIoStackIrp->Parameters.Write.Length > JCOP_PROXY_BUFFER_SIZE) {
		dbg_log("pIoStackIrp->Parameters.Read.Length < pReaderExtension->iSndLen\r\n");
		return STATUS_BUFFER_TOO_SMALL;
	}

	PSMARTCARD_EXTENSION pSmartcardExtension = &pDeviceExtension->smartcardExtension;
	PREADER_EXTENSION pReaderExtension = pSmartcardExtension->ReaderExtension;

	// copy data from user-mode ap's buffer to pRcvBuffer.
	RtlCopyMemory(pReaderExtension->pRcvBuffer, pWriteDataBuffer, pIoStackIrp->Parameters.Write.Length);
	status = STATUS_SUCCESS;
	dwDataWritten = pIoStackIrp->Parameters.Write.Length;
	pReaderExtension->iRcvLen = (unsigned short)pIoStackIrp->Parameters.Write.Length;

	pIrp->IoStatus.Status = status;
	pIrp->IoStatus.Information = dwDataWritten;

	IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	return status;
}

/*!
 * \brief Entry point for this driver's unsuported functions.<br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pIrp Caller-supplied pointer to an IRP structure that describes
		the requested I/O operation.
 *
 * \retval STATUS_NOT_SUPPORTED the status value whitch means that
		this function is not supported.
 */
NTSTATUS VR_UnSupportedFunction(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
	dbg_log("VR_UnSupportedFunction start");
	return STATUS_NOT_SUPPORTED;
}

/*!
 * \brief Entry point for operations before the system unloads the driver.<br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
VOID VR_Unload(IN PDRIVER_OBJECT pDriverObject)
{
	dbg_log("VR_Unload start");

	PDEVICE_EXTENSION pDeviceExtension = (PDEVICE_EXTENSION)pDriverObject->DeviceObject->DeviceExtension;
	PSMARTCARD_EXTENSION pSmartcardExtension = &pDeviceExtension->smartcardExtension;
	PREADER_EXTENSION pReaderExtension = pSmartcardExtension->ReaderExtension;

	// free the send & receive buffer.
	if (pReaderExtension->pSndBuffer != NULL) {
		ExFreePool(pReaderExtension->pSndBuffer);
	}
	if (pReaderExtension->pRcvBuffer != NULL) {
		ExFreePool(pReaderExtension->pRcvBuffer);
	}
	if (pReaderExtension != NULL) {
		ExFreePool(pReaderExtension);
	}

	// free the buffers that were allocated during a call to SmartcardInitialize.
	SmartcardExit(pSmartcardExtension);

	UNICODE_STRING usDosDeviceName;
	RtlInitUnicodeString(&usDosDeviceName, VR_DOS_DEVICE_NAME);
	IoDeleteSymbolicLink(&usDosDeviceName);
	IoDeleteDevice(pDriverObject->DeviceObject);

	// free the smartcard reader name buffer.
	RtlFreeUnicodeString(&(pDeviceExtension->linkName));

	dbg_log("VR_Unload end");
}

/*!
 * \brief Function creates a new device instance.<br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pPhysicalDeviceObject Caller-supplied pointer to a DEVICE_OBJECT
		structure representing a physical device object (PDO) created by a lower-level driver.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
static NTSTATUS addDevice(IN PDRIVER_OBJECT pDriverObject, IN PDEVICE_OBJECT pPhysicalDeviceObject)
{
	dbg_log("addDevice start");

	PDEVICE_OBJECT pDeviceObject = NULL;
	UNICODE_STRING usDeviceName;
	UNICODE_STRING usDosDeviceName;

	RtlInitUnicodeString(&usDeviceName, VR_DEVICE_NAME);
	RtlInitUnicodeString(&usDosDeviceName, VR_DOS_DEVICE_NAME);

	// create device.
	NTSTATUS status = IoCreateDevice(
	                      pDriverObject,
	                      sizeof(DEVICE_EXTENSION), //allocate memory for struct DEVICE_EXTENSION.
	                      &usDeviceName,
	                      FILE_DEVICE_SMARTCARD,
	                      FILE_DEVICE_SECURE_OPEN,
	                      FALSE,
	                      &pDeviceObject
	                  );
	if (status != STATUS_SUCCESS) {
		dbg_log("IoCreateDevice failed! - status: 0x%08X", status);
		return status;
	}

	pDeviceObject->Flags |= DO_BUFFERED_IO;
	pDeviceObject->Flags &= (~DO_DEVICE_INITIALIZING);

	// calling createReaderDevice function.
	status = createReaderDevice(pDeviceObject, &usDeviceName);
	if (status != STATUS_SUCCESS) {
		dbg_log("createReaderDevice failed! - status: 0x%08X", status);
		// error handling is performed at only this point.
		VR_Unload(pDriverObject);
		return status;
	}

	// create symblic link.
	status = IoCreateSymbolicLink(&usDosDeviceName, &usDeviceName);
	if (status != STATUS_SUCCESS) {
		dbg_log("IoCreateSymbolicLink failed! - status: 0x%08X", status);
		// error handling is performed at only this point.
		VR_Unload(pDriverObject);
	}

	dbg_log("addDevice end - status: 0x%08X", status);
	return status;
}

/*!
 * \brief Entry function of the driver.<br>
 * <br>
 * The I/O manager calls the DriverEntry routine when it loads the driver.<br>
 * <br>
 * Supply entry points for the driver's standard routines and
 * create reader device(this driver does not support PnP).<br>
 * <br>
 * \param [in] pDriverObject Caller-supplied pointer to a DRIVER_OBJECT structure.
		This is the driver's driver object.
 * \param [in] pRegistryPath Pointer to a counted Unicode string specifying
		the path to the driver's registry key.
 *
 * \retval STATUS_SUCCESS the routine successfully end.
 */
NTSTATUS DriverEntry(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath)
{
	dbg_log("DriverEntry start");

	// tell the system our entry points
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		pDriverObject->MajorFunction[i] = VR_UnSupportedFunction;
	}
	pDriverObject->MajorFunction[IRP_MJ_CREATE] = VR_Create;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = VR_Close;
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = VR_IoControl;
	pDriverObject->MajorFunction[IRP_MJ_READ] = VR_ReadBufferedIO;
	pDriverObject->MajorFunction[IRP_MJ_WRITE] = VR_WriteBufferedIO;
	pDriverObject->DriverUnload = VR_Unload;

	// this driver does not support PnP.
	// create reader device at this point.
	NTSTATUS status = addDevice(pDriverObject, NULL);

	dbg_log("DriverEntry end - status: 0x%08X", status);
	return status;
}
