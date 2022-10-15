/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "IMXC2VideoEncoder"
#include <atomic>
#include <log/log.h>
#include <utils/misc.h>

#include <media/hardware/VideoAPI.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/foundation/AUtils.h>

#include <C2Debug.h>
#include <C2PlatformSupport.h>
#include <util/C2InterfaceHelper.h>
#include <Codec2Mapper.h>

#include "IMXC2Interface.h"
#include "IMXC2VideoEncoder.h"
#include "vpu_wrapper.h"
#include "graphics_ext.h"
#include "Memory.h"
#include "IMXUtils.h"

#include <sys/mman.h>

namespace android {

static std::atomic<std::int32_t> gEncoderInstance = 0;


#define CHECK_AND_RETURN_C2_ERR(err) if((err) != OK) {ALOGE("%s, line %d", __FUNCTION__, __LINE__); return (((err) == OK) ? C2_OK : C2_CORRUPTED);}
#define C2ERR(err) ((err) == OK ? C2_OK : C2_CORRUPTED)

//#define IMX_VIDEO_ENC_TRACE
#ifdef IMX_VIDEO_ENC_TRACE
#define IMX_VIDEO_ENC_API_TRACE ALOGD
#else
#define IMX_VIDEO_ENC_API_TRACE ALOGV
#endif

FrameConfig::FrameConfig(FrameConfig *pCfg)
    : mIndex(pCfg->mIndex),
      mBitrate(pCfg->mBitrate),
      mRequestSync(pCfg->mRequestSync){

}
FrameConfig::FrameConfig(uint64_t index, uint32_t bitrate, uint32_t requestSync)
    : mIndex(index),
      mBitrate(bitrate),
      mRequestSync(requestSync){
}

class IMXC2VideoEncoder::IntfImpl : public IMXInterface<void>::BaseParams {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper, C2String componentName)
        : IMXInterface<void>::BaseParams(
                helper,
                componentName,
                C2Component::KIND_ENCODER,
                C2Component::DOMAIN_VIDEO,
                Name2MimeType(componentName.c_str())),
                mComponentName(componentName) {

        C2String mimeType(Name2MimeType(mComponentName.c_str()));
        char socId[20];
        uint32_t minWidth = 64, minHeight = 64;
        if (0 == GetSocId(socId, sizeof(socId))) {
            if ((!strncmp(socId, "i.MX8QM", 7)) || (!strncmp(socId, "i.MX8QXP", 8))) {
                minWidth = 128;
                minHeight = 128;
            } else if (!strncmp(socId, "i.MX8MM", 7)) {
                minWidth = 132;
                minHeight = 96;
            }
        }

        noPrivateBuffers(); // TODO: account for our buffers here
        noInputReferences();
        noOutputReferences();
        noTimeStretch();
        setDerivedInstance(this);

        addParameter(
                DefineParam(mUsage, C2_PARAMKEY_INPUT_STREAM_USAGE)
                .withConstValue(new C2StreamUsageTuning::input(
                        0u, 0))
                .build());

        addParameter(
                DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                .withConstValue(new C2ComponentAttributesSetting(
                    C2Component::ATTRIB_IS_TEMPORAL))
                .build());

        addParameter(
                DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
                .withDefault(new C2StreamPictureSizeInfo::input(0u, 320, 240))
                .withFields({
                    C2F(mSize, width).inRange(minWidth, 1920, 2),
                    C2F(mSize, height).inRange(minHeight, 1088, 2),
                })
                .withSetter(SizeSetter)
                .build());

        addParameter(
                DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
                .withDefault(new C2StreamPixelFormatInfo::input(
                                     0u, HAL_PIXEL_FORMAT_YCBCR_420_888))
                .withFields({C2F(mPixelFormat, value).inRange(0, 0xffffffff)})
                .withSetter(
                    Setter<decltype(*mPixelFormat)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mColorAspects, C2_PARAMKEY_COLOR_ASPECTS)
                .withDefault(new C2StreamColorAspectsInfo::output(
                        0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                        C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                .withFields({
                    C2F(mColorAspects, range).inRange(
                                C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                    C2F(mColorAspects, primaries).inRange(
                                C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                    C2F(mColorAspects, transfer).inRange(
                                C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                    C2F(mColorAspects, matrix).inRange(
                                C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
                })
                .withSetter(ColorAspectsSetter)
                .build());

        addParameter(
                DefineParam(mGop, C2_PARAMKEY_GOP)
                .withDefault(C2StreamGopTuning::output::AllocShared(
                        0 /* flexCount */, 0u /* stream */))
                .withFields({C2F(mGop, m.values[0].type_).any(),
                             C2F(mGop, m.values[0].count).any()})
                .withSetter(GopSetter)
                .build());

        addParameter(
                DefineParam(mActualInputDelay, C2_PARAMKEY_INPUT_DELAY)
                .withDefault(new C2PortActualDelayTuning::input(DEFAULT_B_FRAMES))
                .withFields({C2F(mActualInputDelay, value).inRange(0, MAX_B_FRAMES)})
                //.calculatedAs(InputDelaySetter, mGop)
                .withSetter(
                    Setter<decltype(*mActualInputDelay)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mFrameRate, C2_PARAMKEY_FRAME_RATE)
                .withDefault(new C2StreamFrameRateInfo::output(0u, 30.))
                // TODO: More restriction?
                .withFields({C2F(mFrameRate, value).greaterThan(0.)})
                .withSetter(Setter<decltype(*mFrameRate)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mBitrate, C2_PARAMKEY_BITRATE)
                .withDefault(new C2StreamBitrateInfo::output(0u, 64000))
                .withFields({C2F(mBitrate, value).inRange(4096, 12000000)})
                .withSetter(BitrateSetter)
                .build());

        addParameter(
                DefineParam(mIntraRefresh, C2_PARAMKEY_INTRA_REFRESH)
                .withDefault(new C2StreamIntraRefreshTuning::output(
                        0u, C2Config::INTRA_REFRESH_DISABLED, 0.))
                .withFields({
                    C2F(mIntraRefresh, mode).oneOf({
                        C2Config::INTRA_REFRESH_DISABLED, C2Config::INTRA_REFRESH_ARBITRARY }),
                    C2F(mIntraRefresh, period).any()
                })
                .withSetter(IntraRefreshSetter)
                .build());

        if (mimeType ==  MEDIA_MIMETYPE_VIDEO_AVC) {
            addParameter(
                DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                .withDefault(new C2StreamProfileLevelInfo::output(
                        0u, PROFILE_AVC_MAIN, LEVEL_AVC_4_1))
                .withFields({
                    C2F(mProfileLevel, profile).oneOf({
                        PROFILE_AVC_BASELINE,
                        PROFILE_AVC_CONSTRAINED_BASELINE,
                        PROFILE_AVC_MAIN,
                        PROFILE_AVC_HIGH
                    }),
                    C2F(mProfileLevel, level).oneOf({
                        LEVEL_AVC_1, LEVEL_AVC_1B,
                        LEVEL_AVC_1_1, LEVEL_AVC_1_2,
                        LEVEL_AVC_1_3, LEVEL_AVC_2,
                        LEVEL_AVC_2_1, LEVEL_AVC_2_2,
                        LEVEL_AVC_3, LEVEL_AVC_3_1,
                        LEVEL_AVC_3_2, LEVEL_AVC_4,
                        LEVEL_AVC_4_1, LEVEL_AVC_4_2,
                        LEVEL_AVC_5, LEVEL_AVC_5_1
                    }),
                })
                .withSetter(AvcProfileLevelSetter, mSize, mFrameRate, mBitrate)
                .build());
        } else if (mimeType ==  MEDIA_MIMETYPE_VIDEO_HEVC) {
            addParameter(
                DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                .withDefault(new C2StreamProfileLevelInfo::output(
                        0u, PROFILE_HEVC_MAIN, LEVEL_HEVC_MAIN_1))
                .withFields({
                    C2F(mProfileLevel, profile).oneOf({
                        C2Config::PROFILE_HEVC_MAIN,
                        C2Config::PROFILE_HEVC_MAIN_10,
                        C2Config::PROFILE_HEVC_MAIN_STILL
                    }),
                    C2F(mProfileLevel, level).oneOf({
                        LEVEL_HEVC_MAIN_1, LEVEL_HEVC_MAIN_2,
                        LEVEL_HEVC_MAIN_2_1, LEVEL_HEVC_MAIN_3,
                        LEVEL_HEVC_MAIN_3_1, LEVEL_HEVC_MAIN_4,
                        LEVEL_HEVC_MAIN_4_1, LEVEL_HEVC_MAIN_5,
                        LEVEL_HEVC_MAIN_5_1
                    }),
                })
                .withSetter(HevcProfileLevelSetter, mSize, mFrameRate, mBitrate)
                .build());
        }

        addParameter(
                DefineParam(mRequestSync, C2_PARAMKEY_REQUEST_SYNC_FRAME)
                .withDefault(new C2StreamRequestSyncFrameTuning::output(0u, C2_FALSE))
                .withFields({C2F(mRequestSync, value).oneOf({ C2_FALSE, C2_TRUE }) })
                .withSetter(Setter<decltype(*mRequestSync)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mSyncFramePeriod, C2_PARAMKEY_SYNC_FRAME_INTERVAL)
                .withDefault(new C2StreamSyncFrameIntervalTuning::output(0u, 1000000))
                .withFields({C2F(mSyncFramePeriod, value).any()})
                .withSetter(Setter<decltype(*mSyncFramePeriod)>::StrictValueWithNoDeps)
                .build());
    }

 #if 0  // enable later if needed
    static C2R InputDelaySetter(
            bool mayBlock,
            C2P<C2PortActualDelayTuning::input> &me,
            const C2P<C2StreamGopTuning::output> &gop) {
        (void)mayBlock;
        uint32_t maxBframes = 0;
        ParseGop(gop.v, nullptr, nullptr, &maxBframes);
        me.set().value = maxBframes;
        return C2R::Ok();
    }
#endif
    static C2R BitrateSetter(bool mayBlock, C2P<C2StreamBitrateInfo::output> &me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (me.v.value <= 4096) {
            me.set().value = 4096;
        }
        return res;
    }

    static C2R SizeSetter(bool mayBlock, const C2P<C2StreamPictureSizeInfo::input> &oldMe,
                          C2P<C2StreamPictureSizeInfo::input> &me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (!me.F(me.v.width).supportsAtAll(me.v.width)) {
            res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.width)));
            me.set().width = oldMe.v.width;
        }
        if (!me.F(me.v.height).supportsAtAll(me.v.height)) {
            res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.height)));
            me.set().height = oldMe.v.height;
        }
        return res;
    }

