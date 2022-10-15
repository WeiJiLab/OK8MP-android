/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef UNIA_DECODER_h
#define UNIA_DECODER_h

#include "IMXAudioDecComponent.h"
#include "AudioDecodeUtil.h"
#include <fsl_unia.h>

namespace android {

struct UniaDecInterface
{
    UniACodecVersionInfo                GetVersionInfo;
    UniACodecCreate                     CreateDecoder;
    UniACodecCreatePlus                 CreateDecoderPlus;
    UniACodecDelete                     DeleteDecoder;
    UniACodecReset                      ResetDecoder;
    UniACodecSetParameter               SetParameter;
    UniACodecGetParameter               GetParameter;
    UniACodec_decode_frame              DecodeFrame;
    UniACodec_get_last_error            GetLastError;
};

struct UniaDecFrameInfo {
    bool bGotOneFrame;//true when get one frame and next frame's header
    uint32_t nHeaderCount;
    uint32_t nConsumedOffset;//frame header offset if get one frame
    uint32_t nFrameSize;//frame size
    uint32_t nNextSize;//frame size
    uint32_t nHeaderSize;
};

class UniaDecoder : public IMXAudioDecComponent {
    public:
        UniaDecoder(const std::shared_ptr<C2ComponentInterface> &intf, AudioDecodeUtil *);
        virtual ~UniaDecoder();

    protected:
        // from IMXAudioDecComponent
        c2_status_t doInit() override;
        c2_status_t unInit() override;
        c2_status_t checkCodecConfig(const uint8 * data, size_t size) override;
        AUDIO_DECODE_RETURN_TYPE doDecode(const std::unique_ptr<C2Work> &work,
                                                    std::list<uint8_t *> &outputBuffers, std::list<uint32_t> &outputSizes) override;
        void            doReset() override;
        c2_status_t checkFrameHeader() override;
        c2_status_t codecInit() override;
        c2_status_t handleEOS(uint8_t **ppBuffer, uint32_t* length) override;
        c2_status_t handleBOS(uint32_t* offset, uint32_t length) override;
        c2_status_t getParamDirectly() override;
        c2_status_t parseInputFrame(uint8_t * pBuf, size_t size) override;

    private:

        void * mLibHandle;
        UniaDecInterface * mWrapper;
        void * mWrapperHandle;
        AudioDecodeUtil * mUtil;

        UniACodecMemoryOps memOps;
        UniACodecParameterBuffer codecConfig;
        CHAN_TABLE channelTable;
        int32_t errorCount;//debug
        int32_t profileErrorCount;
        uint32_t consumeFrameCount;
        uint32_t inputFrameCount;
        uint32_t accumulatedOutputLen;
        uint32_t inputBufferCount;
        uint32_t framedInputBufferCount;
        AUDIO_DECODE_RETURN_TYPE _doDecodeInternal(const std::unique_ptr<C2Work> &work,
                                                        uint8_t **ppOutputBuffer, uint32_t *pOutputSize);

        c2_status_t _createWrapperInterface();
        c2_status_t _initWrapperWithParam();
        void _updateParamFromWrapper(std::vector<std::unique_ptr<C2Param>> *configUpdate);
        const char * _wrapperRet2string(uint32_t uniaDecodeRet);
        const char * _uniaParam2string(UA_ParaType param);
        void _calcCheckSum(uint8_t *pBuffer, uint32_t size);

};

};
#endif

