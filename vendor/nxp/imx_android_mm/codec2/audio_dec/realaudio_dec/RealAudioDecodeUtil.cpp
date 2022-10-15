/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */
#define LOG_NDEBUG 0
#define LOG_TAG "RaDecUtil"
#include <log/log.h>

#include <C2Config_imx.h>
#include <IMXC2Interface.h>
#include <media/stagefright/foundation/MediaDefs.h>
#include <UniaDecoder.h>
#include <C2ComponentFactory.h>
#include <C2PlatformSupport.h>
#include <Imx_ext.h>

#include "RealAudioDecodeUtil.h"

namespace android {

#define REAL_AUDIO_PUSH_MODE_LEN (2240/8)//max frame size

class RealAudioDecodeUtil::IntfImpl : public IMXInterface<void>::BaseParams  {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper, C2String componentName)
        : IMXInterface<void>::BaseParams(
                helper,
                componentName,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_AUDIO,
                MEDIA_MIMETYPE_AUDIO_REAL){
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
                    32000, 44100, 48000
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
                DefineParam(mInputMaxBufSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                .withConstValue(new C2StreamMaxBufferSizeInfo::input(0u, 8192))
                .build());

        addParameter(
                DefineParam(mBitsPerFrame, C2_PARAMKEY_BITS_PER_FRAME)
                .withDefault(new C2StreamBitsPerFrame::output(0u, 0))
                .withFields({C2F(mBitsPerFrame, value).inRange(0, 10000)})
                .withSetter(Setter<decltype(*mBitsPerFrame)>::NonStrictValueWithNoDeps)
                .build());

    }

    uint32_t getSampleRate() const { return mSampleRate->value; };
    uint32_t getChannelCount() const { return mChannelCount->value; };
    uint32_t getBitsPerFrame() const { return mBitsPerFrame->value; };
    void setSampleRate(uint32_t value) { mSampleRate->value = value; };
    void setChannelCount(uint32_t value) { mChannelCount->value = value; };

private:
    std::shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    std::shared_ptr<C2StreamChannelCountInfo::output> mChannelCount;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    std::shared_ptr<C2StreamBitsPerFrame::output> mBitsPerFrame;
};

RealAudioDecodeUtil::RealAudioDecodeUtil(std::string & codecName, const std::shared_ptr<IntfImpl> &intfImpl)
    : AudioDecodeUtil(),
    bFrameChecked(false),
    mIntf(intfImpl)
{
    LOGV("entry %p", this);
    if(codecName.find("c2.imx.ra.decoder.sw", 0) != std::string::npos){
        wrapperLibName = "lib_realad_wrap_arm11_elinux_android.so";
        optionalWrapperLibName = "lib_realad_wrap_arm_elinux_android.so"; // 64 bits
    }
    else{
        // error
        LOGE("invalid codecName %s", codecName.c_str());
        wrapperLibName = nullptr;
        optionalWrapperLibName = nullptr;
    }
}

RealAudioDecodeUtil::~RealAudioDecodeUtil()
{
    LOGV("entry");
}

c2_status_t RealAudioDecodeUtil::getLibName(const char ** lib, const char ** optionalLib)
{
    LOGV("entry");
    *lib = wrapperLibName;
    *optionalLib = optionalWrapperLibName;
    return C2_OK;
}

uint32_t RealAudioDecodeUtil::getFrameHdrBufLen()
{
    LOGV("entry");
    return REAL_AUDIO_PUSH_MODE_LEN;
}

uint32_t RealAudioDecodeUtil::getOutBufferLen()
{
    LOGV("entry");
    return 4096;
}

c2_status_t RealAudioDecodeUtil::parseFrame(uint8_t * pBuffer, int len, UniaDecFrameInfo *info)
{
    LOGV("entry");
    return C2_OK;
}

c2_status_t RealAudioDecodeUtil::getDecoderProp(AUDIOFORMAT *formatType, bool *isHwBased)
{
    LOGV("entry");
    if (formatType)
        *formatType = RA;
    if (isHwBased)
        *isHwBased = false;

    return C2_OK;
}

c2_status_t RealAudioDecodeUtil::handleBOS(uint32_t* offset, uint32_t length) {
    return C2_OK;
}

c2_status_t RealAudioDecodeUtil::handleEOS(uint8_t **ppBuffer, uint32_t* length)
{
    return C2_OK;
}

c2_status_t RealAudioDecodeUtil::setParameter(UA_ParaType index,int32_t value)
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
        default:
        {
            LOGV("unknown index!");
            ret = C2_OMITTED;
            break;
        }
    }
    return ret;
}

c2_status_t RealAudioDecodeUtil::getParameter(UA_ParaType index,int32_t * value)
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
        case UNIA_FRAMED:
        {
            *value = false;
            LOGV("framed %d",*value);
            break;
        }
        case UNIA_RA_FRAME_BITS:
        {
            *value = (int32_t)mIntf->getBitsPerFrame();
            LOGV("UNIA_RA_FRAME_BITS %d",*value);
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

size_t RealAudioDecodeUtil::getPushModeInputLen()
{
    return REAL_AUDIO_PUSH_MODE_LEN;
}


class IMXC2RaDecFactory : public C2ComponentFactory {
public:
    IMXC2RaDecFactory(C2String name) : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
            GetImxC2Store()->getParamReflector()))
    {
            if(!name.empty())
                mCodecName.assign(name);
            else
                mCodecName.assign("c2.imx.ra.decoder.sw");
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {

            auto impl = std::make_shared<RealAudioDecodeUtil::IntfImpl>(mHelper, mCodecName.c_str());
            RealAudioDecodeUtil * pUtil = new RealAudioDecodeUtil(mCodecName, impl);

        *component = std::shared_ptr<C2Component>(
                new UniaDecoder(std::make_shared<IMXC2Interface<RealAudioDecodeUtil::IntfImpl>>(mCodecName.c_str(), id, impl),
                                            pUtil),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new IMXInterface<RealAudioDecodeUtil::IntfImpl>(
                        mCodecName, id, std::make_shared<RealAudioDecodeUtil::IntfImpl>(mHelper, mCodecName.c_str())),
                deleter);
        return C2_OK;
    }

    virtual ~IMXC2RaDecFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
    std::string mCodecName;
};

}  // namespace android

extern "C" ::C2ComponentFactory* IMXCreateCodec2Factory(C2String name) {
    return new ::android::IMXC2RaDecFactory(name);
}

extern "C" void IMXDestroyCodec2Factory(::C2ComponentFactory* factory) {
    delete factory;
}