    static C2R ColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::output> &me) {
        (void)mayBlock;
        return C2R::Ok();
    }

    static C2R AvcProfileLevelSetter(
            bool mayBlock,
            C2P<C2StreamProfileLevelInfo::output> &me,
            const C2P<C2StreamPictureSizeInfo::input> &size,
            const C2P<C2StreamFrameRateInfo::output> &frameRate,
            const C2P<C2StreamBitrateInfo::output> &bitrate) {
        (void)mayBlock;
        if (!me.F(me.v.profile).supportsAtAll(me.v.profile)) {
            me.set().profile = PROFILE_AVC_CONSTRAINED_BASELINE;
        }

        struct LevelLimits {
            C2Config::level_t level;
            float mbsPerSec;
            uint64_t mbs;
            uint32_t bitrate;
        };
        constexpr LevelLimits kLimits[] = {
            { LEVEL_AVC_1,     1485,    99,     64000 },
            // Decoder does not properly handle level 1b.
            // { LEVEL_AVC_1B,    1485,   99,   128000 },
            { LEVEL_AVC_1_1,   3000,   396,    192000 },
            { LEVEL_AVC_1_2,   6000,   396,    384000 },
            { LEVEL_AVC_1_3,  11880,   396,    768000 },
            { LEVEL_AVC_2,    11880,   396,   2000000 },
            { LEVEL_AVC_2_1,  19800,   792,   4000000 },
            { LEVEL_AVC_2_2,  20250,  1620,   4000000 },
            { LEVEL_AVC_3,    40500,  1620,  10000000 },
            { LEVEL_AVC_3_1, 108000,  3600,  14000000 },
            { LEVEL_AVC_3_2, 216000,  5120,  20000000 },
            { LEVEL_AVC_4,   245760,  8192,  20000000 },
            { LEVEL_AVC_4_1, 245760,  8192,  50000000 },
            { LEVEL_AVC_4_2, 522240,  8704,  50000000 },
            { LEVEL_AVC_5,   589824, 22080, 135000000 },
        };

        uint64_t mbs = uint64_t((size.v.width + 15) / 16) * ((size.v.height + 15) / 16);
        float mbsPerSec = float(mbs) * frameRate.v.value;

        // Check if the supplied level meets the MB / bitrate requirements. If
        // not, update the level with the lowest level meeting the requirements.

        bool found = false;
        // By default needsUpdate = false in case the supplied level does meet
        // the requirements. For Level 1b, we want to update the level anyway,
        // so we set it to true in that case.
        bool needsUpdate = (me.v.level == LEVEL_AVC_1B);
        for (const LevelLimits &limit : kLimits) {
            if (mbs <= limit.mbs && mbsPerSec <= limit.mbsPerSec &&
                    bitrate.v.value <= limit.bitrate) {
                // This is the lowest level that meets the requirements, and if
                // we haven't seen the supplied level yet, that means we don't
                // need the update.
                if (needsUpdate) {
                    ALOGD("Given level %x does not cover current configuration: "
                          "adjusting to %x", me.v.level, limit.level);
                    me.set().level = limit.level;
                }
                found = true;
                break;
            }
            if (me.v.level == limit.level) {
                // We break out of the loop when the lowest feasible level is
                // found. The fact that we're here means that our level doesn't
                // meet the requirement and needs to be updated.
                needsUpdate = true;
            }
        }
        if (!found) {
            // We set to the highest supported level.
            me.set().level = LEVEL_AVC_5;
        }

        return C2R::Ok();
    }

    static C2R HevcProfileLevelSetter(
            bool mayBlock,
            C2P<C2StreamProfileLevelInfo::output> &me,
            const C2P<C2StreamPictureSizeInfo::input> &size,
            const C2P<C2StreamFrameRateInfo::output> &frameRate,
            const C2P<C2StreamBitrateInfo::output> &bitrate) {
        (void)mayBlock;
        if (!me.F(me.v.profile).supportsAtAll(me.v.profile)) {
            me.set().profile = PROFILE_HEVC_MAIN;
        }

        struct LevelLimits {
            C2Config::level_t level;
            uint64_t samplesPerSec;
            uint64_t samples;
            uint32_t bitrate;
        };

        constexpr LevelLimits kLimits[] = {
            { LEVEL_HEVC_MAIN_1,       552960,    36864,    128000 },
            { LEVEL_HEVC_MAIN_2,      3686400,   122880,   1500000 },
            { LEVEL_HEVC_MAIN_2_1,    7372800,   245760,   3000000 },
            { LEVEL_HEVC_MAIN_3,     16588800,   552960,   6000000 },
            { LEVEL_HEVC_MAIN_3_1,   33177600,   983040,  10000000 },
            { LEVEL_HEVC_MAIN_4,     66846720,  2228224,  12000000 },
            { LEVEL_HEVC_MAIN_4_1,  133693440,  2228224,  20000000 },
            { LEVEL_HEVC_MAIN_5,    267386880,  8912896,  25000000 },
            { LEVEL_HEVC_MAIN_5_1,  534773760,  8912896,  40000000 },
            { LEVEL_HEVC_MAIN_5_2, 1069547520,  8912896,  60000000 },
            { LEVEL_HEVC_MAIN_6,   1069547520, 35651584,  60000000 },
            { LEVEL_HEVC_MAIN_6_1, 2139095040, 35651584, 120000000 },
            { LEVEL_HEVC_MAIN_6_2, 4278190080, 35651584, 240000000 },
        };

        uint64_t samples = size.v.width * size.v.height;
        uint64_t samplesPerSec = samples * frameRate.v.value;

        // Check if the supplied level meets the MB / bitrate requirements. If
        // not, update the level with the lowest level meeting the requirements.

        bool found = false;
        // By default needsUpdate = false in case the supplied level does meet
        // the requirements.
        bool needsUpdate = false;
        for (const LevelLimits &limit : kLimits) {
            if (samples <= limit.samples && samplesPerSec <= limit.samplesPerSec &&
                    bitrate.v.value <= limit.bitrate) {
                // This is the lowest level that meets the requirements, and if
                // we haven't seen the supplied level yet, that means we don't
                // need the update.
                if (needsUpdate) {
                    ALOGD("Given level %x does not cover current configuration: "
                          "adjusting to %x", me.v.level, limit.level);
                    me.set().level = limit.level;
                }
                found = true;
                break;
            }
            if (me.v.level == limit.level) {
                // We break out of the loop when the lowest feasible level is
                // found. The fact that we're here means that our level doesn't
                // meet the requirement and needs to be updated.
                needsUpdate = true;
            }
        }
        if (!found) {
            // We set to the highest supported level.
            me.set().level = LEVEL_HEVC_MAIN_5_2;
        }
        return C2R::Ok();
    }

    static C2R IntraRefreshSetter(bool mayBlock, C2P<C2StreamIntraRefreshTuning::output> &me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (me.v.period < 1) {
            me.set().mode = C2Config::INTRA_REFRESH_DISABLED;
            me.set().period = 0;
        } else {
            // only support arbitrary mode (cyclic in our case)
            me.set().mode = C2Config::INTRA_REFRESH_ARBITRARY;
        }
        return res;
    }

    static C2R GopSetter(bool mayBlock, C2P<C2StreamGopTuning::output> &me) {
        (void)mayBlock;
        for (size_t i = 0; i < me.v.flexCount(); ++i) {
            const C2GopLayerStruct &layer = me.v.m.values[0];
            if (layer.type_ == C2Config::picture_type_t(P_FRAME | B_FRAME)
                    && layer.count > MAX_B_FRAMES) {
                me.set().m.values[i].count = MAX_B_FRAMES;
            }
        }
        return C2R::Ok();
    }

    uint32_t getProfile_l() const {
        return mProfileLevel->profile;
    }

    uint32_t getLevel_l() const {
        struct Level {
            C2Config::level_t c2Level;
            uint32_t avcLevel;
        };
        constexpr Level levels[] = {
            { LEVEL_AVC_1,   10 },
            { LEVEL_AVC_1B,   9 },
            { LEVEL_AVC_1_1, 11 },
            { LEVEL_AVC_1_2, 12 },
            { LEVEL_AVC_1_3, 13 },
            { LEVEL_AVC_2,   20 },
            { LEVEL_AVC_2_1, 21 },
            { LEVEL_AVC_2_2, 22 },
            { LEVEL_AVC_3,   30 },
            { LEVEL_AVC_3_1, 31 },
            { LEVEL_AVC_3_2, 32 },
            { LEVEL_AVC_4,   40 },
            { LEVEL_AVC_4_1, 41 },
            { LEVEL_AVC_4_2, 42 },
            { LEVEL_AVC_5,   50 },
        };
        for (const Level &level : levels) {
            if (mProfileLevel->level == level.c2Level) {
                return level.avcLevel;
            }
        }
        ALOGD("Unrecognized level: %x", mProfileLevel->level);
        return 41;
    }

    uint32_t getSyncFramePeriod_l() const {
        if (mSyncFramePeriod->value < 0 || mSyncFramePeriod->value == INT64_MAX) {
            return 0;
        }
        double period = mSyncFramePeriod->value / 1e6 * mFrameRate->value;
        return (uint32_t)c2_max(c2_min(period + 0.5, double(UINT32_MAX)), 1.);
    }

    std::shared_ptr<C2StreamColorAspectsInfo::output> getColorAspects_l() {
        return mColorAspects;
    }

    // unsafe getters
    std::shared_ptr<C2StreamPictureSizeInfo::input> getSize_l() const { return mSize; }
    std::shared_ptr<C2StreamPixelFormatInfo::input> getPixelFormat_l() const { return mPixelFormat; }
    std::shared_ptr<C2StreamIntraRefreshTuning::output> getIntraRefresh_l() const { return mIntraRefresh; }
    std::shared_ptr<C2StreamFrameRateInfo::output> getFrameRate_l() const { return mFrameRate; }
    std::shared_ptr<C2StreamBitrateInfo::output> getBitrate_l() const { return mBitrate; }
    std::shared_ptr<C2StreamRequestSyncFrameTuning::output> getRequestSync_l() const { return mRequestSync; }
    std::shared_ptr<C2StreamGopTuning::output> getGop_l() const { return mGop; }

