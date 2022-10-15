/**
 *  Copyright (c) 2009-2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "RingBuffer"
#include <log/log.h>

#include <stdio.h>
#include <dlfcn.h>
#include "RingBuffer.h"
#include <malloc.h>
#include <string.h>
#include <C2_imx.h>
#include <cutils/properties.h>

RingBuffer::RingBuffer()
{
	RingBufferPtr = NULL;
	Reserved = NULL;
	TotalConsumeLen = 0;
	nPrevOffset = 0;
	nPushModeInputLen = 0;
	nOneByteTime = 0;
	nRingBufferLen = 0;
	ReservedLen = 0;
	Begin = NULL;
	End = NULL;
	Consumered = NULL;
	logLevel = property_get_int32("vendor.media.log.level", 0);
}

RINGBUFFER_ERRORTYPE RingBuffer::BufferCreate(uint32_t nPushModeLen, uint32_t nRingBufferScale)
{
    RINGBUFFER_ERRORTYPE ret = RINGBUFFER_SUCCESS;

	nPushModeInputLen = nPushModeLen;
	nRingBufferLen = nPushModeInputLen * nRingBufferScale;

	/** Create ring buffer for audio decoder input stream. */
	LOGD("Ring buffer len: %d\n", nRingBufferLen);
	RingBufferPtr = (uint8_t *)malloc(nRingBufferLen+8);
	if (RingBufferPtr == NULL)
	{
		LOGE("Can't get memory.\n");
		return RINGBUFFER_INSUFFICIENT_RESOURCES;
	}

	Reserved = (uint8_t *)malloc(nPushModeInputLen);
	if (Reserved == NULL)
	{
		free(RingBufferPtr);
		LOGE("Can't get memory.\n");
		return RINGBUFFER_INSUFFICIENT_RESOURCES;
	}

	TotalConsumeLen = 0;
	ReservedLen = nPushModeInputLen;
	Begin = RingBufferPtr;
	End = RingBufferPtr;
	Consumered = RingBufferPtr;
	nPrevOffset = 0;

    return ret;
}

RINGBUFFER_ERRORTYPE RingBuffer::Resize(uint32_t nLength)
{

    uint32_t beginOffset = Begin - RingBufferPtr;
    uint32_t endOffset = End - RingBufferPtr;

	uint8_t *RingBufferPtr2 = (uint8_t *)malloc(nLength+8);
	if (RingBufferPtr2 == NULL)
	{
		LOGE("Can't get memory.\n");
		return RINGBUFFER_INSUFFICIENT_RESOURCES;
	}
    if(Begin > End)
        memcpy(RingBufferPtr2, End, AudioDataLen());
    else{
        uint32_t secondSegment = RingBufferPtr + nRingBufferLen - End;
        memcpy(RingBufferPtr2, End, secondSegment);
        memcpy(RingBufferPtr2 + secondSegment, RingBufferPtr, Begin - RingBufferPtr);
    }
    Begin = RingBufferPtr2 + AudioDataLen();
    End = RingBufferPtr2;
    nRingBufferLen = nLength+8;

    free(RingBufferPtr);
    RingBufferPtr = RingBufferPtr2;
    LOGI("new length %d", nLength);

    return RINGBUFFER_SUCCESS;
}

RINGBUFFER_ERRORTYPE RingBuffer::BufferReset()
{
	RINGBUFFER_ERRORTYPE ret = RINGBUFFER_SUCCESS;

	TotalConsumeLen = 0;
	Begin = RingBufferPtr;
	End = RingBufferPtr;
	Consumered = RingBufferPtr;
	nPrevOffset = 0;

	return ret;
}

RINGBUFFER_ERRORTYPE RingBuffer::BufferFree()
{
	RINGBUFFER_ERRORTYPE ret = RINGBUFFER_SUCCESS;

    if(RingBufferPtr != NULL){
        free(RingBufferPtr);
        RingBufferPtr = NULL;
    }

    if(Reserved != NULL){
        free(Reserved);
        Reserved = NULL;
    }

	return ret;
}

