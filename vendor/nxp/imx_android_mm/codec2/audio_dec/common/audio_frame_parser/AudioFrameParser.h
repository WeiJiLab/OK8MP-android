/**
 *  Copyright (c) 2009-2015, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductors Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#ifndef AudioFrameParser_h
#define AudioFrameParser_h

#ifdef __cplusplus
extern "C" {
#endif

#include <unistd.h>

//audio frame parser return value
#define AFP_RETURN int32_t
#define AFP_SUCCESS 0
#define AFP_FAILED  1

/*!	boolean type*/
typedef enum eimx_linux_bool
{
	E_IMX_FALSE = 0,
	E_IMX_TRUE
}eimx_linux_bool;


//audio frame information
typedef struct AUDIO_FRAME_INFO {
    eimx_linux_bool bGotOneFrame;//true when get one frame and next frame's header
    uint32_t nHeaderCount;
    uint32_t nConsumedOffset;//frame header offset if get one frame
    uint32_t nFrameSize;//frame size
    uint32_t nBitRate;
    uint32_t nSamplesPerFrame;
    uint32_t nSamplingRate;
    uint32_t nChannels;
    uint32_t nNextFrameSize;//frame size
    uint32_t nHeaderSize;
} AUDIO_FRAME_INFO;

typedef struct FRAME_INFO
{
    uint32_t frm_size;
    uint32_t b_rate;
    uint32_t sampling_rate;
    uint32_t sample_per_fr;
    uint32_t layer;
    uint32_t version;
    uint32_t channels;
    uint32_t header_size;
}FRAME_INFO;

typedef eimx_linux_bool (*IsValidHeader)(uint8_t * pHeader,FRAME_INFO * Info);

AFP_RETURN CheckFrame(AUDIO_FRAME_INFO *pFrameInfo, uint8_t *pBuffer, uint32_t nBufferLen,uint32_t nHeadSize,IsValidHeader fpIsValidHeader);

#ifdef __cplusplus
}
#endif

#endif//AudioFrameParser_h
