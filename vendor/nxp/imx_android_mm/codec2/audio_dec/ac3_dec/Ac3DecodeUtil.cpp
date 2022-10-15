/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "Ac3DecUtil"
#include <log/log.h>

#include "Ac3FrameParser.h"

#include <C2Config_imx.h>
#include <IMXC2Interface.h>
#include <media/stagefright/foundation/MediaDefs.h>
#include <UniaDecoder.h>
#include <C2ComponentFactory.h>
#include <C2PlatformSupport.h>

#include "Ac3DecodeUtil.h"

namespace android {

#define AC3D_FRAME_SIZE  1536
#define AC3_PUSH_MODE_LEN (3840+8)//max frame size + another frame header size

class Ac3DecodeUtil::IntfImpl : public IMXInterface<void>::BaseParams  {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper, C2String componentName)
        : IMXInterface<void>::BaseParams(
                helper,
                componentName,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_AUDIO,
                MEDIA_MIMETYPE_AUDIO_AC3){
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
                DefineParam(mBitrate, C2_PARAMKEY_BITRATE)
                .withDefault(new C2StreamBitrateInfo::input(0u, 64000))
                .withFields({C2F(mBitrate, value).inRange(8000, 960000)})
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

Ac3DecodeUtil::Ac3DecodeUtil(std::string & codecName, const std::shared_ptr<IntfImpl> &intfImpl)
    : AudioDecodeUtil(),
    bFrameChecked(false),
    mIntf(intfImpl)
{
    LOGV("entry %p", this);
    if(codecName.find("c2.imx.ac3.decoder.sw", 0) != std::string::npos){
        wrapperLibName = "lib_ac3d_wrap_arm11_elinux_android.so";
        optionalWrapperLibName = "lib_ac3d_wrap_arm_android.so"; // 64 bits
    }
    else{
        // error
        LOGE("invalid codecName %s", codecName.c_str());
        wrapperLibName = nullptr;
        optionalWrapperLibName = nullptr;
    }
}

Ac3DecodeUtil::~Ac3DecodeUtil()
{
    LOGV("entry");
}

c2_status_t Ac3DecodeUtil::getLibName(const char ** lib, const char ** optionalLib)
{
    LOGV("entry");
    *lib = wrapperLibName;
    *optionalLib = optionalWrapperLibName;
    return C2_OK;
}

uint32_t Ac3DecodeUtil::getFrameHdrBufLen()
{
    LOGV("entry");
    return AC3_PUSH_MODE_LEN;
}

uint32_t Ac3DecodeUtil::getOutBufferLen()
{
    LOGV("entry");
    return 256*6*6*sizeof(int32);
}

c2_status_t Ac3DecodeUtil::checkFrameHeader(unsigned char * pBuffer, size_t length, uint32_t *pOffset)
{
    LOGV("entry");
    uint32_t nVal = 0;
    AUDIO_FRAME_INFO FrameInfo;
    bool bFound = false;
    memset(&FrameInfo, 0, sizeof(AUDIO_FRAME_INFO));

    if(bFrameChecked){
        return C2_OK;
    }

    do{
        //LOGI("Get stream length: %zu\n", length);

        if(AFP_SUCCESS != Ac3CheckFrame(&FrameInfo, pBuffer, length)){
            LOGD("CHECK FAILED");
            break;
        }

        if(FrameInfo.bGotOneFrame){
            bFound = true;
            LOGD("get one frame");
        }

        uint32_t nOffset = FrameInfo.nConsumedOffset;

        if(nOffset < length)
            LOGD("buffer=%02x%02x%02x%02x",pBuffer[nOffset],pBuffer[nOffset+1],pBuffer[nOffset+2],pBuffer[nOffset+1]);

        *pOffset = nOffset;

        if(bFound)
            return C2_OK;
    }while(0);

    return C2_NOT_FOUND;

}

c2_status_t Ac3DecodeUtil::parseFrame(uint8_t * pBuffer, int len, UniaDecFrameInfo *info)
{
    LOGV("entry");
    return C2_OK;
}

c2_status_t Ac3DecodeUtil::getDecoderProp(AUDIOFORMAT *formatType, bool *isHwBased)
{
    LOGV("entry");
    if (formatType)
        *formatType = AC3;
    if (isHwBased)
        *isHwBased = false;

    return C2_OK;
}

c2_status_t Ac3DecodeUtil::handleBOS(uint32_t* offset, uint32_t length) {
    return C2_OK;
}

c2_status_t Ac3DecodeUtil::handleEOS(uint8_t **ppBuffer, uint32_t* length)
{
    return C2_OK;
}

c2_status_t Ac3DecodeUtil::setParameter(UA_ParaType index,int32_t value)
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

c2_status_t Ac3DecodeUtil::getParameter(UA_ParaType index,int32_t * value)
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

size_t Ac3DecodeUtil::getPushModeInputLen()
{
    return AC3_PUSH_MODE_LEN;
}


class IMXC2Ac3DecFactory : public C2ComponentFactory {
public:
    IMXC2Ac3DecFactory(C2String name) : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
            GetImxC2Store()->getParamReflector()))
    {
            if(!name.empty())
                mCodecName.assign(name);
            else
                mCodecName.assign("c2.imx.ac3.decoder.sw");
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {

            auto impl = std::make_shared<Ac3DecodeUtil::IntfImpl>(mHelper, mCodecName.c_str());
            Ac3DecodeUtil * pAc3Util = new Ac3DecodeUtil(mCodecName, impl);

        *component = std::shared_ptr<C2Component>(
                new UniaDecoder(std::make_shared<IMXC2Interface<Ac3DecodeUtil::IntfImpl>>(mCodecName.c_str(), id, impl),
                                            pAc3Util),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new IMXInterface<Ac3DecodeUtil::IntfImpl>(
                        mCodecName, id, std::make_shared<Ac3DecodeUtil::IntfImpl>(mHelper, mCodecName.c_str())),
                deleter);
        return C2_OK;
    }

    virtual ~IMXC2Ac3DecFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
    std::string mCodecName;
};

}  // namespace android

extern "C" ::C2ComponentFactory* IMXCreateCodec2Factory(C2String name) {
    return new ::android::IMXC2Ac3DecFactory(name);
}

extern "C" void IMXDestroyCodec2Factory(::C2ComponentFactory* factory) {
    delete factory;
}

