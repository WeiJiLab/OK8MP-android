/**
 *  Copyright 2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "AudFrmPars"

#include "AudioFrameParser.h"
#include <malloc.h>
#include <log/log.h>
#include <string.h>

AFP_RETURN CheckFrame(AUDIO_FRAME_INFO *pFrameInfo, uint8_t *pBuffer, uint32_t nBufferLen,uint32_t nHeadSize,IsValidHeader fpIsValidHeader)
{
    AFP_RETURN ret = AFP_SUCCESS;

    uint32_t nOffset = 0;
    uint32_t nFrameOffset = 0;
    uint32_t nFirstFrameSize = 0;
    uint32_t nFirstHeaderSize = 0;
    uint32_t nHeaderCount = 0;
    FRAME_INFO Info;
    uint8_t* pHeader = NULL;

    if(!pFrameInfo || !pBuffer){
        return AFP_FAILED;
    }

    memset(pFrameInfo, 0, sizeof(AUDIO_FRAME_INFO));

    pFrameInfo->bGotOneFrame = false;
    pFrameInfo->nHeaderCount = 0;
    pFrameInfo->nConsumedOffset = 0;
    pFrameInfo->nFrameSize = 0;
    pFrameInfo->nBitRate = 0;
    pFrameInfo->nSamplesPerFrame = 0;

    memset(&Info, 0x0, sizeof(FRAME_INFO));
    ALOGV("CheckFrame start nBufferLen=%d",nBufferLen);

    while(nOffset + nHeadSize <= nBufferLen){

        pHeader = pBuffer + nOffset;

        if(!fpIsValidHeader(pHeader,&Info)){
            nOffset++;
            continue;
        }
        //check the frame size
        if(0 == Info.frm_size){
            nOffset++;
            continue;
        }

        nHeaderCount ++;
        nFrameOffset = nOffset;
        nFirstFrameSize = Info.frm_size;
        nFirstHeaderSize = Info.header_size;

        ALOGV("CheckFrame nOffset=%d,frm_size=%d",nOffset,Info.frm_size);

        if(Info.frm_size + nOffset == nBufferLen){
            printf("CheckFrame frame size=%d,buffer len=%d",Info.frm_size,nBufferLen);
            pFrameInfo->bGotOneFrame = true;
            pFrameInfo->nNextFrameSize = 0;
            break;
        }else if(Info.frm_size + nOffset + nHeadSize < nBufferLen){
            nOffset += Info.frm_size;
        }else{
            nOffset++;
            continue;
        }

        pHeader = pBuffer + nOffset;

        if(fpIsValidHeader(pHeader,&Info)){
            pFrameInfo->bGotOneFrame = true;
            pFrameInfo->nNextFrameSize = Info.frm_size;
            nHeaderCount ++;
            break;
        }else{
            nOffset = nFrameOffset+1;
            continue;
        }
    }

    if(nFirstFrameSize > nHeadSize){
        pFrameInfo->nFrameSize = nFirstFrameSize;
        pFrameInfo->nHeaderSize = nFirstHeaderSize;
    }else{
        pFrameInfo->nFrameSize = 0;
    }

    if(nHeaderCount > 0){
        pFrameInfo->nConsumedOffset = nFrameOffset;
    }else{
        pFrameInfo->nConsumedOffset = nBufferLen;
        pFrameInfo->nFrameSize = 0;
    }

    pFrameInfo->nBitRate = Info.b_rate;
    pFrameInfo->nSamplesPerFrame = Info.sample_per_fr;
    pFrameInfo->nSamplingRate = Info.sampling_rate;
    pFrameInfo->nChannels = Info.channels;
    pFrameInfo->nHeaderCount = nHeaderCount;
    ALOGV("CheckFrame bGotOneFrame=%d,nFrameCount=%d,Consumed=%d,nFrameSize=%d,\
samplerate=%d,bitrate=%d,channel=%d,samplePerFrame=%d",
        pFrameInfo->bGotOneFrame,
        pFrameInfo->nHeaderCount,
        pFrameInfo->nConsumedOffset,
        pFrameInfo->nFrameSize,
        pFrameInfo->nSamplingRate,
        pFrameInfo->nBitRate,
        pFrameInfo->nChannels,
        pFrameInfo->nSamplesPerFrame
    );

    return ret;
}