private:
    C2String mComponentName;
    std::shared_ptr<C2StreamUsageTuning::input> mUsage;
    std::shared_ptr<C2StreamPictureSizeInfo::input> mSize;
    std::shared_ptr<C2StreamPixelFormatInfo::input> mPixelFormat;
    std::shared_ptr<C2StreamColorAspectsInfo::output> mColorAspects;
    std::shared_ptr<C2StreamFrameRateInfo::output> mFrameRate;
    std::shared_ptr<C2StreamRequestSyncFrameTuning::output> mRequestSync;
    std::shared_ptr<C2StreamIntraRefreshTuning::output> mIntraRefresh;
    std::shared_ptr<C2StreamBitrateInfo::output> mBitrate;
    std::shared_ptr<C2StreamProfileLevelInfo::output> mProfileLevel;
    std::shared_ptr<C2StreamSyncFrameIntervalTuning::output> mSyncFramePeriod;
    std::shared_ptr<C2StreamGopTuning::output> mGop;
};


static void fillEmptyWork(const std::unique_ptr<C2Work> &work) {
    ALOGD("fillEmptyWork");

    uint32_t flags = 0;
    if (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) {
        flags |= C2FrameData::FLAG_END_OF_STREAM;
        ALOGV("signalling eos");
    }
    work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.ordinal = work->input.ordinal;
    work->workletsProcessed = 1u;
}

