/**
 *  Copyright 2019,2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef IMX_AUDIO_DEC_COMPONENT_H_
#define IMX_AUDIO_DEC_COMPONENT_H_

#include <queue>
#include <IMXC2ComponentBase.h>

#include "RingBuffer.h"
#include "AudioTSManager.h"
#include <C2_imx.h>

#define DEFAULT_REQUIRED_SIZE 10

/* don't use ringbuffer to accumulate input, just decode each input when it comes, and mark it as Processed,
     and no need to call finish() */
//#define DECODE_EACH_INPUT

typedef enum {
    AUDIO_DECODE_SUCCESS,
    AUDIO_DECODE_EOS,
    AUDIO_DECODE_FAILURE,
    AUDIO_DECODE_NEEDMORE,
    AUDIO_DECODE_FATAL_ERROR,
}AUDIO_DECODE_RETURN_TYPE;

typedef struct AUDIO_PARAM_PCMMODETYPE {
    uint32_t nChannels;                /**< Number of channels (e.g. 2 for stereo) */
    //OMX_NUMERICALDATATYPE eNumData;   /**< indicates PCM data as signed or unsigned */
    //OMX_ENDIANTYPE eEndian;           /**< indicates PCM data as little or big endian */
    //bool bInterleaved;
                                            /**< True for normal interleaved data; false for
                                           non-interleaved data (e.g. block data) */
    uint32_t nBitPerSample;
    uint32_t nSamplingRate;            /**< Sampling rate of the source data.  Use 0 for
                                           variable or unknown sampling rate. */
    //OMX_AUDIO_PCMMODETYPE ePCMMode;   /**< PCM mode enumeration */
    //OMX_AUDIO_CHANNELTYPE eChannelMapping[OMX_AUDIO_MAXCHANNELS]; /**< Slot i contains channel defined by eChannelMap[i] */

} AUDIO_PARAM_PCMMODETYPE;


namespace android {

class IMXAudioDecComponent : public IMXC2ComponentBase {

public:
    class IntfImpl;

    IMXAudioDecComponent(const std::shared_ptr<C2ComponentInterface> &intf);
    virtual ~IMXAudioDecComponent();

protected:

    // From IMXC2Component
    c2_status_t onInit() override;
    c2_status_t onStop() override;
    void        onReset() override;
    void        onRelease() override;
    c2_status_t onFlush_sm() override;
    void        processWork(const std::unique_ptr<C2Work> &work) override;
    c2_status_t drainInternal(uint32_t drainMode) override;

    virtual c2_status_t doInit() = 0;
    virtual c2_status_t unInit() = 0;
    virtual c2_status_t checkCodecConfig(const uint8_t * data, size_t size) = 0;
    virtual AUDIO_DECODE_RETURN_TYPE doDecode(const std::unique_ptr<C2Work> &work,
                                                    std::list<uint8_t *> &outputBuffers,
                                                    std::list<uint32_t> &outputSizes) = 0;
    virtual void        doReset() = 0;
    virtual c2_status_t checkFrameHeader() = 0;
    virtual c2_status_t codecInit() = 0;
    virtual c2_status_t handleEOS(uint8_t **ppBuffer, uint32_t* length) { return C2_OK; };
    virtual c2_status_t handleBOS(uint32_t* offset, uint32_t length) { return C2_OK; };
    virtual c2_status_t getParamDirectly() { return C2_OK; };
    virtual c2_status_t parseInputFrame(uint8_t * pBuf, size_t size) = 0;

    RingBuffer AudioRingBuffer;
    bool       bDecoderEOS;

    uint32_t nPushModeInputLen;
    uint32_t nRingBufferScale;
    uint64_t TS_PerFrame;
    uint32_t nRequiredSize;//set by subclass for audio ring buffer len

    AUDIO_PARAM_PCMMODETYPE PcmMode;
    uint32_t nOutputBitPerSample;
    bool     bConvertEnable;
    decode_mode_t ePlayMode;
    AudioTSManager TS_Manager;
    bool bReceivedEOS;
    int64_t mTimestampDelta;
    bool mDecodeEachInput;
    int32_t logLevel;

    std::queue<int> mFrameLenQueue;

private:

    bool bCheckFrameHeader;
    bool bCodecInit;
    bool bDecoderInitFail;

    bool bFirstInBuffer;
    uint8_t * pConvertBuffer;
    uint32_t nConvertBufferSize;
    bool  bFirstOutput;
    FILE* fpOutput;
    uint32_t frameIndex;
    bool bInitFlag;

    c2_status_t _processInputFlag(const std::unique_ptr<C2Work> &work);
    c2_status_t _processInputData(const std::unique_ptr<C2Work> &work);
    c2_status_t _processOutput(const std::unique_ptr<C2Work> &work);
    c2_status_t _convertData(uint8_t * pOut, uint32_t *nOutSize, uint8_t *pIn, uint32_t nInSize);
    const char * _decodeRetToString(AUDIO_DECODE_RETURN_TYPE decodeRet);
    void _fileDump(FILE** ppFp, uint8_t* pBits, uint32_t nSize);
    void _fileDump(FILE** ppFp, const std::unique_ptr<C2Work> &work);
    void _compareData(const std::unique_ptr<C2Work> &work, uint8_t * pBits, uint32_t nSize);
    void _drainOutput(
        const std::unique_ptr<C2Work> &work,
        uint8_t * pData,
        uint32_t dataLen,
        int64_t nTimeStamp,
        bool eos);
};

}  // namespace android

#endif

