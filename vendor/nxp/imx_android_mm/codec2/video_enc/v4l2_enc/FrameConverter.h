/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#ifndef FRAMECONVERTER_H
#define FRAMECONVERTER_H
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/Mutexed.h>
namespace android {

class FrameConverter{
public:
    status_t Create(uint32_t format);
    status_t ConvertToCodecData(uint8_t* pInData, uint32_t nSize,
        uint8_t* pOutData,uint32_t*nOutSize,uint32_t * nConsumeLen);
    status_t ConvertToData(uint8_t* pData, uint32_t nSize);
    status_t GetSpsPpsPtr(uint8_t** pData,uint32_t *outLen);
    status_t CheckSpsPps(uint8_t* pInData, uint32_t nSize, uint32_t* nConsumeLen);
    status_t Destroy();

private:
    uint32_t nFormat;
    uint32_t nLen;
    uint8_t* pSpsPps;
    
    bool FindStartCode(uint8_t* pData, uint32_t nSize,uint8_t** ppStart);

};
}
#endif