IMXC2VideoEncoder::IMXC2VideoEncoder(
                                    C2String name,
                                    c2_node_id_t id,
                                    const std::shared_ptr<IntfImpl> &intfImpl)
    : IMXC2ComponentBase(std::make_shared<IMXInterface<IntfImpl>>(name, id, intfImpl)),
      mIntf(intfImpl),
      mName(name),
      bGetBlockPool(false),
      bStarted(false),
      bCodecDataReceived(false),
      bPPEnabled(false),
      nOutBufferNum(8),
      nCurInTimestamp(-1),
      nCurOutTimestamp(-1),
      nCurOutFrameIsKey(0),
      nCurOutFrameId(-1),
      nCurOutFrameSize(0){

      mPreProcess = nullptr;
      mEncoder = nullptr;
      gEncoderInstance++;
}

IMXC2VideoEncoder::~IMXC2VideoEncoder() {
    if(gEncoderInstance > 0)
        gEncoderInstance--;
}

c2_status_t IMXC2VideoEncoder::onInit() {

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    {
        IntfImpl::Lock lock = mIntf->lock();
        mSize = mIntf->getSize_l();
        mBitrate = mIntf->getBitrate_l();
        mFrameRate = mIntf->getFrameRate_l();
        mIntraRefresh = mIntf->getIntraRefresh_l();
        mPixelFormat = mIntf->getPixelFormat_l();
    }

    int32_t max_count = 32;
    {
        FILE *f = fopen("/sys/devices/soc0/soc_id", "r");
        if(f != NULL){
            char inputBuf[20];
            if(fgets(inputBuf, sizeof(inputBuf), f) != NULL){
                if(!strncmp(inputBuf, "i.MX8QM", 7)){
                    max_count = 16;
                }else if(!strncmp(inputBuf, "i.MX8QXP", 8)){
                    max_count = 8;
                }
            }
            fclose(f);
        }
    }
    if(gEncoderInstance > max_count)
        return C2_NO_MEMORY;

    return C2_OK;
}

c2_status_t IMXC2VideoEncoder::onStop() {
    status_t err = C2_OK;

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);
    if (mPreProcess) {
        err = mPreProcess->stop();
        CHECK_AND_RETURN_C2_ERR(err);
    }

    if (mEncoder)
        err = mEncoder->stop();

    return C2ERR(err);
}

void IMXC2VideoEncoder::onReset() {

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    // release + init encoder as default
    onRelease();
}

void IMXC2VideoEncoder::onRelease() {

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    releaseComponent();
}

c2_status_t IMXC2VideoEncoder::onFlush_sm() {
    status_t err = OK;

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    if (mPreProcess) {
        err = mPreProcess->flush();
        CHECK_AND_RETURN_C2_ERR(err);
    }

    if (mEncoder) {
        err = mEncoder->flush();
        CHECK_AND_RETURN_C2_ERR(err);
    }

    return C2ERR(err);
}

