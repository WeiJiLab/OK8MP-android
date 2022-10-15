/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "Mp3DecUtil"
#include <log/log.h>

#include <Mp3FrameParser.h>
#include <C2Config_imx.h>
#include <IMXC2Interface.h>
#include <media/stagefright/foundation/MediaDefs.h>
#include <UniaDecoder.h>
#include <C2ComponentFactory.h>
#include <C2PlatformSupport.h>

#include "Mp3DecodeUtil.h"

namespace android {

#define MP3D_FRAME_SIZE  1152
#define MP3_PUSH_MODE_LEN   (1024*3)
#define MP3_DECODER_DELAY 529 //samples

#define DSP_WRAPPER_LIB_NAME "lib_dsp_wrap_arm12_android.so"

class Mp3DecodeUtil::IntfImpl : public IMXInterface<void>::BaseParams  {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper, C2String componentName)
        : IMXInterface<void>::BaseParams(
                helper,
                componentName,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_AUDIO,
                MEDIA_MIMETYPE_AUDIO_MPEG){
        noPrivateBuffers(); // TODO: account for our buffers here
        noInputReferences();
        noOutputReferences();
        noInputLatency();
        noTimeStretch();

        addParameter(
                DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                .withConstValue(new C2ComponentAttributesSetting(
                    C2Component::ATTRIB_IS_TEMPORAL))
                .build());

        addParameter(
                DefineParam(mSampleRate, C2_PARAMKEY_SAMPLE_RATE)
                .withDefault(new C2StreamSampleRateInfo::output(0u, 44100))
                .withFields({C2F(mSampleRate, value).oneOf({
                    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
                })})
                .withSetter(Setter<decltype(*mSampleRate)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mChannelCount, C2_PARAMKEY_CHANNEL_COUNT)
                .withDefault(new C2StreamChannelCountInfo::output(0u, 1))
                .withFields({C2F(mChannelCount, value).inRange(1, 8)})
                .withSetter(Setter<decltype(*mChannelCount)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mBitrate, C2_PARAMKEY_BITRATE)
                .withDefault(new C2StreamBitrateInfo::input(0u, 64000))
                .withFields({C2F(mBitrate, value).inRange(4000, 448000)})
                .withSetter(Setter<decltype(*mBitrate)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mInputMaxBufSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                .withConstValue(new C2StreamMaxBufferSizeInfo::input(0u, 8192))
                .build());

    }

    uint32_t getSampleRate() const { return mSampleRate->value; };
    uint32_t getChannelCount() const { return mChannelCount->value; };
    uint32_t getBitrate() const { return mBitrate->value; };
    void setSampleRate(uint32_t value) { mSampleRate->value = value; };
    void setChannelCount(uint32_t value) { mChannelCount->value = value; };
    void setBitrate(uint32_t value) { mBitrate->value = value; };

private:
    std::shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    std::shared_ptr<C2StreamChannelCountInfo::output> mChannelCount;
    std::shared_ptr<C2StreamBitrateInfo::input> mBitrate;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
};

Mp3DecodeUtil::Mp3DecodeUtil(std::string & codecName, const std::shared_ptr<IntfImpl> &intfImpl)
    : AudioDecodeUtil(),
    bFrameChecked(false),
    mIntf(intfImpl),
    nDelayLeft(0)
{
    LOGV("entry %p", this);
    if(codecName.find("c2.imx.mp3.decoder.sw", 0) != std::string::npos){
        wrapperLibName = "lib_mp3d_wrap_arm12_elinux_android.so";
        optionalWrapperLibName = nullptr;
    }
    else if(codecName.find("c2.imx.mp3.decoder.hw", 0) != std::string::npos){
        wrapperLibName = DSP_WRAPPER_LIB_NAME;
        optionalWrapperLibName = nullptr;
    }
    else{
        // error
        LOGE("invalid codecName %s", codecName.c_str());
        wrapperLibName = nullptr;
        optionalWrapperLibName = nullptr;
    }
}

Mp3DecodeUtil::~Mp3DecodeUtil()
{
    LOGV("entry");
}

c2_status_t Mp3DecodeUtil::getLibName(const char ** lib, const char ** optionalLib)
{
    LOGV("entry");
    *lib = wrapperLibName;
    *optionalLib = optionalWrapperLibName;
    return C2_OK;
}

uint32_t Mp3DecodeUtil::getOutBufferLen()
{
    LOGV("entry");
    return MP3D_FRAME_SIZE* 2*2;
}

c2_status_t Mp3DecodeUtil::parseFrame(uint8_t * pBuffer, int len, UniaDecFrameInfo *info)
{
    LOGV("entry");
    AUDIO_FRAME_INFO FrameInfo;

    if(pBuffer == nullptr || info == nullptr || len <= 0)
        return C2_BAD_VALUE;

    memset(&FrameInfo, 0, sizeof(AUDIO_FRAME_INFO));

    if(AFP_SUCCESS == Mp3CheckFrame(&FrameInfo, pBuffer, len)){
        info->bGotOneFrame = FrameInfo.bGotOneFrame;
        info->nConsumedOffset = FrameInfo.nConsumedOffset;
        info->nHeaderCount = FrameInfo.nHeaderCount;
        info->nHeaderSize = FrameInfo.nHeaderSize;
        info->nFrameSize = FrameInfo.nFrameSize;
        info->nNextSize = FrameInfo.nNextFrameSize;
    }

    return C2_OK;
}

c2_status_t Mp3DecodeUtil::getDecoderProp(AUDIOFORMAT *formatType, bool *isHwBased)
{
    LOGV("entry");
    if (formatType)
        *formatType = MP3;
    if (isHwBased)
        *isHwBased = !strcmp(wrapperLibName, DSP_WRAPPER_LIB_NAME);

    return C2_OK;
}

c2_status_t Mp3DecodeUtil::handleBOS(uint32_t* offset, uint32_t length)
{
    if (nDelayLeft == 0)
        nDelayLeft = MP3_DECODER_DELAY * mIntf->getChannelCount() * sizeof(int16_t);

    if (length - *offset > nDelayLeft) {
        *offset += nDelayLeft;
        nDelayLeft = 0;
    } else {
        nDelayLeft -= (length - *offset);
        *offset = length;
    }

    if (0 == nDelayLeft)
        return C2_OK;
    else
        return C2_BAD_VALUE;

}

c2_status_t Mp3DecodeUtil::handleEOS(uint8_t **ppBuffer, uint32_t* length)
{
    LOGV("entry");
    uint32_t padding = 0;
    uint8_t *pBuf = *ppBuffer;

    // pad the end of the stream with 529 samples, since that many samples
    // were trimmed off the beginning when decoding started
    padding = MP3_DECODER_DELAY * mIntf->getChannelCount() * sizeof(int16_t);

    pBuf = (uint8_t*)realloc(pBuf, *length + padding);
    if (!pBuf)
        return C2_NO_MEMORY;

    memset(pBuf + *length, 0, padding);
    *ppBuffer = pBuf;
    *length += padding;

    return C2_OK;
}

c2_status_t Mp3DecodeUtil::setParameter(UA_ParaType index,int32_t value)
{
    LOGV("entry");
    c2_status_t ret = C2_OK;
    switch(index){
        case UNIA_SAMPLERATE:
        {
            LOGV("sample rate %d",value);
            //mIntf->setSampleRate(value);
            C2StreamSampleRateInfo::output sampleRateInfo(0u, value);
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            ret = mIntf->config({ &sampleRateInfo }, C2_MAY_BLOCK, &failures);
            break;
        }
        case UNIA_CHANNEL:
        {
            LOGV("channel num %d",value);
            //mIntf->setChannelCount(value);
            C2StreamChannelCountInfo::output channelCountInfo(0u, value);
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            ret = mIntf->config({ &channelCountInfo }, C2_MAY_BLOCK, &failures);
            break;
        }
        case UNIA_BITRATE:
        {
            LOGV("bitrate %d",value);
            mIntf->setBitrate(value);
            break;
        }
        default:
        {
            LOGV("unknown index!");
            ret = C2_OMITTED;
            break;
        }
    }
    return ret;
}

c2_status_t Mp3DecodeUtil::getParameter(UA_ParaType index,int32_t * value)
{
    LOGV("entry");
    c2_status_t ret = C2_OK;
    if(value == nullptr){
        ret = C2_BAD_VALUE;
        return ret;
    }

    switch(index){
        case UNIA_SAMPLERATE:
        {
            *value = (int32_t)mIntf->getSampleRate();
            LOGV("sample rate %d",*value);
            break;
        }
        case UNIA_CHANNEL:
        {
            *value = (int32_t)mIntf->getChannelCount();
            LOGV("channel num %d",*value);
            break;
        }
        case UNIA_BITRATE:
        {
            *value = (int32_t)mIntf->getBitrate();
            LOGV("bitrate %d",*value);
            break;
        }
        case UNIA_FRAMED:
        {
            *value = true;
            LOGV("framed %d",*value);
            break;
        }
        default:
        {
            LOGV("unknown index!");
            ret = C2_OMITTED;
            break;
        }
    }
    return ret;

}

size_t Mp3DecodeUtil::getPushModeInputLen()
{
    return MP3_PUSH_MODE_LEN;
}

c2_status_t Mp3DecodeUtil::checkFrameHeader(unsigned char * pBuffer, size_t length, uint32_t *pOffset){

    LOGV("entry");
    uint32_t nVal = 0;
    AUDIO_FRAME_INFO FrameInfo;
    memset(&FrameInfo, 0, sizeof(AUDIO_FRAME_INFO));

    do{

        if(AFP_SUCCESS != Mp3CheckFrame(&FrameInfo, pBuffer, length)){
            LOGD("CHECK FAILED");
            break;
        }

        if(FrameInfo.bGotOneFrame && FrameInfo.nHeaderCount == 1 &&
            (FrameInfo.nNextFrameSize == 0 || FrameInfo.nFrameSize == length)){
            mFrameInput = true;
            LOGD("get one single frame");
        }

        uint32_t nOffset = FrameInfo.nConsumedOffset;
        *pOffset = nOffset;
        return C2_OK;
    }while(0);

    return C2_NOT_FOUND;
}

class IMXC2Mp3DecFactory : public C2ComponentFactory {
public:
    IMXC2Mp3DecFactory(C2String name) : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
            GetImxC2Store()->getParamReflector()))
    {
            if(!name.empty())
                mCodecName.assign(name);
            else
                mCodecName.assign("c2.imx.mp3.decoder.sw");
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {

            auto impl = std::make_shared<Mp3DecodeUtil::IntfImpl>(mHelper, mCodecName.c_str());
            Mp3DecodeUtil * pMp3Util = new Mp3DecodeUtil(mCodecName, impl);

        *component = std::shared_ptr<C2Component>(
                new UniaDecoder(std::make_shared<IMXC2Interface<Mp3DecodeUtil::IntfImpl>>(mCodecName.c_str(), id, impl),
                                            pMp3Util),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new IMXInterface<Mp3DecodeUtil::IntfImpl>(
                        mCodecName, id, std::make_shared<Mp3DecodeUtil::IntfImpl>(mHelper, mCodecName.c_str())),
                deleter);
        return C2_OK;
    }

    virtual ~IMXC2Mp3DecFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
    std::string mCodecName;
};

}  // namespace android

extern "C" ::C2ComponentFactory* IMXCreateCodec2Factory(C2String name) {
    return new ::android::IMXC2Mp3DecFactory(name);
}

extern "C" void IMXDestroyCodec2Factory(::C2ComponentFactory* factory) {
    delete factory;
}
