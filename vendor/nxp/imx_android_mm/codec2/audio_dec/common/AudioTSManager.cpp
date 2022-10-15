/**
 *  Copyright (c) 2012, Freescale Semiconductor Inc.,
 *  Copyright 2017 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "AudioTSMgr"
#include <log/log.h>

#include <stdio.h>
#include <dlfcn.h>
#include <malloc.h>
#include "AudioTSManager.h"
#include <C2_imx.h>
#include <cutils/properties.h>

AudioTSManager::AudioTSManager()
{
    TS_Queue = nullptr;
    CurrentTS = 0;
    PreTS = -1;
    bHaveTS = false;
    TotalConsumeLen = 0;
    TotalReceivedLen = 0;
    nOneByteTime = 0;
    logLevel = property_get_int32("vendor.media.log.level", 0);
}

AUDIO_TS_MANAGER_ERRORTYPE AudioTSManager::Create()
{
    AUDIO_TS_MANAGER_ERRORTYPE ret = AUDIO_TS_MANAGER_SUCCESS;

    /** Create queue for TS. */
    TS_Queue = new List<TS_ITEM>();
    if (!TS_Queue)
    {
        LOGE("Can't get memory.\n");
        return AUDIO_TS_MANAGER_INSUFFICIENT_RESOURCES;
    }

    CurrentTS = 0;
    PreTS = -1;
    bHaveTS = false;
    TotalConsumeLen = 0;
    TotalReceivedLen = 0;

    return ret;
}

AUDIO_TS_MANAGER_ERRORTYPE AudioTSManager::SetOneByteTime(uint32_t OneByteTime)
{
    AUDIO_TS_MANAGER_ERRORTYPE ret = AUDIO_TS_MANAGER_SUCCESS;
    nOneByteTime = OneByteTime;
    return ret;
}

AUDIO_TS_MANAGER_ERRORTYPE AudioTSManager::Reset()
{
    AUDIO_TS_MANAGER_ERRORTYPE ret = AUDIO_TS_MANAGER_SUCCESS;

    if(TS_Queue){
        while (TS_Queue->GetNodeCnt() > 0)
        {
            TS_ITEM *TS_Item = TS_Queue->GetNode(0);
            if(!TS_Item || TS_Queue->Remove(TS_Item) != LIST_SUCCESS)
            {
                LOGE("Can't remove audio TS item.\n");
                return AUDIO_TS_MANAGER_FAILURE;
            }
            free(TS_Item);
        }
    }

    CurrentTS = 0;
    PreTS = -1;
    bHaveTS = false;
    TotalConsumeLen = 0;
    TotalReceivedLen = 0;

    return ret;
}

AUDIO_TS_MANAGER_ERRORTYPE AudioTSManager::Free()
{
    AUDIO_TS_MANAGER_ERRORTYPE ret = AUDIO_TS_MANAGER_SUCCESS;

    if(TS_Queue != nullptr){
        delete TS_Queue;
        TS_Queue = nullptr;
    }
    LOGD("TS queue deleted\n");

    return ret;
}

AUDIO_TS_MANAGER_ERRORTYPE AudioTSManager::TS_Add(int64_t ts, uint32_t BufferLen)
{
    AUDIO_TS_MANAGER_ERRORTYPE ret = AUDIO_TS_MANAGER_SUCCESS;
    TS_ITEM *TS_Item = nullptr;

    do {
        // Core parser EOS.
        // Core parser will output same audio time stamp if the audio data in
        // same trunk.
        if (ts < 0 \
                || (ts == 0 && BufferLen == 0) \
                || ts == PreTS)
        {
            TotalReceivedLen += BufferLen;
            break;
        }

        TS_Item = (TS_ITEM*)malloc(sizeof(TS_ITEM));

        TS_Item->ts = ts;
        PreTS = ts;
        /** Should add TS first after received buffer from input port */
        TS_Item->begin = TotalReceivedLen;
    
        if(TS_Queue->GetNodeCnt() >= TS_QUEUE_SIZE){
            LOGE("Queue overflow, can't add TS item, max queue size: %d \n", TS_QUEUE_SIZE);
            ret = AUDIO_TS_MANAGER_FAILURE;
            break;
        }

        if (TS_Queue->Add(TS_Item) != LIST_SUCCESS) {
            LOGE("Can't add TS item to audio ts queue. \n");
            ret = AUDIO_TS_MANAGER_FAILURE;
            break;
        }

        TotalReceivedLen += BufferLen;
        LOGV("item(ts: %lld, begin: %lld), BufferLen %d, TotalReceivedLen %lld\n", (long long)TS_Item->ts, 
            (long long)TS_Item->begin, BufferLen,(long long)TotalReceivedLen);
        
    }while (0);

    if(ret != AUDIO_TS_MANAGER_SUCCESS && TS_Item)
        free(TS_Item);

    return ret;
}