status_t IMXC2VideoEncoder::getDynamicConfigParam(uint64_t index) {
    bool changed = false;
    uint32_t bitrateValue = 0;
    uint32_t requestSyncValue = 0;

    IntfImpl::Lock lock = mIntf->lock();
    std::shared_ptr<C2StreamIntraRefreshTuning::output> intraRefresh = mIntf->getIntraRefresh_l();
    std::shared_ptr<C2StreamBitrateInfo::output> bitrate = mIntf->getBitrate_l();
    std::shared_ptr<C2StreamRequestSyncFrameTuning::output> requestSync = mIntf->getRequestSync_l();
    lock.unlock();

    if (bitrate != mBitrate) {
        ALOGI("dynamic change bitrate %d -> %d \n", mBitrate->value, bitrate->value);
        mBitrate = bitrate;
        changed = true;
        bitrateValue = mBitrate->value;
    }

    if (requestSync != mRequestSync) {
        // we can handle IDR immediately
        if (requestSync->value) {
            // unset request
            C2StreamRequestSyncFrameTuning::output clearSync(0u, C2_FALSE);
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            mIntf->config({ &clearSync }, C2_MAY_BLOCK, &failures);
            ALOGV("Got sync request");
            requestSyncValue = 1;
            changed = true;
        }
        mRequestSync = requestSync;
    }

    if (changed) {
        ALOGV("config changed: index %lld bitrate %d sync %d", (long long)index, bitrateValue, requestSyncValue);
        Mutexed<FrameConfigQueue>::Locked queue(mFrameCfgQueue);
        FrameConfig config(index, bitrateValue, requestSyncValue);
        queue->push(std::make_unique<FrameConfig>(&config));
    }

    return OK;
}

status_t IMXC2VideoEncoder::setDynamicConfigParam(uint64_t index) {
    std::unique_ptr<FrameConfig> config;
    {
        Mutexed<FrameConfigQueue>::Locked queue(mFrameCfgQueue);
        if (queue->empty())
            return OK;
        if (queue->front()->mIndex != index)
            return OK;

        config = std::move(queue->front());
        queue->pop();
    }

    if (config->mBitrate > 0) {
        ALOGV("set bitrate %d", config->mBitrate);
        mEncoder->setConfig(ENC_CONFIG_BIT_RATE, &(config->mBitrate));
    }

    if (config->mRequestSync) {
        // we can handle IDR immediately
        int intraRefresh = 1;
        mEncoder->setConfig(ENC_CONFIG_INTRA_REFRESH, &intraRefresh);
    }

    return OK;
}

void IMXC2VideoEncoder::processWork(const std::unique_ptr<C2Work> &work) {

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    work->result = C2_OK;
    work->workletsProcessed = 0u;
    work->worklets.front()->output.flags = work->input.flags;

    std::shared_ptr<C2Buffer> inputBuffer = nullptr;

    if (!work->input.buffers.empty()) {
        std::shared_ptr<const C2GraphicView> view;
        inputBuffer = work->input.buffers[0];
        view = std::make_shared<const C2GraphicView>(
            inputBuffer->data().graphicBlocks().front().map().get());
        if (view->error() != C2_OK) {
            ALOGE("graphic view map err = %d", view->error());
            work->result = C2_CORRUPTED;
            work->workletsProcessed = 1u;
            return;
        }
    } else {
        if (bStarted && (work->input.flags & C2FrameData::FLAG_END_OF_STREAM)) {
            drainInternal(DRAIN_COMPONENT_WITH_EOS);
            return;
        } else {
            // fill empty work
            return fillEmptyWork(work);
        }
    }

    const C2ConstGraphicBlock block = inputBuffer->data().graphicBlocks().front();
    fsl::Memory *prvHandle = (fsl::Memory*)block.handle();

    if (mPixelFormat->value == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        mPixelFormat->value = prvHandle->format;
    }

    uint32_t size = mSize->width * mSize->height * pxlfmt2bpp(mPixelFormat->value) / 8;

    if (block.width() < mSize->width || block.height() < mSize->height) {
        ALOGW("unexpected Capacity Aspect %d(%d) x %d(%d)", block.width(), mSize->width, block.height(), mSize->height);
        return;
    }

    if (!bStarted) {
        status_t err = initComponent();
        if (err != OK){
            work->result = C2_CORRUPTED;
            work->workletsProcessed = 1u;
            return;
        }

        bStarted = true;
    }

    int fd = prvHandle->fd;
    uint64_t pPhysAddr = prvHandle->phys;
    uint64_t pVirtAddr = prvHandle->base;
    uint64_t timestamp = work->input.ordinal.timestamp.peeku();
    uint32_t flags = work->input.flags;
    int32_t inputId = static_cast<int32_t>(work->input.ordinal.frameIndex.peeku() & 0x3FFFFFFF);
    bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);

    ALOGV("in buffer virt addr %p phys addr %p size %d timestamp %lld frameindex %d, flags %x, pixel format 0x%x",
          (void*)pVirtAddr, (void*)pPhysAddr, (int)size, (long long)timestamp, inputId, flags, mPixelFormat->value);

    getDynamicConfigParam(timestamp);

    if (bPPEnabled) {
        mPreProcess->queueInput((void*)pVirtAddr, (void*)pPhysAddr, size, timestamp, flags, fd, inputId);
        return;
    }

    IMXInputBuffer inBuffer((void*)pVirtAddr, (void*)pPhysAddr, fd, inputId, size, timestamp, flags, eos);

    status_t ret = encoderQueueBuffer(work, &inBuffer);
    if (ret != OK)
        ALOGW("encoderQueueBuffer failed");

}

c2_status_t IMXC2VideoEncoder::drainInternal(uint32_t drainMode) {

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    // trigger encoding to drain internel input buffers
    if (drainMode == NO_DRAIN) {
        ALOGW("drain with NO_DRAIN: no-op");
        return C2_OK;
    }
    if (drainMode == DRAIN_CHAIN) {
        ALOGW("DRAIN_CHAIN not supported");
        return C2_OMITTED;
    }

    // DRAIN_COMPONENT_WITH_EOS
    if (bPPEnabled) {
        mPreProcess->queueInput(nullptr, nullptr, 0, 0, C2FrameData::FLAG_END_OF_STREAM, -1, -1);
    } else {
        IMXInputBuffer inBuffer(nullptr, nullptr, -1, -1, 0, 0, 0, true);
        mEncoder->queueInput(&inBuffer);
    }

    return C2_OK;
}

