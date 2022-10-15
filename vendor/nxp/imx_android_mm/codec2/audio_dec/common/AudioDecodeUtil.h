/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef AUDIO_DECODE_UTIL_h
#define AUDIO_DECODE_UTIL_h

#include <C2.h>
#include <C2_imx.h>
#include <fsl_unia.h>
#include <cutils/properties.h>

namespace android {

struct UniaDecFrameInfo;

class AudioDecodeUtil {
    public:
        AudioDecodeUtil():mFrameInput(false){
            logLevel = property_get_int32("vendor.media.log.level", 0);
        };
        virtual c2_status_t getLibName(const char ** lib, const char ** optionalLib) = 0;
        virtual uint32_t getFrameHdrBufLen(){ return 0;};
        virtual uint32_t getOutBufferLen() = 0;
        virtual c2_status_t checkFrameHeader(unsigned char * pBuffer, size_t length, uint32_t *pOffset){
            return C2_OK;
        };
        virtual c2_status_t parseFrame(uint8_t * pBuffer, int len, UniaDecFrameInfo *info){
            return C2_OMITTED;
        };
        virtual c2_status_t getDecoderProp(AUDIOFORMAT *formatType, bool *isHwBased) = 0;
        virtual c2_status_t handleBOS(uint32_t* offset, uint32_t length) = 0;
        virtual c2_status_t handleEOS(uint8_t **ppBuffer, uint32_t* length) = 0;
        virtual c2_status_t setParameter(UA_ParaType index,int32_t value) = 0;
        virtual c2_status_t getParameter(UA_ParaType index,int32_t * value) = 0;
        bool isFrameInput() { return mFrameInput; };
        virtual c2_status_t checkParameter() { return C2_OK; };
        virtual size_t getPushModeInputLen(){ return 2048;};

        virtual ~AudioDecodeUtil(){ };

    protected:
        const char * wrapperLibName;
        const char * optionalWrapperLibName;
        bool mFrameInput;
        int32_t logLevel;


};

}
#endif

