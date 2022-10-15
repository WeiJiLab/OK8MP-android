/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#include "FrameConverter.h"

//#define LOG_NDEBUG 0
#define LOG_TAG "FrameConverter"
#include <linux/videodev2.h>

namespace android {
status_t FrameConverter::Create(uint32_t format)
{
    if(format != V4L2_PIX_FMT_H264)
        return UNKNOWN_ERROR;
    nFormat = format;
    pSpsPps = NULL;
    nLen = 0;
    return OK;
}
status_t FrameConverter::ConvertToCodecData(
    uint8_t* pInData, uint32_t nSize, uint8_t* pOutData,uint32_t*nOutSize,uint32_t * nConsumeLen)
{
    uint8_t* pPre=pInData;
    uint32_t spsSize=0, ppsSize=0;
    uint8_t *sps=NULL, *pps=NULL;
    uint8_t* pNext=NULL;
    uint8_t naltype;
    int32_t length=(int32_t)nSize;
    uint8_t* pTemp=NULL,*pFilled=NULL;
    uint32_t skipLen=0;

    if(pInData == NULL || pOutData == NULL || nOutSize == NULL)
        return UNKNOWN_ERROR;

    /*search boundary of sps and pps */
    if(false==FindStartCode(pInData,length,&pPre)){
        goto search_finish;
    }

    pPre+=4; //skip 4 bytes of startcode
    length-=(pPre-pInData);

    if(length<=0){
        goto search_finish;
    }

    while(1){
        int size;

        if(FindStartCode(pPre,length,&pNext)){
            size=pNext-pPre;
        }
        else{
            size=length; //last nal
        }
        naltype=pPre[0] & 0x1f;
        ALOGD("find one nal, type: 0x%x, size: %d \r\n",naltype,size);
        if (naltype==7) { /* SPS */
            sps=pPre;
            spsSize=size;
        }
        else if (naltype==8) { /* PPS */
            pps= pPre;
            ppsSize=size;
        }else if(naltype == 0x0c){//skip coda padding bytes
            skipLen += 4+size;
        }
        
        if(pNext==NULL){
            goto search_finish;
        }
        pNext+=4;
        length-=(pNext-pPre);
        if(length<=0){
        	goto search_finish;
        }
        pPre=pNext;
    }
search_finish:
    if((sps==NULL)||(pps==NULL)){
        return UNKNOWN_ERROR;
    }

    pFilled=pOutData;
    pFilled[0]=1;		/* version */
    pFilled[1]=sps[1];	/* profile */
    pFilled[2]=sps[2];	/* profile compat */
    pFilled[3]=sps[3];	/* level */
    pFilled[4]=0xFF;	/* 6 bits reserved (111111) + 2 bits nal size length - 1 (11) */
    pFilled[5]=0xE1;	/* 3 bits reserved (111) + 5 bits number of sps (00001) */
    pFilled+=6;

    pFilled[0]=(spsSize>>8)&0xFF; /*sps size*/
    pFilled[1]=spsSize&0xFF;
    pFilled+=2;
    memcpy(pFilled,sps,spsSize); /*sps data*/
    pFilled+=spsSize;

    pFilled[0]=1;		/* number of pps */
    pFilled++;
    pFilled[0]=(ppsSize>>8)&0xFF;	/*pps size*/
    pFilled[1]=ppsSize&0xFF;
    pFilled+=2;
    memcpy(pFilled,pps,ppsSize); /*pps data*/
    *nOutSize=6+2+spsSize+1+2+ppsSize;
    *nConsumeLen = 4+4+spsSize+ppsSize+skipLen;

    if(pSpsPps != NULL)
        free(pSpsPps);
    if(nSize > (*nConsumeLen))
        nLen = (*nConsumeLen);
    else
        return UNKNOWN_ERROR;
    pSpsPps = (uint8_t*)malloc(nLen);
    memcpy(pSpsPps,pInData,nLen); /*sps data*/

    return OK;
}
status_t FrameConverter::CheckSpsPps(uint8_t* pInData, uint32_t nSize, uint32_t* nConsumeLen)
{
    uint8_t* pPre=pInData;
    uint32_t spsSize=0, ppsSize=0;
    uint8_t *sps=NULL, *pps=NULL;
    uint8_t* pNext=NULL;
    uint8_t naltype;
    int32_t length=(int32_t)nSize;
    uint8_t* pTemp=NULL,*pFilled=NULL;
    uint32_t skipLen=0;

    if(pInData == NULL || nConsumeLen == NULL)
        return UNKNOWN_ERROR;

    /*search boundary of sps and pps */
    if(false==FindStartCode(pInData,length,&pPre)){
        goto search_finish;
    }

    pPre+=4; //skip 4 bytes of startcode
    length-=(pPre-pInData);

    if(length<=0){
        goto search_finish;
    }

    while(1){
        int size;

        if(FindStartCode(pPre,length,&pNext)){
            size=pNext-pPre;
        }
        else{
            size=length; //last nal
        }
        naltype=pPre[0] & 0x1f;
        ALOGD("find one nal, type: 0x%x, size: %d \r\n",naltype,size);
        if (naltype==7) { /* SPS */
            sps=pPre;
            spsSize=size;
        }
        else if (naltype==8) { /* PPS */
            pps= pPre;
            ppsSize=size;
        }else if(naltype == 0x0c){//skip coda padding bytes
            skipLen += 4+size;
        }

        if(pNext==NULL){
            goto search_finish;
        }
        pNext+=4;
        length-=(pNext-pPre);
        if(length<=0){
            goto search_finish;
        }
        pPre=pNext;
    }

search_finish:
    if((sps==NULL)||(pps==NULL)){
        return UNKNOWN_ERROR;
    }

    nLen = spsSize+ppsSize+skipLen+4+4;

    if(nSize < nLen)
        return UNKNOWN_ERROR;

    *nConsumeLen = nLen;

    if(pSpsPps != NULL)
        free(pSpsPps);

    pSpsPps = (uint8_t*)malloc(nLen);
    if(pSpsPps == NULL)
        return UNKNOWN_ERROR;
    memcpy(pSpsPps,pInData,nLen); /*sps data*/
    ALOGD("size=%d,consume=%d,nLen=%d",nSize,*nConsumeLen,nLen);
    return OK;
}

status_t FrameConverter::ConvertToData(uint8_t* pData, uint32_t nSize)
{
    /*we will replace the 'start code'(00000001) with 'nal size'(4bytes), and the buffer length no changed*/
    uint8_t* pPre=pData;
    int32_t length=(int32_t)nSize;
    uint32_t nalSize=0;
    uint32_t outSize=0;
    uint32_t i=0;
    uint8_t* pNext=NULL;
    uint8_t naltype;

    if(!FindStartCode(pData,length,&pPre)){
        goto finish;
    }
    pPre+=4; //skip 4 bytes of startcode
    length-=(pPre-pData);
    while(1){

        if(FindStartCode(pPre,length,&pNext)){
            nalSize=pNext-pPre;
        }
        else{
            nalSize=length; //last nal
        }
        naltype=pPre[0] & 0x1f;
        ALOGD("find one nal, type: 0x%x, size: %d \r\n",naltype,nalSize);

        pPre[-4]=(nalSize>>24)&0xFF;
        pPre[-3]=(nalSize>>16)&0xFF;
        pPre[-2]=(nalSize>>8)&0xFF;
        pPre[-1]=(nalSize)&0xFF;

        i++;
        outSize+=nalSize+4;
        ALOGD("ConvertToData find one nal, size: %d \r\n",nalSize);

        if(pNext==NULL){
            goto finish;
        }
        pNext+=4;
        length-=(pNext-pPre);
        pPre=pNext;
    }
finish:
    return OK;
}
status_t FrameConverter::GetSpsPpsPtr(uint8_t** pData,uint32_t *outLen)
{
    if(pData == NULL || outLen == NULL)
        return UNKNOWN_ERROR;

    *pData = pSpsPps;
    *outLen = nLen;
    return OK;
}
status_t FrameConverter::Destroy()
{
    if(pSpsPps != NULL)
        free(pSpsPps);
    nLen = 0;
    return OK;
}
bool FrameConverter::FindStartCode(uint8_t* pData, uint32_t nSize,uint8_t** ppStart)
{
    #define AVC_START_CODE 0x00000001
    uint32_t startcode=0xFFFFFFFF;
    uint8_t* p=pData;
    uint8_t* pEnd=pData+nSize;
    
    if(nSize < 4){
        *ppStart=NULL;
        return false;
    }
    while(p<pEnd){
        startcode=(startcode<<8)|p[0];
        if(AVC_START_CODE==startcode){
            break;
        }
        p++;
    }
    if(p>=pEnd){
        *ppStart=NULL;
        return false;
    }
    *ppStart=p-3;
    return true;
}
}
