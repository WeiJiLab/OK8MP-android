/**
 *  Copyright 2019,2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "WmaDecUtil"
#include <log/log.h>

#include <C2Config_imx.h>
#include <IMXC2Interface.h>
#include <media/stagefright/foundation/MediaDefs.h>
#include <UniaDecoder.h>
#include <C2ComponentFactory.h>
#include <C2PlatformSupport.h>

#include "WmaDecodeUtil.h"


namespace android {

#define WMA_OUTPUT_PORT_SIZE 200000
#define DSP_WRAPPER_LIB_NAME "lib_dsp_wrap_arm12_android.so"

#define WMA_MAX_CHANNELS 8
/* pcm channel mask for wma*/
static uint32 wma10d_1channel_layout[] = {
    /* FC */
    UA_CHANNEL_FRONT_CENTER
};
static uint32 wma10d_2channel_layout[] = {
    /* FL,FR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
};
static uint32 wma10d_3channel_layout[] = {
    /* FL,FR,FC */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER,
    };
static uint32 wma10d_4channel_layout[] = {
    /* FL,FR,BL,BR */
    UA_CHANNEL_FRONT_LEFT_CENTER,
    UA_CHANNEL_FRONT_RIGHT_CENTER,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT
};
static uint32 wma10d_5channel_layout[] = {
    /* FL,FR,FC,BL,BR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT
    };
static uint32 wma10d_6channel_layout[] = {
    /* FL,FR,FC,LFE,BL,BR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_LFE,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT
};
static uint32 wma10d_7channel_layout[] = {
    /* FL,FR,FC,LFE,BL,BR,BC */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_LFE,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT,
    UA_CHANNEL_REAR_CENTER
};
static uint32 wma10d_8channel_layout[] = {
    /* FL,FR,FC,LFE,BL,BR,SL,SR  */
    UA_CHANNEL_FRONT_LEFT_CENTER,
    UA_CHANNEL_FRONT_RIGHT_CENTER,
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_LFE,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT,
    UA_CHANNEL_SIDE_LEFT,
    UA_CHANNEL_SIDE_RIGHT
};
static uint32 * wma10d_channel_layouts[] = {
    NULL,
    wma10d_1channel_layout, // 1
    wma10d_2channel_layout, // 2
    wma10d_3channel_layout,
    wma10d_4channel_layout,
    wma10d_5channel_layout,
    wma10d_6channel_layout,
    wma10d_7channel_layout,
    wma10d_8channel_layout
};

class WmaDecodeUtil::IntfImpl : public IMXInterface<void>::BaseParams  {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper, C2String componentName)
        : IMXInterface<void>::BaseParams(
                helper,
                componentName,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_AUDIO,
                MEDIA_MIMETYPE_AUDIO_WMA){
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
                .withFields({C2F(mBitrate, value).inRange(1, 3000000)})
                .withSetter(Setter<decltype(*mBitrate)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mInputMaxBufSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                .withConstValue(new C2StreamMaxBufferSizeInfo::input(0u, 8192))
                .build());

        addParameter(
                DefineParam(mBlockAlign, C2_PARAMKEY_AUDIO_BLOCK_ALIGN)
                .withDefault(new C2StreamAudioBlockAlign::input(0u, 0))
                .withFields({C2F(mBlockAlign, value).inRange(0, 0x7fffffff)})
                .withSetter(Setter<decltype(*mBlockAlign)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mSubFormat, C2_PARAMKEY_VENDOR_SUB_FORMAT)
                .withDefault(new C2StreamVendorSubFormat::output(0))
                .withFields({C2F(mSubFormat, value).inRange(0, 0x7fffffff)})
                .withSetter(Setter<decltype(*mSubFormat)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mBitsPerSample, C2_PARAMKEY_BITS_PER_SAMPLE)
                .withDefault(new C2StreamBitsPerSample::output(0u, 0))
                .withFields({C2F(mBitsPerSample, value).inRange(0, 100)})
                .withSetter(Setter<decltype(*mBitsPerSample)>::NonStrictValueWithNoDeps)
                .build());

    }

    uint32_t getSampleRate() const { return mSampleRate->value; };
    uint32_t getChannelCount() const { return mChannelCount->value; };
    uint32_t getBitrate() const { return mBitrate->value; };
    uint32_t getBlockAlign() const { return mBlockAlign->value; };
    uint32_t getSubFormat() const { return mSubFormat->value; };
    uint32_t getBitsPerSample() const { return mBitsPerSample->value; };

    void setSampleRate(uint32_t value) { mSampleRate->value = value; };
    void setChannelCount(uint32_t value) { mChannelCount->value = value; };
    void setBitrate(uint32_t value) { mBitrate->value = value; };
    void setBlockAlign(uint32_t value) { mBlockAlign->value = value; };
    void setBitsPerSample(uint32_t value) { mBitsPerSample->value = value; };