RINGBUFFER_ERRORTYPE RingBuffer::BufferAdd(uint8_t *pBuffer, uint32_t BufferLen, uint32_t *pActualLen)
{
    RINGBUFFER_ERRORTYPE ret = RINGBUFFER_SUCCESS;

	int32_t DataLen = AudioDataLen();
	int32_t FreeBufferLen = nRingBufferLen - DataLen - 1;
	if (FreeBufferLen < (int32_t)BufferLen)
	{
		*pActualLen = FreeBufferLen;
	}
	else
	{
		*pActualLen = BufferLen;
	}

	if (Begin + *pActualLen > RingBufferPtr + nRingBufferLen)
	{
		uint32_t FirstSegmentLen = nRingBufferLen - (Begin - RingBufferPtr);
		memcpy(Begin, pBuffer, FirstSegmentLen);
		memcpy(RingBufferPtr, pBuffer + FirstSegmentLen, *pActualLen - FirstSegmentLen);
		Begin = RingBufferPtr + *pActualLen - FirstSegmentLen;
	}
	else
	{
		memcpy(Begin, pBuffer, *pActualLen);
		Begin += *pActualLen;
	}

	LOGV("nRingBufferLen = %d\t DataLen = %d\n", nRingBufferLen, DataLen);

    return ret;
}

RINGBUFFER_ERRORTYPE RingBuffer::BufferAddZeros(uint32_t BufferLen, uint32_t *pActualLen)
{
    RINGBUFFER_ERRORTYPE ret = RINGBUFFER_SUCCESS;

	uint32_t DataLen = AudioDataLen();
	uint32_t FreeBufferLen = nRingBufferLen - DataLen - 1;
	if (FreeBufferLen < BufferLen)
	{
		*pActualLen = FreeBufferLen;
	}
	else
	{
		*pActualLen = BufferLen;
	}

	if (Begin + *pActualLen > RingBufferPtr + nRingBufferLen)
	{
		uint32_t FirstSegmentLen = nRingBufferLen - (Begin - RingBufferPtr);
		memset(Begin, 0, FirstSegmentLen);
		memset(RingBufferPtr, 0, *pActualLen - FirstSegmentLen);
		Begin = RingBufferPtr + *pActualLen - FirstSegmentLen;
	}
	else
	{
		memset(Begin, 0, *pActualLen);
		Begin += *pActualLen;
	}

	LOGV("nRingBufferLen = %d\t DataLen = %d\n", nRingBufferLen, DataLen);

    return ret;
}


uint32_t RingBuffer::AudioDataLen()
{
	int32_t DataLen = Begin - End;
	if (DataLen < 0)
	{
		DataLen = nRingBufferLen - (End - Begin);
	}

    return (uint32_t)DataLen;
}

RINGBUFFER_ERRORTYPE RingBuffer::BufferGet(uint8_t **ppBuffer, uint32_t BufferLen, uint32_t *pActualLen)
{
    RINGBUFFER_ERRORTYPE ret = RINGBUFFER_SUCCESS;
	int32_t DataLen = AudioDataLen();
	if (DataLen < (int32_t)BufferLen)
	{
		*pActualLen = DataLen;
	}
	else
	{
		*pActualLen = BufferLen;
	}
	if (ReservedLen < *pActualLen)
	{
		LOGW("Reserved buffer is too short.\n");
		*pActualLen = ReservedLen;
	}

	if (End + *pActualLen > RingBufferPtr + nRingBufferLen)
	{
		uint32_t FirstSegmentLen = nRingBufferLen - (End - RingBufferPtr);
		memcpy(Reserved, End, FirstSegmentLen);
		memcpy(Reserved + FirstSegmentLen, RingBufferPtr, *pActualLen - FirstSegmentLen);
		*ppBuffer = Reserved;
	}
	else
	{
		*ppBuffer = End;
	}

	LOGV("nRingBufferLen = %d\t DataLen = %d\n", nRingBufferLen, DataLen);
    return ret;
}

RINGBUFFER_ERRORTYPE RingBuffer::BufferConsumed(uint32_t ConsumedLen)
{
    RINGBUFFER_ERRORTYPE ret = RINGBUFFER_SUCCESS;
	int32_t DataLen = AudioDataLen();
	if (DataLen < (int32_t)ConsumedLen)
	{
		LOGE("Ring buffer consumer point set error.\n");
		return RINGBUFFER_FAILURE;
	}

	if (End + ConsumedLen > RingBufferPtr + nRingBufferLen)
	{
		End -= nRingBufferLen - ConsumedLen;
	}
	else
	{
		End += ConsumedLen;
	}

	TotalConsumeLen += ConsumedLen;
	LOGV("nRingBufferLen = %d\t DataLen = %d\n", nRingBufferLen, DataLen);
	return ret;
}

/* File EOF */