AUDIO_TS_MANAGER_ERRORTYPE AudioTSManager::TS_Get(int64_t *ts)
{
    AUDIO_TS_MANAGER_ERRORTYPE ret = AUDIO_TS_MANAGER_SUCCESS;
    *ts = CurrentTS;
    LOGV("Get CurrentTS = %lld\n", (long long)CurrentTS);
    return ret;
}

AUDIO_TS_MANAGER_ERRORTYPE AudioTSManager::TS_SetIncrease(int64_t ts)
{
    AUDIO_TS_MANAGER_ERRORTYPE ret = AUDIO_TS_MANAGER_SUCCESS;
    if (bHaveTS == false) {
        CurrentTS += ts;
        LOGV("ts %lld, update CurrentTS to %lld\n", (long long)ts, (long long)CurrentTS);
    }
    else{
        LOGV("ts %lld, do nothing\n", (long long)ts);
    }
    bHaveTS = false;
    return ret;
}

uint32_t AudioTSManager::GetFrameLen()
{
    TS_ITEM *TS_Item = TS_Queue->GetNode(0);
    uint32_t nFrameLen = 0;

    if (!TS_Item) {
        LOGV("Can't get first audio TS item.\n");
        return TotalReceivedLen - TotalConsumeLen;
    }

    nFrameLen = TS_Item->begin - TotalConsumeLen;
    LOGV("item(ts: %lld, begin: %lld), TotalConsumeLen: %lld => nFrameLen %d\n", 
        (long long)TS_Item->ts, (long long)TS_Item->begin, (long long)TotalConsumeLen, nFrameLen);

    //free(TS_Item);
    return nFrameLen;
}

AUDIO_TS_MANAGER_ERRORTYPE AudioTSManager::Consumed(uint32_t ConsumedLen)
{
        AUDIO_TS_MANAGER_ERRORTYPE ret = AUDIO_TS_MANAGER_SUCCESS;

	if (TotalReceivedLen < TotalConsumeLen + ConsumedLen) {
		LOGE("audio ts manager consumer point set error.\n");
		return AUDIO_TS_MANAGER_FAILURE;
	}

	TotalConsumeLen += ConsumedLen;
        LOGV("len %d, TotalConsumeLen %lld", ConsumedLen, (long long)TotalConsumeLen);

	if (TS_Queue->GetNodeCnt() < 1){
            LOGV("no ts item!");
            return ret;
       }

	/** Adjust current TS */
	TS_ITEM *TS_Item;

	while (1) {
            TS_Item = TS_Queue->GetNode(0);
            if (!TS_Item) {
                LOGV("Can't get first audio TS item.\n");
                return AUDIO_TS_MANAGER_SUCCESS;
            }

            if (TotalConsumeLen >= TS_Item->begin) {
                CurrentTS = TS_Item->ts;
                if (nOneByteTime) {
                    CurrentTS += (TotalConsumeLen - TS_Item->begin) \
                                 * nOneByteTime;
                    LOGV("update CurrentTS to %lld by nOneByteTime", (long long)CurrentTS);
                }
                bHaveTS = true;

                LOGV("bHaveTS, remove item(ts(CurrentTS) %lld, begin %lld)",(long long)TS_Item->ts,(long long)TS_Item->begin);

                if (TS_Queue->Remove(TS_Item) != LIST_SUCCESS) {
                    LOGE("Can't get audio TS item.\n");
                    return AUDIO_TS_MANAGER_FAILURE;
                }
                else
                    free(TS_Item);
            }
            else
                break;
	}

	return ret;
}

/* File EOF */
