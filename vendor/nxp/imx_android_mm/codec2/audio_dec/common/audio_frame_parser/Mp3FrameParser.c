/**
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "Mp3FrameParser.h"
//bit rate table
int16_t mp3_bit_rate[16][6]= {
    /* bitrate in Kbps
    * MPEG-1 layer 1, 2, 3; MPEG-2 layer 1,2,3; MPEG-2.5 layer 1,2,3
    */
    {0,      0,	    0, 	    0,	    0,	    0},
    {32,     32,	    32,	    32,	    8,	    8},
    {64,	    48,	    40,	    48,	    16,	    16},
    {96,	    56,	    48,	    56,	    24,	    24},
    {128,    64,	    56,	    64,	    32,	    32},
    {160,    80,	    64, 	80,	    40,	    40},
    {192,	96,	    80, 	96,	    48,	    48},
    {224,	112,	96,	    112,	56,	    56},
    {256,	128,	112,	128,	64,	    64},
    {288,	160,	128,	144,	80,	    80},
    {320,	192,	160,	160,	96,	    96},
    {352,	224,	192,	176,	112,	112},
    {384,	256,	224,    192,	128,	128},
    {416,	320,	256,    224,	144,	144},
    {448,	384,	320,    256,	160,	160},
    {999,	999,	999,    999,	999,	999}
};

//sampling rate table
uint32_t mp3_sampling_rate[4][3] = {
    {44100, 22050, 11025},
    {48000, 24000, 12000},
    {32000, 16000, 8000},
    {99999, 99999, 99999}
};

//sample per frame table
uint32_t mp3_sample_per_frame[3][3] = {
    {384,   384,       84},
    {1152,  1152,   1152},
    {1152,  576,    576}
};
static eimx_linux_bool IsMP3FrameHeader(uint8_t * pHeader,FRAME_INFO * Info)
{
    int32_t mpeg_version;
    int32_t layer;
    int32_t bit_rate_index;
    int32_t bit_rate;
    int32_t sampling_frequency;
    int32_t sampling_frequency_index;
    int32_t padding;
    int32_t private_bit;
    int32_t channel_mode;
    int32_t frame_size;
    int32_t channel_num;
    int32_t has_crc = 0;

    if(pHeader == NULL || Info == NULL)
        return E_IMX_FALSE;

    if(pHeader[0] !=0xFF || (pHeader[1] & 0xE0) != 0xE0)
        return E_IMX_FALSE;

    mpeg_version = ((int32_t)pHeader[1]&0x18)>>3;

    if(mpeg_version == 3){
        mpeg_version = 0;  /* mpeg ver 1 */
    }else if(mpeg_version == 2){
        mpeg_version = 1;  /* mpeg ver 2 */
    }
    else if(mpeg_version == 0){
        mpeg_version = 2;  /* mpeg ver 2.5 */
    }

    layer  = 4 - (((int32_t)pHeader[1]&0x06)>>1);

    if (( 3!= layer) && (2 != layer) && (1 != layer))  {
        return E_IMX_FALSE;
    }

    Info->version = mpeg_version;
    Info->layer = layer;

    /* has crc ? */
    has_crc = !(((uint8_t) pHeader[1]) & 0x01);

    bit_rate_index = ((int32_t)pHeader[2]&0xF0)>>4;

    if(mpeg_version == 0)
        bit_rate = mp3_bit_rate[bit_rate_index][mpeg_version+layer-1];
    else
        bit_rate = mp3_bit_rate[bit_rate_index][3+layer-1];

    Info->b_rate = bit_rate;

    if (bit_rate > 448 || bit_rate == 0)
    {
        return E_IMX_FALSE;
    }

    sampling_frequency_index =((int32_t)pHeader[2]&0x0C)>>2;
    sampling_frequency  = mp3_sampling_rate[sampling_frequency_index][mpeg_version];

    Info->sampling_rate = sampling_frequency;
    Info->sample_per_fr = mp3_sample_per_frame[layer-1][mpeg_version];

    padding = ((int32_t)pHeader[2]&0x02)>>1;

    private_bit = ((int32_t)pHeader[2]&0x01);

    channel_mode = ((int32_t)pHeader[3]&0xC0)>>6;

    if(layer ==1)
        frame_size = (int32_t)((12*bit_rate*1000)/sampling_frequency+ padding)*4;
    else
        frame_size = (int32_t)((Info->sample_per_fr/8*bit_rate*1000)/sampling_frequency)+ padding;

    Info->frm_size = frame_size ;

    if (3 == channel_mode)
    {
        channel_num = 1;
    }
    else
    {
        channel_num = 2;
    }

    Info->channels = channel_num;

    return E_IMX_TRUE;
}

AFP_RETURN Mp3CheckFrame(AUDIO_FRAME_INFO *pFrameInfo, uint8_t *pBuffer, uint32_t nBufferLen)
{
    return CheckFrame(pFrameInfo,pBuffer,nBufferLen,MP3_FRAME_HEAD_SIZE,IsMP3FrameHeader);
}