status_t IMXC2VideoEncoder::initComponent() {
    status_t err = OK;
    int profile = 0, level = 0;
    int IDRInterval;
    int pixelFmt = mPixelFormat->value; //default value
    std::shared_ptr<C2StreamGopTuning::output> gop;
    std::shared_ptr<C2StreamColorAspectsInfo::output> c2Aspects;
    EncIsoColorAspects colorAspects;

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    const char* mime = Name2MimeType((const char*)mName.c_str());
    if (mime == nullptr) {
        ALOGE("Unsupported component name: %s", mName.c_str());
        return C2_BAD_VALUE;
    }

    mEncoder = CreateVideoEncoderInstance(mime);
    if (!mEncoder) {
        ALOGE("CreateVideoEncoderInstance for mime(%s) failed \n", mime);
        return C2_CORRUPTED;
    }
    err = mEncoder->RegisterLooper();
    if (err) {
        ALOGE("mEncoder register handle fail");
    }


    if (!bGetBlockPool) {
        if (OK != mEncoder->setLinearBlockPool(mOutputBlockPool))
            return BAD_VALUE;
        bGetBlockPool = true;
    }

    {
        IntfImpl::Lock lock = mIntf->lock();
        IDRInterval = (int)mIntf->getSyncFramePeriod_l();
        gop = mIntf->getGop_l();
        c2Aspects = mIntf->getColorAspects_l();

        if (mime == MEDIA_MIMETYPE_VIDEO_HEVC || mime == MEDIA_MIMETYPE_VIDEO_AVC) {
            profile = mIntf->getProfile_l();
            level = mIntf->getLevel_l();
        }
    }

    ColorAspects aspects;
    bool range = false;
    int primaries = 0, matrix = 0, transfer = 0;

    memset(&aspects, 0, sizeof(aspects));
    memset(&colorAspects, 0, sizeof(EncIsoColorAspects));

    if (!C2Mapper::map(c2Aspects.get()->primaries, &aspects.mPrimaries)) {
        aspects.mPrimaries = ColorAspects::PrimariesUnspecified;
    }
    if (!C2Mapper::map(c2Aspects.get()->transfer, &aspects.mTransfer)) {
        aspects.mTransfer = ColorAspects::TransferUnspecified;
    }
    if (!C2Mapper::map(c2Aspects.get()->matrix, &aspects.mMatrixCoeffs)) {
        aspects.mMatrixCoeffs = ColorAspects::MatrixUnspecified;
    }
    if (!C2Mapper::map(c2Aspects.get()->range, &aspects.mRange)) {
        aspects.mRange = ColorAspects::RangeUnspecified;
    }

    ColorUtils::convertCodecColorAspectsToIsoAspects(aspects, &primaries, &transfer, &matrix, &range);

    if (aspects.mRange > 0) {
        colorAspects.videoSignalTypePresentFlag = 1;
        colorAspects.fullRange = (uint32_t)range;
        if (aspects.mPrimaries || aspects.mMatrixCoeffs || aspects.mTransfer) {
            colorAspects.colourDescriptionPresentFlag = 1;
            colorAspects.primaries = primaries;
            colorAspects.matrixCoeffs = matrix;
            colorAspects.transfer = transfer;
        }
    }

    bPPEnabled = mEncoder->checkIfPreProcessNeeded(pixelFmt);


    if (bPPEnabled) {
        // init preprocess component
        mPreProcess = CreatePreProcessInstance();

        if (!mPreProcess) {
            goto RELEASE_ENCODER;
        }

        PROCESSBASE_FORMAT inFmt, outFmt;
        inFmt.width = mSize->width;
        inFmt.height = mSize->height;
        inFmt.format = pixelFmt;
        inFmt.stride = mSize->width;
        inFmt.bufferSize = inFmt.width * inFmt.height * pxlfmt2bpp(inFmt.format) / 8;

        ALOGI("init preprocess w=%d,h=%d,pixelFmt=%x,bufferSize=%d",mSize->width,mSize->height, pixelFmt, inFmt.bufferSize);
        err = mPreProcess->setConfig(PROCESS_CONFIG_INPUT_FORMAT, &inFmt);
        if (err) {
            goto RELEASE_ENCODER;
        }

        err = mPreProcess->init((ProcessBase::Client*)this, mOutputBlockPool);
        if (err) {
            goto RELEASE_ENCODER;
        }

        err = mPreProcess->getConfig(PROCESS_CONFIG_OUTPUT_FORMAT, &outFmt);
        if (err) {
            goto RELEASE_ENCODER;
        }

        pixelFmt = outFmt.format;
    }

    EncInputParam inPara;
    memset(&inPara, 0, sizeof(EncInputParam));
    inPara.eColorFormat = pixelFmt;
    inPara.nPicWidth = mSize->width;
    inPara.nPicHeight = mSize->height;
    inPara.nWidthStride = mSize->width;
    inPara.nHeightStride = mSize->height;
    inPara.nRotAngle = 0;
    inPara.nFrameRate = mFrameRate->value;
    inPara.nBitRate = mBitrate->value;
    inPara.nGOPSize = IDRInterval;
    inPara.nIDRPeriod = IDRInterval;
    inPara.nRefreshIntra = mIntraRefresh->period;
    inPara.bEnabledSPSIDR = true;
    inPara.nRcIntraQP = 0;
    inPara.nProfile = profile;
    inPara.nLevel = level;

    memcpy(&inPara.sColorAspects, &colorAspects, sizeof(EncIsoColorAspects));

    ALOGI("initComponent: res=(%d x %d) fps=%d bitrate=%d GOP=%d pixelFormat %x",
        inPara.nPicWidth, inPara.nPicHeight, inPara.nFrameRate, inPara.nBitRate, inPara.nGOPSize, pixelFmt);
    ALOGI("initComponent: IDRInterval=%d RefreshIntra=%d profile=0x%x level=0x%x",
        inPara.nIDRPeriod, inPara.nRefreshIntra, inPara.nProfile, inPara.nLevel);
    ALOGI("initComponent: sColorAspects: flag %d flag %d rang %d primaries %d transfer %d matrix %d",
        inPara.sColorAspects.videoSignalTypePresentFlag,
        inPara.sColorAspects.colourDescriptionPresentFlag,
        inPara.sColorAspects.fullRange, inPara.sColorAspects.primaries,
        inPara.sColorAspects.transfer, inPara.sColorAspects.matrixCoeffs);

    mEncoder->initEncInputParamter(&inPara);

    err = mEncoder->init((VideoEncoderBase::Client*)this);
    if (err) {
        goto RELEASE_ENCODER;
    }

    return C2_OK;

RELEASE_ENCODER:
    // release encoder if init failed, in case of upper layer don't call release
    releaseComponent();
    return C2_CORRUPTED;

}