private:
    std::shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    std::shared_ptr<C2StreamChannelCountInfo::output> mChannelCount;
    std::shared_ptr<C2StreamBitrateInfo::input> mBitrate;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    std::shared_ptr<C2StreamAudioBlockAlign::input> mBlockAlign;
    std::shared_ptr<C2StreamVendorSubFormat::output> mSubFormat;
    std::shared_ptr<C2StreamBitsPerSample::output> mBitsPerSample;
};

WmaDecodeUtil::WmaDecodeUtil(std::string & codecName, const std::shared_ptr<IntfImpl> &intfImpl)
    : AudioDecodeUtil(),
    bFrameChecked(false),
    mIntf(intfImpl)
{
    LOGV("entry %p", this);
    if(codecName.find("c2.imx.wma.decoder.sw", 0) != std::string::npos){
        wrapperLibName = "lib_wma10d_wrap_arm12_elinux_android.so";
        optionalWrapperLibName = nullptr;
    }
    else if(codecName.find("c2.imx.wma.decoder.hw", 0) != std::string::npos){
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

WmaDecodeUtil::~WmaDecodeUtil()
{
    LOGV("entry");
}

c2_status_t WmaDecodeUtil::getLibName(const char ** lib, const char ** optionalLib)
{
    LOGV("entry");
    *lib = wrapperLibName;
    *optionalLib = optionalWrapperLibName;
    return C2_OK;
}

uint32_t WmaDecodeUtil::getOutBufferLen()
{
    //LOGV("entry");
    return WMA_OUTPUT_PORT_SIZE;
}

c2_status_t WmaDecodeUtil::getDecoderProp(AUDIOFORMAT *formatType, bool *isHwBased)
{
    LOGV("entry");
    if (formatType)
        *formatType = WMA;
    if (isHwBased)
        *isHwBased = !strcmp(wrapperLibName, DSP_WRAPPER_LIB_NAME);
    return C2_OK;
}

c2_status_t WmaDecodeUtil::handleBOS(uint32_t* offset, uint32_t length) {
    return C2_OK;
}

c2_status_t WmaDecodeUtil::handleEOS(uint8_t **ppBuffer, uint32_t* length)
{
    return C2_OK;
}

c2_status_t WmaDecodeUtil::setParameter(UA_ParaType index,int32_t value)
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
        case UNIA_DEPTH:
        {
            LOGV("UNIA_DEPTH %d", value);
            mIntf->setBitsPerSample(value);
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

c2_status_t WmaDecodeUtil::getParameter(UA_ParaType index,int32_t * value)
{
    //LOGV("entry");
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
            *value = mIntf->getBitrate();
            LOGV("bitrate %d",*value);
            break;
        }
        case UNIA_FRAMED:
        {
            *value = true;
            LOGV("framed %d",*value);
            break;
        }
        case UNIA_WMA_BlOCKALIGN:
        {
            *value = mIntf->getBlockAlign();
            LOGV("block align %d",*value);
            break;
        }
        case UNIA_WMA_VERSION:
            *value = mIntf->getSubFormat();
            LOGV("subformat is %d", *value);
            break;
        case UNIA_DEPTH:
            *value = (int32_t)mIntf->getBitsPerSample();
            LOGV("get UNIA_DEPTH %d", *value);
            break;
        case UNIA_CHAN_MAP_TABLE:
            CHAN_TABLE table;
            memset(&table,0,sizeof(table));
            table.size = WMA_MAX_CHANNELS;
            memcpy(&table.channel_table,wma10d_channel_layouts,sizeof(wma10d_channel_layouts));
            memcpy(value,&table,sizeof(CHAN_TABLE));
            break;
        default:
        {
            LOGV("unknown index 0x%x", index);
            ret = C2_OMITTED;
            break;
        }
    }
    return ret;

}


class IMXC2WmaDecFactory : public C2ComponentFactory {
public:
    IMXC2WmaDecFactory(C2String name) : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
            GetImxC2Store()->getParamReflector()))
    {
            if(!name.empty())
                mCodecName.assign(name);
            else
                mCodecName.assign("c2.imx.wma.decoder.sw");
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {

            auto impl = std::make_shared<WmaDecodeUtil::IntfImpl>(mHelper, mCodecName.c_str());
            WmaDecodeUtil * pUtil = new WmaDecodeUtil(mCodecName, impl);

            std::shared_ptr<C2ComponentInterface> intf = std::make_shared<IMXC2Interface<WmaDecodeUtil::IntfImpl>>(mCodecName.c_str(), id, impl);
            pUtil->mIntf2 = intf;

        *component = std::shared_ptr<C2Component>(
                new UniaDecoder(intf, pUtil),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new IMXInterface<WmaDecodeUtil::IntfImpl>(
                        mCodecName, id, std::make_shared<WmaDecodeUtil::IntfImpl>(mHelper, mCodecName.c_str())),
                deleter);
        return C2_OK;
    }

    virtual ~IMXC2WmaDecFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
    std::string mCodecName;
};

}  // namespace android

extern "C" ::C2ComponentFactory* IMXCreateCodec2Factory(C2String name) {
    return new ::android::IMXC2WmaDecFactory(name);
}

extern "C" void IMXDestroyCodec2Factory(::C2ComponentFactory* factory) {
    delete factory;
}

