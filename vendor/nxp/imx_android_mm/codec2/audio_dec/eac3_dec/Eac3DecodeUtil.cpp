/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "Eac3DecUtil"
#include <log/log.h>

#include <C2Config_imx.h>
#include <IMXC2Interface.h>
#include <media/stagefright/foundation/MediaDefs.h>
#include <UniaDecoder.h>
#include <C2ComponentFactory.h>
#include <C2PlatformSupport.h>

#include "Eac3DecodeUtil.h"

namespace android {

#define EAC3_PUSH_MODE_LEN (6144+8)//max frame size + another frame header size

#define EAC3_MAX_CHANNELS 8
/* pcm channel layout for eac3 */
static uint32 eac3d_1channel_layout[] = {
    /* FC */
    UA_CHANNEL_FRONT_CENTER
};
static uint32 eac3d_2channel_layout[] = {
    /* FL,FR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT
};
static uint32 eac3d_3channel_layout[] = {
    /* FL,FR, FC */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER
    };
static uint32 eac3d_4channel_layout[] = {
    /* FL,FR,BL,BR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT
};
static uint32 eac3d_5channel_layout[] = {
    /* FL,FR,FC,BL,BR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT
    };
static uint32 eac3d_6channel_layout[] = {
    /* FL,FR,FC,LFE,BL,BR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_LFE,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT
};
static uint32 eac3d_8channel_layout[] = {
/* FL, FR, FC, LFE, BL, BR, SL, SR */
  UA_CHANNEL_FRONT_LEFT,
  UA_CHANNEL_FRONT_RIGHT,
  UA_CHANNEL_FRONT_CENTER,
  UA_CHANNEL_LFE,
  UA_CHANNEL_REAR_LEFT,
  UA_CHANNEL_REAR_RIGHT,
  UA_CHANNEL_SIDE_LEFT,
  UA_CHANNEL_SIDE_RIGHT,
};
static uint32 * eac3d_channel_layouts[] = {
    NULL,
    eac3d_1channel_layout,// 1
    eac3d_2channel_layout,// 2
    eac3d_3channel_layout,
    eac3d_4channel_layout,
    eac3d_5channel_layout,
    eac3d_6channel_layout,
    NULL,
    eac3d_8channel_layout
};


class Eac3DecodeUtil::IntfImpl : public IMXInterface<void>::BaseParams  {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper, C2String componentName)
        : IMXInterface<void>::BaseParams(
                helper,
                componentName,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_AUDIO,
                MEDIA_MIMETYPE_AUDIO_EAC3){
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

Eac3DecodeUtil::Eac3DecodeUtil(std::string & codecName, const std::shared_ptr<IntfImpl> &intfImpl)
    : AudioDecodeUtil(),
    bFrameChecked(false),
    mIntf(intfImpl)
{
    LOGV("entry %p", this);
    if(codecName.find("c2.imx.eac3.decoder.sw", 0) != std::string::npos){
        wrapperLibName = "lib_ddpd_wrap_arm12_elinux_android.so";
        optionalWrapperLibName = "lib_ddpd_wrap_arm_android.so"; // 64 bits
    }
    else{
        LOGE("invalid codecName %s", codecName.c_str());
        wrapperLibName = nullptr;
        optionalWrapperLibName = nullptr;
    }
}

Eac3DecodeUtil::~Eac3DecodeUtil()
{
    LOGV("entry");
}

c2_status_t Eac3DecodeUtil::getLibName(const char ** lib, const char ** optionalLib)
{
    LOGV("entry");
    *lib = wrapperLibName;
    *optionalLib = optionalWrapperLibName;
    return C2_OK;
}

uint32_t Eac3DecodeUtil::getFrameHdrBufLen()
{
    LOGV("entry");
    return EAC3_PUSH_MODE_LEN;
}

uint32_t Eac3DecodeUtil::getOutBufferLen()
{
    LOGV("entry");
    return 36864;//8*6*256*24/8
}

c2_status_t Eac3DecodeUtil::parseFrame(uint8_t * pBuffer, int len, UniaDecFrameInfo *info)
{
    LOGV("entry");
    return C2_OK;
}

c2_status_t Eac3DecodeUtil::getDecoderProp(AUDIOFORMAT *formatType, bool *isHwBased)
{
    LOGV("entry");
    if (formatType)
        *formatType = DD_PLUS;
    if (isHwBased)
        *isHwBased = false;

    return C2_OK;
}

c2_status_t Eac3DecodeUtil::handleBOS(uint32_t* offset, uint32_t length) {
    return C2_OK;
}

c2_status_t Eac3DecodeUtil::handleEOS(uint8_t **ppBuffer, uint32_t* length)
{
    return C2_OK;
}

c2_status_t Eac3DecodeUtil::setParameter(UA_ParaType index,int32_t value)
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

c2_status_t Eac3DecodeUtil::getParameter(UA_ParaType index,int32_t * value)
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
        case UNIA_CHAN_MAP_TABLE:
        {
            CHAN_TABLE table;
            memset(&table,0,sizeof(table));
            table.size = EAC3_MAX_CHANNELS;
            memcpy(&table.channel_table,eac3d_channel_layouts,sizeof(eac3d_channel_layouts));
            memcpy(value,&table,sizeof(CHAN_TABLE));
            LOGD("SET MAP TABLE");
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

size_t Eac3DecodeUtil::getPushModeInputLen()
{
    return EAC3_PUSH_MODE_LEN;
}


class IMXC2Eac3DecFactory : public C2ComponentFactory {
public:
    IMXC2Eac3DecFactory(C2String name) : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
            GetImxC2Store()->getParamReflector()))
    {
            if(!name.empty())
                mCodecName.assign(name);
            else
                mCodecName.assign("c2.imx.eac3.decoder.sw");
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {

            auto impl = std::make_shared<Eac3DecodeUtil::IntfImpl>(mHelper, mCodecName.c_str());
            Eac3DecodeUtil * pEac3Util = new Eac3DecodeUtil(mCodecName, impl);

        *component = std::shared_ptr<C2Component>(
                new UniaDecoder(std::make_shared<IMXC2Interface<Eac3DecodeUtil::IntfImpl>>(mCodecName.c_str(), id, impl),
                                            pEac3Util),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new IMXInterface<Eac3DecodeUtil::IntfImpl>(
                        mCodecName, id, std::make_shared<Eac3DecodeUtil::IntfImpl>(mHelper, mCodecName.c_str())),
                deleter);
        return C2_OK;
    }

    virtual ~IMXC2Eac3DecFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
    std::string mCodecName;
};

}  // namespace android

extern "C" ::C2ComponentFactory* IMXCreateCodec2Factory(C2String name) {
    return new ::android::IMXC2Eac3DecFactory(name);
}

extern "C" void IMXDestroyCodec2Factory(::C2ComponentFactory* factory) {
    delete factory;
}