status_t IMXC2VideoEncoder::releaseComponent() {

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    {
        Mutexed<FrameConfigQueue>::Locked queue(mFrameCfgQueue);
        while (!queue->empty()) {
            queue->pop();
        }
    }

    if (mPreProcess) {
        mPreProcess->destroy();
        mPreProcess.clear();
    }

    if (mEncoder) {
        mEncoder->destroy();
        mEncoder.clear();
    }

    bStarted = false;
    return OK;
}

status_t IMXC2VideoEncoder::handleOutputFrame(int32_t outFrameId, uint32_t frameSize,
                                                        uint64_t timestamp, int keyFrame, uint32_t offset,
                                                        const std::unique_ptr<C2Work>& work) {
    ALOGV("handleOutputFrame size %d ts %lld key %d work %p", frameSize, (long long)timestamp, keyFrame, work.get());

    LinearBlockInfo* info = mEncoder->getLinearBlockById(outFrameId);
    if (!info) {
        /* notify error */
        ALOGE("%s line %d: wrong pictureId %d", __FUNCTION__, __LINE__, outFrameId);
        return BAD_VALUE;
    }

    if (info->mState != LinearBlockInfo::State::OWNED_BY_VPU) {
        ALOGE("%s line %d: error linear block state, expect OWNED_BY_VPU but get %d", __FUNCTION__, __LINE__, info->mState);
        return BAD_VALUE;
    }

    // unmap this buffer so that it can back to buffer pool
    if (info->mVirtAddr > 0 && info->mCapacity > 0) {
        munmap((void*)info->mVirtAddr, info->mCapacity);
        info->mVirtAddr = 0;
    }

    // finish work
    std::shared_ptr<C2Buffer> buffer = createLinearBuffer(std::move(info->mLinearBlock), offset, frameSize);

    if (keyFrame)
        buffer->setInfo(std::make_shared<C2StreamPictureTypeMaskInfo::output>(0u /* stream id */, C2Config::SYNC_FRAME));

    uint8_t *pCsd = nullptr;
    uint32_t nCsdSize = 0;

    if (!bCodecDataReceived) {
        status_t err = OK;
        err = mEncoder->getCodecData(&pCsd, &nCsdSize);
        if (OK == err && pCsd && nCsdSize > 0) {
            bCodecDataReceived = true;
            ALOGV("receive codecdata, size=%d", nCsdSize);
        }
    }

    auto fillWork = [buffer, timestamp, pCsd, nCsdSize](const std::unique_ptr<C2Work> &work) {
        uint32_t flags = 0;
        if ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) &&
                (c2_cntr64_t(timestamp) == work->input.ordinal.timestamp)) {
            flags |= C2FrameData::FLAG_END_OF_STREAM;
            ALOGV("signalling eos");
        }

        if (pCsd && nCsdSize > 0) {
            std::unique_ptr<C2StreamInitDataInfo::output> csdInfo =
                C2StreamInitDataInfo::output::AllocUnique(nCsdSize, 0u);
            if (csdInfo)
                memcpy(csdInfo->m.value, pCsd, nCsdSize);
            work->worklets.front()->output.configUpdate.push_back(std::move(csdInfo));
        }

        work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.buffers.push_back(buffer);
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->workletsProcessed = 1u;
    };

    if (work) {
        if (pCsd && nCsdSize > 0) {
            std::unique_ptr<C2StreamInitDataInfo::output> csdInfo =
                C2StreamInitDataInfo::output::AllocUnique(nCsdSize, 0u);
            if (csdInfo)
                memcpy(csdInfo->m.value, pCsd, nCsdSize);
            work->worklets.front()->output.configUpdate.push_back(std::move(csdInfo));
        }
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.buffers.push_back(buffer);
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->worklets.front()->output.flags = (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) ?
                                                C2FrameData::FLAG_END_OF_STREAM : (C2FrameData::flags_t)0;
        work->workletsProcessed = 1u;

    } else {
        c2_status_t err = finish(timestamp, fillWork);
        if (C2_NOT_FOUND == err) {
            // need to return this output buffer to encoder because its c2work is flushed.
            mEncoder->returnOutputBufferToEncoder(info->mBlockId);
        }
    }

    info->mState = LinearBlockInfo::State::OWNED_BY_CLIENT;
    info->mLinearBlock.reset();

    return OK;
}

status_t IMXC2VideoEncoder::handleInputUsed(int inputId) {
    nUsedFrameIndex = static_cast<uint64_t>(inputId);

    IMX_VIDEO_ENC_API_TRACE("%s, line %d, input id %d \n", __FUNCTION__, __LINE__, inputId);

    if (nUsedFrameIndex == nCurFrameIndex)
        return OK; // will handle this in processQueue later

    C2Work* work = getPendingWorkByFrameIndex(nUsedFrameIndex);
    if (!work) {
        ALOGW("%s: can't find C2Work for id %d \n", __FUNCTION__, inputId);
        return BAD_VALUE;
    }

    // When the work is done, the input buffer shall be reset by component.
    work->input.buffers.front().reset();
    ALOGV("input id %d is used \n", inputId);

    return OK;
}

