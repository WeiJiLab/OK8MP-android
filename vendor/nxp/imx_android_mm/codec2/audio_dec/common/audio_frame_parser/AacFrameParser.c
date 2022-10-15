/**
 *  Copyright (c) 2010-2015, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "AacFrameParser.h"

static const uint32_t aac_sampling_frequency[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000};
#define AAC_SAMPLERATE_TABLE_SIZE (uint32_t)(sizeof(aac_sampling_frequency)/sizeof(aac_sampling_frequency[0]))
#define AAC_FRAME_SIZE 1024
static eimx_linux_bool IsADTSFrameHeader(uint8_t * pHeader,FRAME_INFO * Info)
{
    int32_t id;
    int32_t layer;
    int32_t rotection_abs = 0;
    int32_t profile;
    int32_t sampling_freq_idx;
    int32_t private_bit;
    int32_t channel_config;
    int32_t original_copy;
    int32_t home;
    int32_t copyright_id_bit;
    int32_t copyright_id_start;
    int32_t frame_length;
    int32_t adts_buffer_fullness;
    int32_t num_of_rdb;
    int32_t protection_abs;

    if(pHeader == NULL || Info == NULL)
        return E_IMX_FALSE;

    if(pHeader[0] !=0xFF || (pHeader[1] & 0xF0) != 0xF0)
        return E_IMX_FALSE;

    id = ((int32_t)pHeader[1]&0x08)>>3;
    layer = ((int32_t)pHeader[1]&0x06)>>1;
    protection_abs = (int32_t)pHeader[1]&0x01;

    if(layer != 0){
        return E_IMX_FALSE;
    }

    profile = ((int32_t)pHeader[2]&0xC0) >> 6;
    //do not check the profile level, decoder will check it.
   // if(profile != 1){
    //    return E_IMX_FALSE;
   // }

    sampling_freq_idx = ((int32_t)pHeader[2]&0x3C) >> 2;
    if (sampling_freq_idx >= 0xc){
        return E_IMX_FALSE;
    }

    private_bit = ((int32_t)pHeader[2]&0x02) >> 1;
    channel_config = (((int32_t)pHeader[2]&0x01) << 2) + (((int32_t)pHeader[3]&0xC0) >> 6);
    original_copy = ((int32_t)pHeader[3]&0x20) >> 5;
    home = ((int32_t)pHeader[3]&0x10) >> 4;

    copyright_id_bit = ((int32_t)pHeader[3]&0x08) >> 3;
    copyright_id_start = ((int32_t)pHeader[3]&0x04) >> 2;
    frame_length = (((int32_t)pHeader[3]&0x03) << 11) + (((int32_t)pHeader[4]) << 3) + (((int32_t)pHeader[5]&0xE0) >> 5);

    adts_buffer_fullness = (((int32_t)pHeader[5]&0x1F) << 6) + (((int32_t)pHeader[6]&0xFC) >> 2);
    num_of_rdb = ((int32_t)pHeader[6]&0x03);

    Info->channels = channel_config;

    //ref to 13818-7AAC (2).pdf, Table 42 ¡ª Channel Configuration
    if(Info->channels == 7)
    {
        Info->channels = 8;
    }
    else if(Info->channels == 0) //if 0, should parsed in raw block, so just set 1
    {
        Info->channels = 1;
    }

    if((uint32_t)sampling_freq_idx < AAC_SAMPLERATE_TABLE_SIZE)
    {
        Info->sampling_rate = aac_sampling_frequency[sampling_freq_idx];
    }

    Info->sample_per_fr = AAC_FRAME_SIZE;
    Info->b_rate = Info->sampling_rate / Info->sample_per_fr * (frame_length << 3);

    Info->frm_size = frame_length;

    if(1 == protection_abs)
        Info->header_size = 7;
    else
        Info->header_size = 9;

    return E_IMX_TRUE;
}
AFP_RETURN AacCheckFrame(AUDIO_FRAME_INFO *pFrameInfo, uint8_t *pBuffer, uint32_t nBufferLen)
{
    return CheckFrame(pFrameInfo,pBuffer,nBufferLen,AAC_FRAME_HEAD_SIZE,IsADTSFrameHeader);
}
