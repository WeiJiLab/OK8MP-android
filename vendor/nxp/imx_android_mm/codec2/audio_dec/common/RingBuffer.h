/**
 *  Copyright (c) 2009-2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/**
 *  @file RingBuffer.h
 *  @brief Class definition of ring buffer
 *  @ingroup RingBuffer
 */


#ifndef RingBuffer_h
#define RingBuffer_h

#define RING_BUFFER_SCALE 5

typedef enum {
    RINGBUFFER_SUCCESS,
    RINGBUFFER_FAILURE,
    RINGBUFFER_INSUFFICIENT_RESOURCES
}RINGBUFFER_ERRORTYPE;

class RingBuffer {
	public:
		RingBuffer();
		RINGBUFFER_ERRORTYPE BufferCreate(uint32_t nPushModeLen, uint32_t nRingBufferScale = RING_BUFFER_SCALE);
        RINGBUFFER_ERRORTYPE BufferFree();
		RINGBUFFER_ERRORTYPE BufferReset();
		RINGBUFFER_ERRORTYPE BufferAdd(uint8_t *pBuffer, uint32_t BufferLen, uint32_t *pActualLen);
		RINGBUFFER_ERRORTYPE BufferAddZeros(uint32_t BufferLen, uint32_t *pActualLen);
		uint32_t AudioDataLen();
		RINGBUFFER_ERRORTYPE BufferGet(uint8_t **ppBuffer, uint32_t BufferLen, uint32_t *pActualLen);
		/** Set consumered point for ring buffer. */
		RINGBUFFER_ERRORTYPE BufferConsumed(uint32_t ConsumedLen);
		RINGBUFFER_ERRORTYPE Resize(uint32_t nLength);
		uint32_t nPrevOffset;
	private:
		uint32_t nPushModeInputLen;
		int64_t TotalConsumeLen;
        uint32_t nOneByteTime;
		uint8_t *RingBufferPtr;
		uint32_t nRingBufferLen; /**< Should at least RING_BUFFER_SCALE * PUSH model input buffer length */
		uint8_t *Reserved;
		uint32_t ReservedLen;   /**< 1/RING_BUFFER_SCALE length of ring buffer length,
								 used for the end of ring buffer data */
		uint8_t *Begin;
		uint8_t *End;
		uint8_t *Consumered;
		int32_t logLevel;
};

#endif
/* File EOF */