status_t IMXC2VideoEncoder::encoderQueueBuffer(const std::unique_ptr<C2Work> &work, IMXInputBuffer* pInBuffer) {
    uint64_t timestamp = pInBuffer->timestamp;

    if (strcmp(MEDIA_MIMETYPE_VIDEO_VP8,Name2MimeType((const char*)mName.c_str())) == 0) {
        uint32_t frameDuration, frameRate;
        uint32_t ticksPerSecond = 1000000;
        if (timestamp > nCurInTimestamp) {
            frameDuration = (uint32_t)(timestamp - nCurInTimestamp);
        } else {
            frameDuration = (uint32_t)ticksPerSecond/mFrameRate->value;
        }

        frameRate = (ticksPerSecond + frameDuration/2) / frameDuration;
        if (frameRate != mFrameRate->value) {
            mFrameRate->value = frameRate;
            // unset request
            C2StreamFrameRateInfo::output newFrameRate(0u, frameRate);
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            mIntf->config({ &newFrameRate }, C2_MAY_BLOCK, &failures);
            ALOGI("got new frame rate  %d\n", frameRate);
            mEncoder->setConfig(ENC_CONFIG_FRAME_RATE, &frameRate);
        }
    }

    setDynamicConfigParam(timestamp);

    ALOGV("encoderQueueBuffer: phys %p ts %lld id %d", pInBuffer->pInputPhys, (long long)pInBuffer->timestamp, pInBuffer->id);

    nCurInTimestamp = timestamp;
    mEncoder->queueInput(pInBuffer);

    ALOGV("encoderQueueBuffer done, in ts %lld, out ts %lld, work %p",
        (long long)nCurInTimestamp, (long long)nCurOutTimestamp, work.get());

#if 0
    if (nCurInTimestamp == nCurOutTimestamp) {
        // input data is encoded done immediately, send it out right now
        if (OK != handleOutputFrame(nCurOutFrameId, nCurOutFrameSize, nCurOutTimestamp, nCurOutFrameIsKey, work)) {
            ALOGW("processWork: handleOutputFrame return bad value");
        }
    }
#endif
    return OK;
}

void IMXC2VideoEncoder::clearOutputFrameBuffer() {

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

}

void IMXC2VideoEncoder::notifyOutputFrameReady(int32_t outFrameId, uint32_t frameSize,
                                                          uint64_t timestamp, int keyFrame, uint32_t offset) {

    IMX_VIDEO_ENC_API_TRACE("%s, line %d \n", __FUNCTION__, __LINE__);

    ALOGV("out frame ready, out ts %lld, in ts %lld", (long long)timestamp, (long long)nCurInTimestamp);
    nCurOutTimestamp = timestamp;
#if 0
    if (nCurOutTimestamp == nCurInTimestamp) {
        // record and handle this output frame later
        nCurOutFrameIsKey = keyFrame;
        nCurOutFrameId = outFrameId;
        nCurOutFrameSize = frameSize;
        return;
    }
#endif

    if (OK != handleOutputFrame(outFrameId, frameSize, timestamp, keyFrame, offset, nullptr)) {
        ALOGW("handleOutputFrame return bad value");
    }
}

void IMXC2VideoEncoder::notifyInputBufferUsed(int32_t input_id) {
    if (bPPEnabled) {
        // return input buffer to preprocessor
        mPreProcess->outputBufferReturned(input_id);
    } else {
        handleInputUsed(input_id);
    }
}

void IMXC2VideoEncoder::notifyFlushDone() {
}

void IMXC2VideoEncoder::notifyResetDone() {
}

void IMXC2VideoEncoder::notifyEos() {
    finishWithException(true/*eos*/, false/*force*/);
}

void IMXC2VideoEncoder::notifyError(status_t err) {
    (void)err;
}

status_t IMXC2VideoEncoder::fetchProcessBuffer(int *bufferId, unsigned long *phys) {
    (void)bufferId;
    (void)phys;
    return OK;
}

status_t IMXC2VideoEncoder::notifyProcessInputUsed(int inputId) {
    return handleInputUsed(inputId);
}

status_t IMXC2VideoEncoder::notifyProcessDone(int outputId, uint64_t timestamp, uint32_t flag) {
    ProcessBlockInfo* pInfo = mPreProcess->getProcessBlockById(outputId);
    if (!pInfo) {
        ALOGE("notifyProcessDone get invalid outputId: %d", outputId);
        return BAD_VALUE;
    }

    IMXInputBuffer inBuffer((void*)pInfo->mVirtAddr, (void*)pInfo->mPhysAddr, pInfo->mFd, pInfo->mBlockId,
                            pInfo->mCapacity, timestamp, flag, false);

    return encoderQueueBuffer(nullptr, &inBuffer);
}


status_t IMXC2VideoEncoder::notifyProcessOutputClear() {
    return OK;
}


status_t IMXC2VideoEncoder::notifyProcessFlushDone() {
    return OK;
}


status_t IMXC2VideoEncoder::notifyProcessResetDone() {
    return OK;
}


void IMXC2VideoEncoder::notifyProcessError() {
}

void IMXC2VideoEncoder::notifyProcessEos() {
    // queue eos to encoder
    IMXInputBuffer inBuffer(nullptr, nullptr, -1, -1, 0, 0, 0, true);
    mEncoder->queueInput(&inBuffer);
}

class IMXC2VideoEncoderFactory : public C2ComponentFactory {
public:
    IMXC2VideoEncoderFactory(C2String name)
        : mHelper(std::static_pointer_cast<C2ReflectorHelper>(GetImxC2Store()->getParamReflector())),
          mComponentName(name) {
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {
        *component = std::shared_ptr<C2Component>(
                new IMXC2VideoEncoder(mComponentName.c_str(),
                                 id,
                                 std::make_shared<IMXC2VideoEncoder::IntfImpl>(mHelper, mComponentName.c_str())),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id,
            std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new IMXInterface<IMXC2VideoEncoder::IntfImpl>(
                        mComponentName, id, std::make_shared<IMXC2VideoEncoder::IntfImpl>(mHelper, mComponentName)),
                deleter);
        return C2_OK;
    }

    virtual ~IMXC2VideoEncoderFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
    C2String mComponentName;
};


extern "C" ::C2ComponentFactory* IMXCreateCodec2Factory(C2String name) {
    ALOGV("in %s", __func__);
    return new ::android::IMXC2VideoEncoderFactory(name);
}

extern "C" void IMXDestroyCodec2Factory(::C2ComponentFactory* factory) {
    ALOGV("in %s", __func__);
    delete factory;
}

} // namespcae android

/* end of file */
