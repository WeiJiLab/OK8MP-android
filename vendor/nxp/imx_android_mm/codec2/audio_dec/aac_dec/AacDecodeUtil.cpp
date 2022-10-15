/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "AacDecUtil"
#include <log/log.h>

#include <AacFrameParser.h>
#include <C2Config_imx.h>
#include <IMXC2Interface.h>
#include <media/stagefright/foundation/MediaDefs.h>
#include <UniaDecoder.h>
#include <C2ComponentFactory.h>
#include <C2PlatformSupport.h>

#include "AacDecodeUtil.h"

#undef bool
#define bool bool

namespace android {

#define AACD_FRAME_SIZE  1024
#define AAC_PUSH_MODE_LEN   (6*768+7)//max frame lenth + another frame header size
#define ADIF_FILE 0x41444946
#define DSP_WRAPPER_LIB_NAME "lib_dsp_wrap_arm12_android.so"

#define AAC_MAX_CHANNELS 8
/* pcm channgel layout for AAC*/
static uint32 aacd_1channel_layout[] = {
    /* FC */
    UA_CHANNEL_FRONT_CENTER
};

static uint32 aacd_2channel_layout[] = {
    /* FL,FR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT
};

static uint32 aacd_3channel_layout[] = {
    /* FL,FR,FC */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER
};

static uint32 aacd_4channel_layout[] = {
    /* FC,FCL,FCR,BC */
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_FRONT_LEFT_CENTER,
    UA_CHANNEL_FRONT_RIGHT_CENTER,
    UA_CHANNEL_REAR_CENTER
};

static uint32 aacd_5channel_layout[] = {
    /* FL,FR,FC,BL,BR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT
};

static uint32 aacd_6channel_layout[] = {
    /* FL,FR,FC,LFE,BL,BR */
    UA_CHANNEL_FRONT_LEFT,
    UA_CHANNEL_FRONT_RIGHT,
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_LFE,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT,
};

static uint32 aacd_8channel_layout[] = {
    /* FC,LFE,,BL,BR,FCL,FCR,SL,SR */
    UA_CHANNEL_FRONT_CENTER,
    UA_CHANNEL_LFE,
    UA_CHANNEL_REAR_LEFT,
    UA_CHANNEL_REAR_RIGHT,
    UA_CHANNEL_FRONT_LEFT_CENTER,
    UA_CHANNEL_FRONT_RIGHT_CENTER,
    UA_CHANNEL_SIDE_LEFT,
    UA_CHANNEL_SIDE_RIGHT
};

static uint32 * aacd_channel_layouts[] = {
    NULL,
    aacd_1channel_layout, // 1
    aacd_2channel_layout, // 2
    aacd_3channel_layout,
    aacd_4channel_layout,
    aacd_5channel_layout,
    aacd_6channel_layout,
    NULL,
    aacd_8channel_layout,
};


class AacDecodeUtil::IntfImpl : public IMXInterface<void>::BaseParams {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper, C2String componentName)
        : IMXInterface<void>::BaseParams(
                helper,
                componentName,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_AUDIO,
                MEDIA_MIMETYPE_AUDIO_AAC) {
        noPrivateBuffers();
        noInputReferences();
        noOutputReferences();
        noInputLatency();
        noTimeStretch();

        addParameter(
                DefineParam(mSampleRate, C2_PARAMKEY_SAMPLE_RATE)
                .withDefault(new C2StreamSampleRateInfo::output(0u, 44100))
                .withFields({C2F(mSampleRate, value).oneOf({
                    7350, 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
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

        addParameter(
                DefineParam(mAacFormat, C2_PARAMKEY_AAC_PACKAGING)
                .withDefault(new C2StreamAacFormatInfo::input(0u, C2Config::AAC_PACKAGING_RAW))
                .withFields({C2F(mAacFormat, value).oneOf({
                    C2Config::AAC_PACKAGING_RAW, C2Config::AAC_PACKAGING_ADTS, (C2Config::aac_packaging_t)AAC_PACKAGING_ADIF
                })})
                .withSetter(Setter<decltype(*mAacFormat)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                .withDefault(new C2StreamProfileLevelInfo::input(0u,
                        C2Config::PROFILE_AAC_LC, C2Config::LEVEL_UNUSED))
                .withFields({
                    C2F(mProfileLevel, profile).oneOf({
                            C2Config::PROFILE_AAC_LC,
                            C2Config::PROFILE_AAC_HE,
                            C2Config::PROFILE_AAC_HE_PS}),
                    C2F(mProfileLevel, level).oneOf({
                            C2Config::LEVEL_UNUSED
                    })
                })
                .withSetter(ProfileLevelSetter)
                .build());

        addParameter(
                DefineParam(mLowLatency, C2_PARAMKEY_VENDOR_LOW_LATENCY)
                .withDefault(new C2StreamVendorLowLatency::input(0u, 1))
                .withFields({C2F(mLowLatency, value).inRange(0, 1)})
                .withSetter(Setter<decltype(*mLowLatency)>::NonStrictValueWithNoDeps)
                .build());

    }

    static C2R ProfileLevelSetter(bool mayBlock, C2P<C2StreamProfileLevelInfo::input> &me) {
        (void)mayBlock;
        (void)me;  // TODO: validate
        return C2R::Ok();
    }

    uint32_t getSampleRate() const { return mSampleRate->value; };
    uint32_t getChannelCount() const { return mChannelCount->value; };
    uint32_t getBitrate() const { return mBitrate->value; };
    C2Config::aac_packaging_t getAacFormat() const { return mAacFormat->value; };
    void setSampleRate(uint32_t value) { mSampleRate->value = value; };
    void setChannelCount(uint32_t value) { mChannelCount->value = value; };
    void setBitrate(uint32_t value) { mBitrate->value = value; };
    void setAacFormat(C2Config::aac_packaging_t fmt) { mAacFormat->value = fmt; };
    bool isLowLatency() { return (mLowLatency->value == 1);};

private:
    std::shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    std::shared_ptr<C2StreamChannelCountInfo::output> mChannelCount;
    std::shared_ptr<C2StreamBitrateInfo::input> mBitrate;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    std::shared_ptr<C2StreamAacFormatInfo::input> mAacFormat;
    std::shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;
    std::shared_ptr<C2StreamVendorLowLatency::input> mLowLatency;
};

AacDecodeUtil::AacDecodeUtil(std::string & codecName, const std::shared_ptr<IntfImpl> &intfImpl)
    : AudioDecodeUtil(),
    bFrameChecked(false),
    mIntf(intfImpl)
{
    LOGV("entry %p", this);
    if(codecName.find("c2.imx.aac.decoder.sw", 0) != std::string::npos){
        wrapperLibName = "lib_aacd_wrap_arm12_elinux_android.so";
        optionalWrapperLibName = "lib_aacplusd_wrap_arm12_elinux_android.so";
    }
    else if(codecName.find("c2.imx.aac.decoder.hw", 0) != std::string::npos){
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

AacDecodeUtil::~AacDecodeUtil()
{
    LOGV("entry");
}

c2_status_t AacDecodeUtil::getLibName(const char ** lib, const char ** optionalLib)
{
    LOGV("entry");
    *lib = wrapperLibName;
    *optionalLib = optionalWrapperLibName;
    return C2_OK;
}

uint32_t AacDecodeUtil::getFrameHdrBufLen()
{
    LOGV("entry");

    return mIntf->isLowLatency() ? 1 : AAC_PUSH_MODE_LEN;

}

uint32_t AacDecodeUtil::getOutBufferLen()
{
    LOGV("entry");
    return 6*AACD_FRAME_SIZE*2*4;
}

c2_status_t AacDecodeUtil::checkFrameHeader(unsigned char * pBuffer, size_t length, uint32_t *pOffset)
{
    LOGV("entry");
    uint32_t nActuralLen = 0;
    uint32_t nVal = 0;
    AUDIO_FRAME_INFO FrameInfo;
    memset(&FrameInfo, 0, sizeof(AUDIO_FRAME_INFO));

    if(bFrameChecked){
        return C2_OK;
    } else if (mIntf->isLowLatency()) {
        if (mIntf->getAacFormat() == C2Config::AAC_PACKAGING_RAW) {
            mFrameInput = true;
        }
        bFrameChecked = true;
        LOGI("low latency mode, mFrameInput %d", mFrameInput);
        return C2_OK;
    }

    do{
        LOGI("data length: %zu\n", length);

        nVal = *pBuffer<<24|*(pBuffer+1)<<16|*(pBuffer+2)<<8|*(pBuffer+3);

        if(nVal == ADIF_FILE){
            LOGI("ADIF_FILE");
            mFrameInput = false;
            bFrameChecked = true;
            mIntf->setAacFormat((C2Config::aac_packaging_t)AAC_PACKAGING_ADIF);
            break;
        }

        if(AFP_SUCCESS != AacCheckFrame(&FrameInfo, pBuffer, length)){
            LOGE("CHECK FAILED");
            break;
        }

        if(FrameInfo.bGotOneFrame){
            LOGI("ADTS_FILE");
            mIntf->setAacFormat(C2Config::AAC_PACKAGING_ADTS);
            mFrameInput = false;
            bFrameChecked = true;
            break;
        }

        mIntf->setAacFormat(C2Config::AAC_PACKAGING_RAW);
        mFrameInput = true;
        bFrameChecked = true;
        LOGI("AacFormat Raw");
    }while(0);

    return C2_OK;

}
c2_status_t AacDecodeUtil::parseFrame(uint8_t * pBuffer, int len, UniaDecFrameInfo *info)
{
    LOGV("entry");
    AUDIO_FRAME_INFO FrameInfo;

    if(pBuffer == nullptr || info == nullptr || len <= 0)
        return C2_BAD_VALUE;

    memset(&FrameInfo, 0, sizeof(AUDIO_FRAME_INFO));

    if(mIntf->getAacFormat() !=  C2Config::AAC_PACKAGING_RAW){
        if(AFP_SUCCESS == AacCheckFrame(&FrameInfo, pBuffer, len)){
            info->bGotOneFrame = FrameInfo.bGotOneFrame;
            info->nConsumedOffset = FrameInfo.nConsumedOffset;
            info->nHeaderCount = FrameInfo.nHeaderCount;
            info->nHeaderSize = FrameInfo.nHeaderSize;
            info->nFrameSize = FrameInfo.nFrameSize;
            info->nNextSize = FrameInfo.nNextFrameSize;
        }
    }else{
        info->bGotOneFrame = true;
        info->nFrameSize = len;
        info->nNextSize = len/2;
    }

    return C2_OK;

}

c2_status_t AacDecodeUtil::getDecoderProp(AUDIOFORMAT *formatType, int *isHwBased)
{
    LOGV("entry");
    if (formatType)
        *formatType = AAC;
    if (isHwBased)
        *isHwBased = !strcmp(wrapperLibName, DSP_WRAPPER_LIB_NAME);

    return C2_OK;
}

c2_status_t AacDecodeUtil::handleBOS(uint32_t* offset, uint32_t length) {
    return C2_OK;
}

c2_status_t AacDecodeUtil::handleEOS(uint8_t **ppBuffer, uint32_t* length)
{
    LOGV("entry");
    uint32_t padding = 0;

    // pad the end of the stream with one buffer of which the value are all 2(avoid gap/overlap),
    // since that the actual last buffer isn't sent by aac decoder.
    padding = AACD_FRAME_SIZE * mIntf->getChannelCount() * sizeof(int16_t);
    // TODO:
    //memset(pOutBufferHdr->pBuffer + pOutBufferHdr->nOffset + pOutBufferHdr->nFilledLen, 2, padding);
    //pOutBufferHdr->nFilledLen += padding;

    return C2_OK;
}

c2_status_t AacDecodeUtil::setParameter(UA_ParaType index,int32_t value)
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

c2_status_t AacDecodeUtil::getParameter(UA_ParaType index,int32_t * value)
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
        case UNIA_STREAM_TYPE:
        {
            C2Config::aac_packaging_t fmt = mIntf->getAacFormat();
            if(fmt == C2Config::AAC_PACKAGING_ADTS){
                *value = STREAM_ADTS;
            }else if(fmt == (C2Config::aac_packaging_t)AAC_PACKAGING_ADIF){
                *value = STREAM_ADIF;
            }else if(fmt == C2Config::AAC_PACKAGING_RAW){
                *value = STREAM_RAW;
                mFrameInput = true;
            }else{
                *value = STREAM_UNKNOW;
            }
            LOGI("stream type %s",*value == STREAM_RAW ?"RAW":(*value == STREAM_ADTS?"ADTS"\
                :(*value == STREAM_ADIF?"ADIF":"UNKNOWN")));
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
            table.size = AAC_MAX_CHANNELS;
            memcpy(&table.channel_table,aacd_channel_layouts,sizeof(aacd_channel_layouts));
            memcpy(value,&table,sizeof(CHAN_TABLE));
            LOGV("map table");
            break;
        }
        default:
        {
            LOGV("unknown index %d", index);
            ret = C2_OMITTED;
            break;
        }
    }
    return ret;

}

size_t AacDecodeUtil::getPushModeInputLen()
{
    return AAC_PUSH_MODE_LEN;
}


class IMXC2AacDecFactory : public C2ComponentFactory {
public:
    IMXC2AacDecFactory(C2String name) : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
            GetImxC2Store()->getParamReflector()))
    {
            if(!name.empty())
                mCodecName.assign(name);
            else
                mCodecName.assign("c2.imx.aac.decoder.sw");
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {

            auto impl = std::make_shared<AacDecodeUtil::IntfImpl>(mHelper, mCodecName.c_str());
            AacDecodeUtil * pAacUtil = new AacDecodeUtil(mCodecName, impl);

        *component = std::shared_ptr<C2Component>(
                new UniaDecoder(std::make_shared<IMXC2Interface<AacDecodeUtil::IntfImpl>>(mCodecName.c_str(), id, impl),
                                            pAacUtil),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new IMXC2Interface<AacDecodeUtil::IntfImpl>(
                        mCodecName, id, std::make_shared<AacDecodeUtil::IntfImpl>(mHelper, mCodecName.c_str())),
                deleter);
        return C2_OK;
    }

    virtual ~IMXC2AacDecFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
    std::string mCodecName;
};

}  // namespace android

extern "C" ::C2ComponentFactory* IMXCreateCodec2Factory(C2String name) {
    return new ::android::IMXC2AacDecFactory(name);
}

extern "C" void IMXDestroyCodec2Factory(::C2ComponentFactory* factory) {
    delete factory;
}

