/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef WMA_DECODE_UTIL_h
#define WMA_DECODE_UTIL_h

#include <AudioDecodeUtil.h>

namespace android {

class WmaDecodeUtil  : public AudioDecodeUtil {
    public:
        class IntfImpl;

        WmaDecodeUtil(std::string & codecName, const std::shared_ptr<IntfImpl> &intfImpl);
        virtual ~WmaDecodeUtil();
        virtual c2_status_t getLibName(const char ** lib, const char ** optionalLib) override;
        virtual uint32_t getOutBufferLen() override;
        virtual c2_status_t getDecoderProp(AUDIOFORMAT *formatType, bool *isHwBased) override;
        virtual c2_status_t handleBOS(uint32_t* offset, uint32_t length) override;
        virtual c2_status_t handleEOS(uint8_t **ppBuffer, uint32_t* length) override;
        virtual c2_status_t setParameter(UA_ParaType index,int32_t value) override;
        virtual c2_status_t getParameter(UA_ParaType index,int32_t * value) override;
        std::shared_ptr<C2ComponentInterface> mIntf2;

    private:
        bool bFrameChecked;
        std::shared_ptr<IntfImpl> mIntf;

};

}
#endif


