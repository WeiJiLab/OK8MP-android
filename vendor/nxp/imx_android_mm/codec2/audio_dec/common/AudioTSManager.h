/**
 *  Copyright (c) 2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/**
 *  @file AudioTSManager.h
 *  @brief Class definition of audio time stamp manager.
 *  @ingroup AudioTSManager
 */


#ifndef AudioTSManager_h
#define AudioTSManager_h

#define TS_QUEUE_SIZE (1024*20)

#include <List.h>

typedef enum {
    AUDIO_TS_MANAGER_SUCCESS,
    AUDIO_TS_MANAGER_FAILURE,
    AUDIO_TS_MANAGER_INSUFFICIENT_RESOURCES
}AUDIO_TS_MANAGER_ERRORTYPE;

typedef struct _TS_ITEM{
    int64_t ts;
    int64_t begin;
}TS_ITEM;

class AudioTSManager {
	public:
		AudioTSManager();
		AUDIO_TS_MANAGER_ERRORTYPE Create();
        AUDIO_TS_MANAGER_ERRORTYPE SetOneByteTime(uint32_t OneByteTime);
        AUDIO_TS_MANAGER_ERRORTYPE Free();
		AUDIO_TS_MANAGER_ERRORTYPE Reset();
		AUDIO_TS_MANAGER_ERRORTYPE TS_Add(int64_t ts, uint32_t BufferLen);
		/** Set TS increase after decode one of audio data */
		AUDIO_TS_MANAGER_ERRORTYPE TS_SetIncrease(int64_t ts);
		/** Get TS for output audio data of audio decoder. The output TS is calculated for
		 every frame of audio data */
		AUDIO_TS_MANAGER_ERRORTYPE TS_Get(int64_t *ts);
		uint32_t GetFrameLen();
		/** Set consumered point for ring buffer. */
		AUDIO_TS_MANAGER_ERRORTYPE Consumed(uint32_t ConsumeredLen);
	private:
		List<TS_ITEM> *TS_Queue;
		int64_t CurrentTS;
		int64_t PreTS;
		bool bHaveTS;
		int64_t TotalConsumeLen;
		int64_t TotalReceivedLen;
		uint32_t nOneByteTime;
		int32_t logLevel;
};

#endif
/* File EOF */
