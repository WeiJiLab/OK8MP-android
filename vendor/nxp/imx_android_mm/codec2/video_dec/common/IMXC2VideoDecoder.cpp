/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "IMXC2VideoDecoder"
#include <utils/Log.h>

#include <media/stagefright/MediaDefs.h>
#include <string.h>
#include <Codec2Mapper.h>

#include "IMXC2VideoDecoder.h"
#include "C2_imx.h"
#include "C2Config_imx.h"
#include "IMXUtils.h"
#include "graphics_ext.h"

namespace android {

#define CHECK_AND_RETURN_C2_ERR(err) if((err) != OK) {ALOGE("%s, line %d", __FUNCTION__, __LINE__); return (((err) == OK) ? C2_OK : C2_CORRUPTED);}
#define C2ERR(err) ((err) == OK ? C2_OK : C2_CORRUPTED)

#define DEFAULT_ACTUAL_OUTPUT_DELAY_VALUE 16
#define DEFAULT_OUTPUT_BUFFER_CNT_IN_POST_PROCESS 6

#ifdef IMX_VIDEO_DEC_API_TRACE
#define IMX_VIDEO_DEC_API_TRACE ALOGD
#else
#define IMX_VIDEO_DEC_API_TRACE
#endif


class IMXC2VideoDecoder::IntfImpl : public IMXInterface<void>::BaseParams {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper, C2String componentName)
        : IMXInterface<void>::BaseParams(
                helper,
                componentName,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_VIDEO,
                Name2MimeType(componentName.c_str())),
                mComponentName(componentName) {

        C2String mimeType(Name2MimeType(mComponentName.c_str()));

        noPrivateBuffers(); // TODO: account for our buffers here
        noInputReferences();
        noOutputReferences();
        noInputLatency();
        noTimeStretch();

        // TODO: Proper support for reorder depth.
        /*addParameter(
                DefineParam(mActualInputDelay, C2_PARAMKEY_INPUT_DELAY)
                .withDefault(new C2PortActualDelayTuning::input(3u))
                .withFields({C2F(mActualInputDelay, value).inRange(0, 32)})
                .withSetter(Setter<decltype(*mActualInputDelay)>::StrictValueWithNoDeps)
                .build());*/

        addParameter(
                DefineParam(mActualOutputDelay, C2_PARAMKEY_OUTPUT_DELAY)
                .withDefault(new C2PortActualDelayTuning::output(DEFAULT_ACTUAL_OUTPUT_DELAY_VALUE))
                .withFields({C2F(mActualOutputDelay, value).inRange(0, 32)})
                .withSetter(Setter<decltype(*mActualOutputDelay)>::StrictValueWithNoDeps)
                .build());

        // TODO: output latency and reordering

        addParameter(
                DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                .withConstValue(new C2ComponentAttributesSetting(C2Component::ATTRIB_IS_TEMPORAL))
                .build());

        // coded and output picture size is the same for this codec
        addParameter(
                DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
                .withDefault(new C2StreamPictureSizeInfo::output(0u, 320, 240))
                .withFields({
                    C2F(mSize, width).inRange(2, 4096, 2),
                    C2F(mSize, height).inRange(2, 2160, 2),
                })
                .withSetter(SizeSetter)
                .build());

        // coded and output picture size is the same for this codec
        addParameter(
                DefineParam(mCrop, C2_PARAMKEY_CROP_RECT)
                .withDefault(new C2StreamCropRectInfo::output(0u, C2Rect(320, 240)))
                .withFields({
                    C2F(mCrop, width).inRange(2, 4096, 2),
                    C2F(mCrop, height).inRange(2, 2160, 2),
                })
                .withSetter(CropSetter)
                .build());

        addParameter(
                DefineParam(mMaxSize, C2_PARAMKEY_MAX_PICTURE_SIZE)
                .withDefault(new C2StreamMaxPictureSizeTuning::output(0u, 320, 240))
                .withFields({
                    C2F(mSize, width).inRange(2, 4096, 2),
                    C2F(mSize, height).inRange(2, 2560, 2),
                })
                .withSetter(MaxPictureSizeSetter, mSize)
                .build());

        if (mimeType == MEDIA_MIMETYPE_VIDEO_AVC) {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::input(0u,
                            C2Config::PROFILE_AVC_CONSTRAINED_BASELINE, C2Config::LEVEL_AVC_5_2))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({
                                C2Config::PROFILE_AVC_CONSTRAINED_BASELINE,
                                C2Config::PROFILE_AVC_BASELINE,
                                C2Config::PROFILE_AVC_MAIN,
                                C2Config::PROFILE_AVC_CONSTRAINED_HIGH,
                                C2Config::PROFILE_AVC_PROGRESSIVE_HIGH,
                                C2Config::PROFILE_AVC_HIGH}),
                        C2F(mProfileLevel, level).oneOf({
                                C2Config::LEVEL_AVC_1, C2Config::LEVEL_AVC_1B, C2Config::LEVEL_AVC_1_1,
                                C2Config::LEVEL_AVC_1_2, C2Config::LEVEL_AVC_1_3,
                                C2Config::LEVEL_AVC_2, C2Config::LEVEL_AVC_2_1, C2Config::LEVEL_AVC_2_2,
                                C2Config::LEVEL_AVC_3, C2Config::LEVEL_AVC_3_1, C2Config::LEVEL_AVC_3_2,
                                C2Config::LEVEL_AVC_4, C2Config::LEVEL_AVC_4_1, C2Config::LEVEL_AVC_4_2,
                                C2Config::LEVEL_AVC_5, C2Config::LEVEL_AVC_5_1, C2Config::LEVEL_AVC_5_2
                        })
                    })
                    .withSetter(ProfileLevelSetter, mSize)
                    .build());
        }else if (mimeType == MEDIA_MIMETYPE_VIDEO_HEVC) {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::input(0u,
                            C2Config::PROFILE_HEVC_MAIN, C2Config::LEVEL_HEVC_MAIN_5_1))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({
                                C2Config::PROFILE_HEVC_MAIN,
                                C2Config::PROFILE_HEVC_MAIN_STILL}),
                        C2F(mProfileLevel, level).oneOf({
                                C2Config::LEVEL_HEVC_MAIN_1,
                                C2Config::LEVEL_HEVC_MAIN_2, C2Config::LEVEL_HEVC_MAIN_2_1,
                                C2Config::LEVEL_HEVC_MAIN_3, C2Config::LEVEL_HEVC_MAIN_3_1,
                                C2Config::LEVEL_HEVC_MAIN_4, C2Config::LEVEL_HEVC_MAIN_4_1,
                                C2Config::LEVEL_HEVC_MAIN_5, C2Config::LEVEL_HEVC_MAIN_5_1,
                                C2Config::LEVEL_HEVC_MAIN_5_2, C2Config::LEVEL_HEVC_HIGH_4,
                                C2Config::LEVEL_HEVC_HIGH_4_1, C2Config::LEVEL_HEVC_HIGH_5,
                                C2Config::LEVEL_HEVC_HIGH_5_1, C2Config::LEVEL_HEVC_HIGH_5_2
                        })
                    })
                    .withSetter(ProfileLevelSetter, mSize)
                    .build());
        } else if (mimeType ==  MEDIA_MIMETYPE_VIDEO_MPEG4) {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::input(0u,
                            C2Config::PROFILE_MP4V_SIMPLE, C2Config::LEVEL_MP4V_3))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({
                                C2Config::PROFILE_MP4V_SIMPLE,
                                C2Config::PROFILE_MP4V_ADVANCED_SIMPLE}),
                        C2F(mProfileLevel, level).oneOf({
                                C2Config::LEVEL_MP4V_0,
                                C2Config::LEVEL_MP4V_0B,
                                C2Config::LEVEL_MP4V_1,
                                C2Config::LEVEL_MP4V_2,
                                C2Config::LEVEL_MP4V_3,
                                C2Config::LEVEL_MP4V_3B,
                                C2Config::LEVEL_MP4V_4,
                                C2Config::LEVEL_MP4V_4A,
                                C2Config::LEVEL_MP4V_5,
                                C2Config::LEVEL_MP4V_6})
                    })
                    .withSetter(ProfileLevelSetter, mSize)
                    .build());
        } else if (mimeType ==  MEDIA_MIMETYPE_VIDEO_MPEG2) {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::input(0u,
                            C2Config::PROFILE_MP2V_SIMPLE, C2Config::LEVEL_MP2V_HIGH))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({
                                C2Config::PROFILE_MP2V_SIMPLE,
                                C2Config::PROFILE_MP2V_MAIN}),
                        C2F(mProfileLevel, level).oneOf({
                                C2Config::LEVEL_MP2V_LOW,
                                C2Config::LEVEL_MP2V_MAIN,
                                C2Config::LEVEL_MP2V_HIGH_1440,
                                C2Config::LEVEL_MP2V_HIGH})
                    })
                    .withSetter(ProfileLevelSetter, mSize)
                    .build());

        } else if (mimeType ==  MEDIA_MIMETYPE_VIDEO_H263) {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::input(0u,
                            C2Config::PROFILE_H263_BASELINE, C2Config::LEVEL_H263_30))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({
                                C2Config::PROFILE_H263_BASELINE,
                                C2Config::PROFILE_H263_ISWV2}),
                        C2F(mProfileLevel, level).oneOf({
                                C2Config::LEVEL_H263_10,
                                C2Config::LEVEL_H263_20,
                                C2Config::LEVEL_H263_30,
                                C2Config::LEVEL_H263_40,
                                C2Config::LEVEL_H263_45,
                                C2Config::LEVEL_H263_50,
                                C2Config::LEVEL_H263_60,
                                C2Config::LEVEL_H263_70})
                    })
                    .withSetter(ProfileLevelSetter, mSize)
                    .build());
        } else if (mimeType ==  MEDIA_MIMETYPE_VIDEO_VP8) {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withConstValue(new C2StreamProfileLevelInfo::input(0u,
                            C2Config::PROFILE_UNUSED, C2Config::LEVEL_UNUSED))
                    .build());
        } else if (mimeType ==  MEDIA_MIMETYPE_VIDEO_VP9) {
            // TODO: Add C2Config::PROFILE_VP9_2HDR ??
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::input(0u,
                            C2Config::PROFILE_VP9_0, C2Config::LEVEL_VP9_5))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({
                                C2Config::PROFILE_VP9_0,
                                C2Config::PROFILE_VP9_2}),
                        C2F(mProfileLevel, level).oneOf({
                                C2Config::LEVEL_VP9_1,
                                C2Config::LEVEL_VP9_1_1,
                                C2Config::LEVEL_VP9_2,
                                C2Config::LEVEL_VP9_2_1,
                                C2Config::LEVEL_VP9_3,
                                C2Config::LEVEL_VP9_3_1,
                                C2Config::LEVEL_VP9_4,
                                C2Config::LEVEL_VP9_4_1,
                                C2Config::LEVEL_VP9_5,
                        })
                    })
                    .withSetter(ProfileLevelSetter, mSize)
                    .build());

            mHdr10PlusInfoInput = C2StreamHdr10PlusInfo::input::AllocShared(0);
            addParameter(
                    DefineParam(mHdr10PlusInfoInput, C2_PARAMKEY_INPUT_HDR10_PLUS_INFO)
                    .withDefault(mHdr10PlusInfoInput)
                    .withFields({
                        C2F(mHdr10PlusInfoInput, m.value).any(),
                    })
                    .withSetter(Hdr10PlusInfoInputSetter)
                    .build());

            mHdr10PlusInfoOutput = C2StreamHdr10PlusInfo::output::AllocShared(0);
            addParameter(
                    DefineParam(mHdr10PlusInfoOutput, C2_PARAMKEY_OUTPUT_HDR10_PLUS_INFO)
                    .withDefault(mHdr10PlusInfoOutput)
                    .withFields({
                        C2F(mHdr10PlusInfoOutput, m.value).any(),
                    })
                    .withSetter(Hdr10PlusInfoOutputSetter)
                    .build());
        }

        addParameter(
                DefineParam(mMaxInputSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                .withDefault(new C2StreamMaxBufferSizeInfo::input(0u, 320 * 240 * 3 / 4))
                .withFields({
                    C2F(mMaxInputSize, value).any(),
                })
                .calculatedAs(MaxInputSizeSetter, mMaxSize)
                .build());

        if (mimeType == MEDIA_MIMETYPE_VIDEO_AVC
                || mimeType == MEDIA_MIMETYPE_VIDEO_HEVC
                || mimeType == MEDIA_MIMETYPE_VIDEO_MPEG2) {

            C2ChromaOffsetStruct locations[1] = { C2ChromaOffsetStruct::ITU_YUV_420_0() };
            std::shared_ptr<C2StreamColorInfo::output> defaultColorInfo =
                C2StreamColorInfo::output::AllocShared(
                        1u, 0u, 8u /* bitDepth */, C2Color::YUV_420);
            memcpy(defaultColorInfo->m.locations, locations, sizeof(locations));

            defaultColorInfo =
                C2StreamColorInfo::output::AllocShared(
                        { C2ChromaOffsetStruct::ITU_YUV_420_0() },
                        0u, 8u /* bitDepth */, C2Color::YUV_420);
            helper->addStructDescriptors<C2ChromaOffsetStruct>();

            addParameter(
                    DefineParam(mColorInfo, C2_PARAMKEY_CODED_COLOR_INFO)
                    .withConstValue(defaultColorInfo)
                    .build());

            addParameter(
                    DefineParam(mDefaultColorAspects, C2_PARAMKEY_DEFAULT_COLOR_ASPECTS)
                    .withDefault(new C2StreamColorAspectsTuning::output(
                            0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                            C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                    .withFields({
                        C2F(mDefaultColorAspects, range).inRange(
                                    C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                        C2F(mDefaultColorAspects, primaries).inRange(
                                    C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                        C2F(mDefaultColorAspects, transfer).inRange(
                                    C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                        C2F(mDefaultColorAspects, matrix).inRange(
                                    C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
                    })
                    .withSetter(DefaultColorAspectsSetter)
                    .build());

            addParameter(
                    DefineParam(mCodedColorAspects, C2_PARAMKEY_VUI_COLOR_ASPECTS)
                    .withDefault(new C2StreamColorAspectsInfo::input(
                            0u, C2Color::RANGE_LIMITED, C2Color::PRIMARIES_UNSPECIFIED,
                            C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                    .withFields({
                        C2F(mCodedColorAspects, range).inRange(
                                    C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                        C2F(mCodedColorAspects, primaries).inRange(
                                    C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                        C2F(mCodedColorAspects, transfer).inRange(
                                    C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                        C2F(mCodedColorAspects, matrix).inRange(
                                    C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
                    })
                    .withSetter(CodedColorAspectsSetter)
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
                    .withSetter(ColorAspectsSetter, mDefaultColorAspects, mCodedColorAspects)
                    .build());
        }

        // TODO: support more formats?
        addParameter(
                DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
                .withDefault(new C2StreamPixelFormatInfo::output(
                            0u, HAL_PIXEL_FORMAT_YCBCR_420_888))
                .withFields({C2F(mPixelFormat, value).inRange(0, 0xffffffff)})
                .withSetter(Setter<decltype(*mPixelFormat)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mSurfaceAllocator, C2_PARAMKEY_OUTPUT_SURFACE_ALLOCATOR)
                .withConstValue(new C2PortSurfaceAllocatorTuning::output(C2PlatformAllocatorStore::BUFFERQUEUE))
                .build());

        addParameter(
                DefineParam(mVideoSubFormat, C2_PARAMKEY_VENDOR_SUB_FORMAT)
                .withDefault(new C2StreamVendorSubFormat::output(0))
                .withFields({C2F(mVideoSubFormat, value).inRange(0, 0x7fffffff)})
                .withSetter(Setter<decltype(*mVideoSubFormat)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mVendorHalPixelFormat, C2_PARAMKEY_VENDOR_HAL_PIXEL_FORMAT)
                .withDefault(new C2StreamVendorHalPixelFormat::output(0))
                .withFields({C2F(mVendorHalPixelFormat, value).inRange(0, 0xffffffff)})
                .withSetter(Setter<decltype(*mVendorHalPixelFormat)>::StrictValueWithNoDeps)
                .build());
        addParameter(
                DefineParam(mLowLatency, C2_PARAMKEY_LOW_LATENCY_MODE)
                .withDefault(new C2GlobalLowLatencyModeTuning(0))
                .withFields({C2F(mLowLatency, value).inRange(0, 1)})
                .withSetter(Setter<decltype(*mLowLatency)>::StrictValueWithNoDeps)
                .build());
    }

    static C2R SizeSetter(bool mayBlock, const C2P<C2StreamPictureSizeInfo::output> &oldMe,
                          C2P<C2StreamPictureSizeInfo::output> &me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (!me.F(me.v.width).supportsAtAll(me.v.width)) {
            me.set().width = ((me.v.width+1)*2/2);
            ALOGI("invalid width = %d", me.v.width);
        }
        if (!me.F(me.v.height).supportsAtAll(me.v.height)) {
            me.set().height = ((me.v.height+1)*2/2);
            ALOGI("invalid height = %d", me.v.height);
        }
        return res;
    }

    static C2R CropSetter(bool mayBlock, const C2P<C2StreamCropRectInfo::output> &oldMe,
                          C2P<C2StreamCropRectInfo::output> &me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (!me.F(me.v.width).supportsAtAll(me.v.width)) {
            me.set().width = ((me.v.width+1)*2/2);
            ALOGI("invalid crop width = %d", me.v.width);
        }
        if (!me.F(me.v.height).supportsAtAll(me.v.height)) {
            me.set().height = ((me.v.height+1)*2/2);
            ALOGI("invalid crop height = %d", me.v.height);
        }
        return res;
    }

    static C2R MaxPictureSizeSetter(bool mayBlock, C2P<C2StreamMaxPictureSizeTuning::output> &me,
                                    const C2P<C2StreamPictureSizeInfo::output> &size) {
        (void)mayBlock;
        // TODO: get max width/height from the size's field helpers vs. hardcoding
        me.set().width = c2_min(c2_max(me.v.width, size.v.width), 4080u);
        me.set().height = c2_min(c2_max(me.v.height, size.v.height), 4080u);
        return C2R::Ok();
    }

    static C2R MaxInputSizeSetter(bool mayBlock, C2P<C2StreamMaxBufferSizeInfo::input> &me,
                                  const C2P<C2StreamMaxPictureSizeTuning::output> &maxSize) {
        (void)mayBlock;
        // assume compression ratio of 2
        me.set().value = (((maxSize.v.width + 15) / 16) * ((maxSize.v.height + 15) / 16) * 192);
        // HACK: allow 20% overhead
        me.set().value += me.set().value / 5;
        return C2R::Ok();
    }

    static C2R ProfileLevelSetter(bool mayBlock, C2P<C2StreamProfileLevelInfo::input> &me,
                                  const C2P<C2StreamPictureSizeInfo::output> &size) {
        (void)mayBlock;
        (void)size;
        (void)me;  // TODO: validate
        return C2R::Ok();
    }

    static C2R DefaultColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsTuning::output> &me) {
        (void)mayBlock;
        if (me.v.range > C2Color::RANGE_OTHER) {
                me.set().range = C2Color::RANGE_OTHER;
        }
        if (me.v.primaries > C2Color::PRIMARIES_OTHER) {
                me.set().primaries = C2Color::PRIMARIES_OTHER;
        }
        if (me.v.transfer > C2Color::TRANSFER_OTHER) {
                me.set().transfer = C2Color::TRANSFER_OTHER;
        }
        if (me.v.matrix > C2Color::MATRIX_OTHER) {
                me.set().matrix = C2Color::MATRIX_OTHER;
        }
        return C2R::Ok();
    }

    static C2R CodedColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::input> &me) {
        (void)mayBlock;
        if (me.v.range > C2Color::RANGE_OTHER) {
                me.set().range = C2Color::RANGE_OTHER;
        }
        if (me.v.primaries > C2Color::PRIMARIES_OTHER) {
                me.set().primaries = C2Color::PRIMARIES_OTHER;
        }
        if (me.v.transfer > C2Color::TRANSFER_OTHER) {
                me.set().transfer = C2Color::TRANSFER_OTHER;
        }
        if (me.v.matrix > C2Color::MATRIX_OTHER) {
                me.set().matrix = C2Color::MATRIX_OTHER;
        }
        return C2R::Ok();
    }

    static C2R ColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::output> &me,
                                  const C2P<C2StreamColorAspectsTuning::output> &def,
                                  const C2P<C2StreamColorAspectsInfo::input> &coded) {
        (void)mayBlock;
        // take default values for all unspecified fields, and coded values for specified ones
        me.set().range = coded.v.range == RANGE_UNSPECIFIED ? def.v.range : coded.v.range;
        me.set().primaries = coded.v.primaries == PRIMARIES_UNSPECIFIED
                ? def.v.primaries : coded.v.primaries;
        me.set().transfer = coded.v.transfer == TRANSFER_UNSPECIFIED
                ? def.v.transfer : coded.v.transfer;
        me.set().matrix = coded.v.matrix == MATRIX_UNSPECIFIED ? def.v.matrix : coded.v.matrix;
        return C2R::Ok();
    }

    std::shared_ptr<C2StreamColorAspectsInfo::output> getColorAspects_l() {
        return mColorAspects;
    }

    std::shared_ptr<C2StreamColorAspectsTuning::output> getDefaultColorAspects_l() {
        return mDefaultColorAspects;
    }


    uint32_t getVenderHalFormat() const { return mVendorHalPixelFormat->value; }

    static C2R Hdr10PlusInfoInputSetter(bool mayBlock, C2P<C2StreamHdr10PlusInfo::input> &me) {
        (void)mayBlock;
        (void)me;  // TODO: validate
        return C2R::Ok();
    }

    static C2R Hdr10PlusInfoOutputSetter(bool mayBlock, C2P<C2StreamHdr10PlusInfo::output> &me) {
        (void)mayBlock;
        (void)me;  // TODO: validate
        return C2R::Ok();
    }

private:
    C2String mComponentName;
    std::shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;
    std::shared_ptr<C2StreamPictureSizeInfo::output> mSize;
    std::shared_ptr<C2StreamCropRectInfo::output> mCrop;
    std::shared_ptr<C2StreamMaxPictureSizeTuning::output> mMaxSize;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mMaxInputSize;
    std::shared_ptr<C2StreamColorInfo::output> mColorInfo;
    std::shared_ptr<C2StreamColorAspectsInfo::input> mCodedColorAspects;
    std::shared_ptr<C2StreamColorAspectsTuning::output> mDefaultColorAspects;
    std::shared_ptr<C2StreamColorAspectsInfo::output> mColorAspects;
    std::shared_ptr<C2StreamPixelFormatInfo::output> mPixelFormat;
    std::shared_ptr<C2PortSurfaceAllocatorTuning::output> mSurfaceAllocator;
    std::shared_ptr<C2StreamHdr10PlusInfo::input> mHdr10PlusInfoInput;
    std::shared_ptr<C2StreamHdr10PlusInfo::output> mHdr10PlusInfoOutput;
    std::shared_ptr<C2StreamVendorSubFormat::output> mVideoSubFormat;
    std::shared_ptr<C2StreamVendorHalPixelFormat::output> mVendorHalPixelFormat;
    std::shared_ptr<C2GlobalLowLatencyModeTuning> mLowLatency;
};


IMXC2VideoDecoder::IMXC2VideoDecoder(const char* name, c2_node_id_t id, const std::shared_ptr<IntfImpl> &intfImpl)
    : IMXC2ComponentBase(std::make_shared<IMXInterface<IntfImpl>>(name, id, intfImpl)),
      mIntf(intfImpl),
      mWidth(320),
      mHeight(240),
      mCropWidth(320),
      mCropHeight(240),
      nOutBufferNum(8),
      mName(name),
      bPendingFmtChanged(false),
      bGetGraphicBlockPool(false),
      bSignalOutputEos(false),
      bSignalledError(false),
      bFlushDone(false),
      bPPEnabled(false),
      bSupportColorAspects(false){
}

IMXC2VideoDecoder::~IMXC2VideoDecoder() {
}

// From IMXC2ComponentBase
c2_status_t IMXC2VideoDecoder::onInit() {
    status_t err;

    const char* mime = Name2MimeType((const char*)mName.c_str());
    if (mime == nullptr) {
        ALOGE("Unsupported component name: %s", mName.c_str());
        return C2_BAD_VALUE;
    }
    ALOGV("onInit mime=%s",mime);

    if (!strcmp(mime, MEDIA_MIMETYPE_VIDEO_AVC)
            || !strcmp(mime, MEDIA_MIMETYPE_VIDEO_HEVC)
            || !strcmp(mime, MEDIA_MIMETYPE_VIDEO_MPEG2))
        bSupportColorAspects = true;

    mDecoder = CreateVideoDecoderInstance(mime);
    if (!mDecoder) {
        ALOGE("CreateVideoDecoderInstance for mime(%s) failed \n", mime);
        return C2_CORRUPTED;
    }

    err = mDecoder->init((VideoDecoderBase::Client*)this);
    if (err) {
        goto RELEASE_DECODER;
    }

    err = initInternalParam();
    if (err)
        goto RELEASE_DECODER;

    err = mDecoder->start();
    if (err)
        goto RELEASE_DECODER;

    return C2_OK;

RELEASE_DECODER:
    // release decoder if init failed, in case of upper layer don't call release
    releaseDecoder();
    return C2_NO_MEMORY;
}

c2_status_t IMXC2VideoDecoder::onStop() {
    status_t err;


    ALOGV("onStop");
    err = mDecoder->stop();
    if (err != OK)
        ALOGE("decoder stop return err %d", err);

    if (mPostProcess) {
        ALOGV("onStop mPostProcess->stop");
        err = mPostProcess->stop();
        if (err != OK)
            ALOGE("post process stop return err %d", err);
    }

    bSignalledError = false;
    bSignalOutputEos = false;

    err = initInternalParam();
    return C2ERR(err);
}

c2_status_t IMXC2VideoDecoder::onFlush_sm() {
    status_t err = OK;
    ALOGV("onFlush_sm");


    err = mDecoder->flush();
    CHECK_AND_RETURN_C2_ERR(err);

    if (bPPEnabled && mPostProcess) {
        err = mPostProcess->flush();
        CHECK_AND_RETURN_C2_ERR(err);
    }


    bPendingFmtChanged = false;
    bSignalledError = false;
    bSignalOutputEos = false;
    bRecieveOutputEos = false;
    bFlushDone = true;

    nCurFrameIndex = 0;
    nUsedFrameIndex = 0;
    ALOGV("onFlush_sm end");
    return C2ERR(err);
}

void IMXC2VideoDecoder::onReset() {
    ALOGV("onReset");
    (void) releaseDecoder();
}

void IMXC2VideoDecoder::onRelease() {
    ALOGV("onRelease");
    (void) releaseDecoder();
}

static void fillEmptyWork(const std::unique_ptr<C2Work> &work) {
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

void IMXC2VideoDecoder::processWork(const std::unique_ptr<C2Work> &work) {
    int32_t fd, inputId;
    uint8_t* inputBuffer;
    uint64_t timestamp;
    status_t ret = OK;

    if (!bGetGraphicBlockPool) {
        status_t err = mDecoder->setGraphicBlockPool(mOutputBlockPool);
        if (err != OK)
            return;
        bGetGraphicBlockPool = true;
    }

    work->result = C2_OK;
    work->workletsProcessed = 0u;
    work->worklets.front()->output.configUpdate.clear();
    work->worklets.front()->output.flags = work->input.flags;

    if (bSignalledError | bSignalOutputEos) {
        work->result = C2_BAD_VALUE;
        return fillEmptyWork(work);
    }

    C2ReadView view = mDummyReadView;
    uint32_t size = 0;
    uint32_t flags = work->input.flags;
    bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);
    if(eos)
        flags &= ~(work->input.flags & C2FrameData::FLAG_END_OF_STREAM);

    if (!work->input.buffers.empty()) {
        view = work->input.buffers[0]->data().linearBlocks().front().map().get();
        size = view.capacity();
        if (size && view.error()) {
            ALOGE("read view map failed %d", view.error());
            work->result = view.error();
            return;
        }
    }

    if (eos && size == 0) {
        ALOGI("input eos, size=0,ts=%d",(int)work->input.ordinal.timestamp.peeku());
        drain_nb(DRAIN_COMPONENT_WITH_EOS);
        bSignalOutputEos = true;
        return;
    } else if (work->input.buffers.empty()) {
        fillEmptyWork(work);
        return;
    }

    const C2ConstLinearBlock block = work->input.buffers[0]->data().linearBlocks().front();
    fd = block.handle()->data[0];
    inputBuffer = const_cast<uint8_t *>(view.data());
    timestamp = work->input.ordinal.timestamp.peeku();
    inputId = static_cast<int32_t>(work->input.ordinal.frameIndex.peeku() & 0x3FFFFFFF);

    ALOGV("in buffer fd %d addr %p size %d timestamp %d frameindex %d, flags %x",
          fd, inputBuffer, (int)view.capacity(), (int)timestamp,
          (int)work->input.ordinal.frameIndex.peeku(), flags);

    ret = mDecoder->queueInput(inputBuffer, size, timestamp, flags, fd, inputId);

    if (nUsedFrameIndex == nCurFrameIndex) {
        work->input.buffers.front().reset();
        ALOGV("input id %d is used \n", inputId);
    }

    // codec data won't have a picture out, mark c2work as processed directly
    if ((flags & C2FrameData::FLAG_CODEC_CONFIG) || ret != OK) {
        work->workletsProcessed = 1u;
        work->result = C2_OK;
    }

    if (eos && size > 0) {
        ALOGI("input eos, size=%d,ts=%d",size,(int)work->input.ordinal.timestamp.peeku());

        drain_nb(DRAIN_COMPONENT_WITH_EOS);
        bSignalOutputEos = true;
        return;
    }
}

c2_status_t IMXC2VideoDecoder::drainInternal(uint32_t drainMode) {
    // trigger decoding to drain internel input buffers
    if (drainMode == NO_DRAIN) {
        ALOGW("drain with NO_DRAIN: no-op");
        return C2_OK;
    }
    if (drainMode == DRAIN_CHAIN) {
        ALOGW("DRAIN_CHAIN not supported");
        return C2_OMITTED;

    }
    // DRAIN_COMPONENT_WITH_EOS
    mDecoder->queueInput(nullptr, 0, 0, C2FrameData::FLAG_END_OF_STREAM, -1, -1);
    return C2_OK;
}

status_t IMXC2VideoDecoder::initInternalParam() {
    c2_status_t err = C2_OK;
    ALOGV("initInternalParam BEGIN");

    C2StreamPictureSizeInfo::output size(0u, mWidth, mHeight);
    err = intf()->query_vb({&size,}, {}, C2_DONT_BLOCK, nullptr);
    if (err == C2_OK) {
        mWidth = size.width;
        mHeight = size.height;

        VideoFormat vFormat;
        mDecoder->getConfig(DEC_CONFIG_INPUT_FORMAT, &vFormat);
        vFormat.width = mWidth;
        vFormat.height = mHeight;
        mDecoder->setConfig(DEC_CONFIG_INPUT_FORMAT, &vFormat);

        memset(&vFormat, 0, sizeof(VideoFormat));
        mDecoder->getConfig(DEC_CONFIG_OUTPUT_FORMAT, &vFormat);
        vFormat.width = mWidth;
        vFormat.height = mHeight;
        mDecoder->setConfig(DEC_CONFIG_OUTPUT_FORMAT, &vFormat);
    }

    C2StreamVendorSubFormat::output subFormat(0);
    err = intf()->query_vb({&subFormat,}, {}, C2_DONT_BLOCK, nullptr);
    if (err == C2_OK && subFormat.value != 0) {
        int32_t vc1SubFormat = subFormat.value;
        ALOGV("SET DEC_CONFIG_VC1_SUB_FORMAT vc1SubFormat=%x",vc1SubFormat);
        mDecoder->setConfig(DEC_CONFIG_VC1_SUB_FORMAT, &vc1SubFormat);
    }

    int outputDelayValue;
    if (OK == mDecoder->getConfig(DEC_CONFIG_OUTPUT_DELAY, &outputDelayValue)) {
        C2PortActualDelayTuning::output outputDelay(outputDelayValue);
        std::vector<std::unique_ptr<C2SettingResult>> failures;
        (void)mIntf->config({&outputDelay}, C2_MAY_BLOCK, &failures);
    }

    C2StreamVendorHalPixelFormat::output output_fmt(0);
    err = intf()->query_vb({&output_fmt,}, {}, C2_DONT_BLOCK, nullptr);
    if (err == C2_OK && output_fmt.value != 0) {
        uint32_t fmt = output_fmt.value;
        ALOGV("SET DEC_CONFIG_HAL_PIXEL_FORMAT fmt=%x",fmt);
        (void)mDecoder->setConfig(DEC_CONFIG_HAL_PIXEL_FORMAT, &fmt);
    }

    C2GlobalLowLatencyModeTuning low_latency(0);
    err = intf()->query_vb({&low_latency,}, {}, C2_DONT_BLOCK, nullptr);
    if (err == C2_OK) {
        int32_t enable = (int32_t)low_latency.value;
        ALOGV("SET C2StreamVendorLowLatency enable=%d",enable);
        (void)mDecoder->setConfig(DEC_CONFIG_LOW_LATENCY, &enable);
    }

    return OK;
}

void IMXC2VideoDecoder::releaseDecoder() {

    if (mDecoder) {
        mDecoder->destroy();
        mDecoder.clear();
    }

    if (mPostProcess) {
        ALOGV("releaseDecoder mPostProcess->destroy");
        mPostProcess->destroy();
        mPostProcess.clear();
    }

}

void IMXC2VideoDecoder::handleOutputPicture(GraphicBlockInfo* info, uint64_t timestamp, uint32_t flag) {

    C2ConstGraphicBlock constBlock = info->mGraphicBlock->share(C2Rect(mCropWidth, mCropHeight), C2Fence());
    std::shared_ptr<C2Buffer> buffer = C2Buffer::CreateGraphicBuffer(std::move(constBlock));
    if(bPPEnabled){
        bPendingFmtChanged = false;
        if(flag & FLAG_RES_CHANGE)
            bPendingFmtChanged = true;
    }

    bool configUpdate = (bPendingFmtChanged || bFlushDone);
    uint32_t outputDelayValue = 0;
    C2PortActualDelayTuning::output outputDelay(0);
    C2StreamPictureSizeInfo::output size(0u, mWidth, mHeight);
    C2StreamPixelFormatInfo::output fmt(0u, HAL_PIXEL_FORMAT_YCBCR_420_888);
    C2StreamCropRectInfo::output crop(0u, C2Rect(mCropWidth, mCropHeight));

    if (bSupportColorAspects) {
        IntfImpl::Lock lock = mIntf->lock();
        buffer->setInfo(mIntf->getColorAspects_l());
    }

    if (bPendingFmtChanged) {
        DecStaticHDRInfo hdrInfo;
        if (OK == mDecoder->getConfig(DEC_CONFIG_HDR10_STATIC_INFO, &hdrInfo)) {
            std::shared_ptr<C2StreamHdrStaticInfo::output> hdrStaticInfo =
                std::make_shared<C2StreamHdrStaticInfo::output>();
            hdrStaticInfo->mastering = {
                .red   = { .x = (float)(hdrInfo.mR[0]*0.00002),  .y = (float)(hdrInfo.mR[1]*0.00002) },
                .green = { .x = (float)(hdrInfo.mG[0]*0.00002),  .y = (float)(hdrInfo.mG[1]*0.00002) },
                .blue  = { .x = (float)(hdrInfo.mB[0]*0.00002),  .y = (float)(hdrInfo.mB[1]*0.00002) },
                .white = { .x = (float)(hdrInfo.mW[0]*0.00002),  .y = (float)(hdrInfo.mW[1]*0.00002) },
                .maxLuminance = (float)(hdrInfo.mMaxDisplayLuminance*1.0),
                .minLuminance = (float)(hdrInfo.mMinDisplayLuminance*0.0001),
            };
            hdrStaticInfo->maxCll = (float)(hdrInfo.mMaxContentLightLevel*1.0);
            hdrStaticInfo->maxFall = (float)(hdrInfo.mMaxFrameAverageLightLevel*1.0);
            buffer->setInfo(hdrStaticInfo);

            ALOGI("HdrStaticInfo: red(%0.5f, %0.5f) green(%0.5f, %0.5f) blue(%0.5f, %0.5f) white(%0.5f, %0.5f)",
                hdrStaticInfo->mastering.red.x, hdrStaticInfo->mastering.red.y,
                hdrStaticInfo->mastering.green.x, hdrStaticInfo->mastering.green.y,
                hdrStaticInfo->mastering.blue.x, hdrStaticInfo->mastering.blue.y,
                hdrStaticInfo->mastering.white.x, hdrStaticInfo->mastering.white.y);
            ALOGI("HdrStaticInfo: maxLuminance(%0.1f) minLuminance(%0.4f) maxCll(%0.1f) maxFall(%0.1f)",
                hdrStaticInfo->mastering.maxLuminance, hdrStaticInfo->mastering.minLuminance,
                hdrStaticInfo->maxCll, hdrStaticInfo->maxFall);
        }
    }

    if (configUpdate) {
        c2_status_t err = intf()->query_vb(
            {
                &outputDelay,
            },
            {},
            C2_DONT_BLOCK,
            nullptr);
        if (err == C2_OK) {
            outputDelayValue = outputDelay.value;
        }

        if (bPendingFmtChanged) {
            //TODO: remove 4 buffers after resolve pause timeout issue
            outputDelayValue = nOutBufferNum + 4;

            if (outputDelay.value < outputDelayValue) {
                outputDelay.value = outputDelayValue;
                std::vector<std::unique_ptr<C2SettingResult>> failures;
                (void)mIntf->config({&outputDelay}, C2_MAY_BLOCK, &failures);
            }
        }

        ALOGV("configUpdate outputDelay.value=%d", outputDelay.value);
        if(bPPEnabled){
            fmt.value = mIntf->getVenderHalFormat();
            ALOGV("handleOutputPicture bPPEnabled");
        }
    }

    auto fillWork = [buffer, timestamp, configUpdate, crop, size, outputDelay, fmt]
                    (const std::unique_ptr<C2Work> &work) mutable {

        uint32_t flags = 0;
        if ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) &&
                (c2_cntr64_t(timestamp) == work->input.ordinal.timestamp)) {
            flags |= C2FrameData::FLAG_END_OF_STREAM;
            ALOGV("signalling eos");
        }
        if (configUpdate) {
            work->worklets.front()->output.configUpdate.push_back(C2Param::Copy(crop));
            work->worklets.front()->output.configUpdate.push_back(C2Param::Copy(size));
            work->worklets.front()->output.configUpdate.push_back(C2Param::Copy(outputDelay));
            work->worklets.front()->output.configUpdate.push_back(C2Param::Copy(fmt));
        }

        work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.buffers.push_back(buffer);
        work->worklets.front()->output.ordinal = work->input.ordinal;

        // if original timestamp is -1, use ts manager adjusted timestamp instead,
        // or frame with ts=-1 will be dropped by display, video become influent.
        if (-1 == work->input.ordinal.timestamp.peeku()) {
            work->worklets.front()->output.ordinal.timestamp = timestamp;
        }

        work->workletsProcessed = 1u;
    };

    c2_status_t err = finish(timestamp, fillWork);

    info->mState = GraphicBlockInfo::State::OWNED_BY_CLIENT;
    bPendingFmtChanged = false;
    bFlushDone = false;

    if (C2_NOT_FOUND == err && !bPPEnabled) {
        // no need to return buffer to post processor because its reference is clear.
        mDecoder->returnOutputBufferToDecoder(info->mBlockId);
    } else {
        info->mGraphicBlock.reset();
    }
}


// callbacks for VideoDecoderBase

// update intf configure
void IMXC2VideoDecoder::notifyVideoInfo(VideoFormat *pFormat) {
    status_t err = OK;
    uint32_t ppOutFmt = 0;
    bPPEnabled = mDecoder->checkIfPostProcessNeeded();

    if (bPPEnabled) {
        ALOGI("post process is enabled");

        PROCESSBASE_FORMAT inFmt, outFmt;
        inFmt.width = pFormat->width;
        inFmt.height = pFormat->height;
        inFmt.stride = pFormat->stride;
        inFmt.interlaced = pFormat->interlaced;
        inFmt.flag = 0;
        inFmt.format = (uint32_t)pFormat->pixelFormat;
        ALOGV("vendor hal format=%d",mIntf->getVenderHalFormat());
        //nuplayer will set vendor hal format. if not, enable nv12 format
        if(0 == mIntf->getVenderHalFormat()){
            inFmt.flag = PROCESSBASE_FORMAT_FLAG_NV12;
            ALOGI("enable NV12 format");
        }

        // receive the first format change event, post process is created and configured.
        // later post process just need to call videoFormatChanged to handle this event.
        if (!mPostProcess) {
            mPostProcess = CreatePostProcessInstance();
            if (!mPostProcess) {
                bSignalledError = true;
                return;
            }

            ALOGV("config process input format: %d x %d, stride %d, fmt=%d", pFormat->width, pFormat->height, pFormat->width, pFormat->pixelFormat);

            err = mPostProcess->setConfig(PROCESS_CONFIG_INPUT_FORMAT, &inFmt);
            if (err) {
                bSignalledError = true;
                return;
            }

            err = mPostProcess->init((ProcessBase::Client*)this, mOutputBlockPool);
            if (err) {
                bSignalledError = true;
                return;
            }

            err = mPostProcess->getConfig(PROCESS_CONFIG_OUTPUT_FORMAT, &outFmt);
            if (err) {
                bSignalledError = true;
                return;
            }
            ppOutFmt = outFmt.format;
        } else {
            ALOGV("notifyVideoInfo call mPostProcess->videoFormatChanged");
            ppOutFmt = mIntf->getVenderHalFormat();
            // need reconfigure post processor to handle resolution change.
            mPostProcess->videoFormatChanged(&inFmt);
            mPostProcess->start();
        }
        nOutBufferNum = DEFAULT_OUTPUT_BUFFER_CNT_IN_POST_PROCESS;
    }
    else
        nOutBufferNum = pFormat->bufferNum;

    mWidth = pFormat->width;
    mHeight = pFormat->height;
    mCropWidth = pFormat->rect.right - pFormat->rect.left;
    mCropHeight = pFormat->rect.bottom- pFormat->rect.top;

    ALOGV("notifyVideoInfo mWidth=%d,mHeight=%d, mCropWidth=%d,mCropHeight=%d,nOutBufferNum=%d",mWidth,mHeight,mCropWidth, mCropHeight,nOutBufferNum);

    std::vector<std::unique_ptr<C2SettingResult>> failures;

    //update C2StreamVendorHalPixelFormat when enable post process for malone decoder
    if(bPPEnabled){
        C2StreamVendorHalPixelFormat::output fmt(0u, HAL_PIXEL_FORMAT_YCBCR_422_I);//FORMAT_YUYV 0x14
        if(ppOutFmt != 0)
            fmt.value = ppOutFmt;
        (void)mIntf->config({&fmt}, C2_MAY_BLOCK, &failures);
        ALOGV("config C2StreamVendorHalPixelFormat fmt=0x%x",fmt.value);
    }

    DecIsoColorAspects colorAspects;
    if (OK == mDecoder->getConfig(DEC_CONFIG_COLOR_ASPECTS, &colorAspects)) {
        ColorAspects aspects;
        C2StreamColorAspectsInfo::input codedAspects = { 0u };
        uint32_t primaries = colorAspects.colourPrimaries;
        uint32_t transfer = colorAspects.transferCharacteristics;
        uint32_t matrix = colorAspects.matrixCoeffs;
        bool range = colorAspects.fullRange;
        memset(&aspects, 0, sizeof(aspects));
        ColorUtils::convertIsoColorAspectsToCodecAspects(primaries, transfer, matrix, range, aspects);
        if (!C2Mapper::map(aspects.mPrimaries, &codedAspects.primaries)) {
            codedAspects.primaries = C2Color::PRIMARIES_UNSPECIFIED;
        }
        if (!C2Mapper::map(aspects.mTransfer, &codedAspects.transfer)) {
            codedAspects.transfer = C2Color::TRANSFER_UNSPECIFIED;
        }
        if (!C2Mapper::map(aspects.mMatrixCoeffs, &codedAspects.matrix)) {
            codedAspects.matrix = C2Color::MATRIX_UNSPECIFIED;
        }
        if (!C2Mapper::map(aspects.mRange, &codedAspects.range)) {
            codedAspects.range = C2Color::RANGE_UNSPECIFIED;
        }
        ALOGD("C2 color aspect p %d r %d m %d t %d",
            codedAspects.primaries, codedAspects.range,
            codedAspects.matrix, codedAspects.transfer);
        std::vector<std::unique_ptr<C2SettingResult>> failures;
        (void)mIntf->config({&codedAspects}, C2_MAY_BLOCK, &failures);
    }

    bPendingFmtChanged = true;
}

void IMXC2VideoDecoder::clearPictureBuffer() {
}

void IMXC2VideoDecoder::notifyPictureReady(int32_t pictureId, uint64_t timestamp) {
    ALOGV("notifyPictureReady picture id=%d, ts=%lld", (int)pictureId, (long long)timestamp);

    GraphicBlockInfo* info = mDecoder->getGraphicBlockById(pictureId);
    if (!info) {
        /* notify error */
        ALOGE("%s line %d: wrong pictureId %d", __FUNCTION__, __LINE__, pictureId);
        return;
    }

    if (info->mState != GraphicBlockInfo::State::OWNED_BY_VPU) {
        ALOGE("%s line %d: error graphic block state, expect OWNED_BY_VPU but get %d", __FUNCTION__, __LINE__, info->mState);
        return;
    }

    if (bPPEnabled) {
        uint32_t flag = 0;
        if(bPendingFmtChanged){
            flag = FLAG_RES_CHANGE;
            bPendingFmtChanged = false;
        }
        ALOGV("queueInput input pictureId=%d,id=%d",(int)pictureId,(int)info->mBlockId);
        mPostProcess->queueInput((void*)info->mVirtAddr, (void*)info->mPhysAddr, info->mCapacity,
                                    timestamp, flag, info->mDMABufFd, info->mBlockId);
        return;
    }

    handleOutputPicture(info, timestamp, 0);

}

void IMXC2VideoDecoder::notifyInputBufferUsed(int32_t input_id) {
    nUsedFrameIndex = static_cast<uint64_t>(input_id);

    C2Work* work = getPendingWorkByFrameIndex(nUsedFrameIndex);
    if (!work) {
        return;
    }

    // When the work is done, the input buffer shall be reset by component.
    work->input.buffers.front().reset();
    ALOGV("input id %d is used \n", (int)input_id);
}

void IMXC2VideoDecoder::notifySkipInputBuffer(int32_t input_id) {
    skipOnePendingWork((uint64_t)input_id);
}

void IMXC2VideoDecoder::notifyFlushDone() {
    if (bPPEnabled) {
        // wait post process notify flush done
        return;
    }
    bFlushDone = true;
}

void IMXC2VideoDecoder::notifyResetDone() {
}

void IMXC2VideoDecoder::notifyError(status_t err) {
    bSignalledError = true;
    finishWithException(false/*eos*/, false/*force*/);
    ALOGE("video decoder notify with error %d", err);
}

void IMXC2VideoDecoder::notifyEos() {
    if (bPPEnabled) {
        // queue an empty input with eos flag
        mPostProcess->queueInput(0, 0, 0, 0, C2FrameData::FLAG_END_OF_STREAM, -1, -1);
        return;
    } else {
        finishWithException(true/*eos*/, !bRecieveOutputEos);
    }

    // fill empty work and sigal eos
    bRecieveOutputEos = true;
}

status_t IMXC2VideoDecoder::fetchProcessBuffer(int *bufferId, unsigned long *phys) {
    status_t ret = OK;

    GraphicBlockInfo *gbInfo = mDecoder->getFreeGraphicBlock();
    if (!gbInfo) {
        if (OK == mDecoder->fetchOutputBuffer()) {
            gbInfo = mDecoder->getFreeGraphicBlock();
        } else
            ret = BAD_VALUE;
    }

    if (ret == OK) {
        *bufferId = gbInfo->mBlockId;
        *phys = gbInfo->mPhysAddr;
    }

    return ret;
}

status_t IMXC2VideoDecoder::notifyProcessInputUsed(int inputId) {
    ALOGV("%s inputId %d", __FUNCTION__, inputId);
    if(mDecoder == NULL)
        return OK;

    mDecoder->returnOutputBufferToDecoder(inputId);
    return OK;
}

status_t IMXC2VideoDecoder::notifyProcessDone(int outputId, uint64_t timestamp, uint32_t flag) {
    ALOGV("%s, outputId %d, timestamp %lld\n", __FUNCTION__, outputId, (long long)timestamp);

    ProcessBlockInfo* pInfo = mPostProcess->getProcessBlockById(outputId);
    if (!pInfo) {
        ALOGE("notifyProcessDone get invalid outputId: %d", outputId);
        return BAD_VALUE;
    }
    if(pInfo->mGraphicBlock == NULL){
        ALOGE("=== notifyProcessDone could not get mGraphicBlock outputId: %d", outputId);
        return BAD_VALUE;
    }

    GraphicBlockInfo graphicInfo;

    memset(&graphicInfo, 0, sizeof(GraphicBlockInfo));
    graphicInfo.mBlockId = pInfo->mBlockId;
    graphicInfo.mGraphicBlock = std::move(pInfo->mGraphicBlock);
    //(void)flag;
    handleOutputPicture(&graphicInfo, timestamp,flag);

    return OK;
}

status_t IMXC2VideoDecoder::notifyProcessOutputClear() {
    return OK;
}

status_t IMXC2VideoDecoder::notifyProcessFlushDone() {
    bFlushDone = true;
    return OK;
}

status_t IMXC2VideoDecoder::notifyProcessResetDone() {
    return OK;
}

void IMXC2VideoDecoder::notifyProcessError() {
    bSignalledError = true;
    finishWithException(false/*eos*/, false/*force*/);
    ALOGE("post process notify with error");
}

void IMXC2VideoDecoder::notifyProcessEos() {
    ALOGV("get notifyProcessEos");
    // fill empty work and sigal eos
    finishWithException(true/*eos*/, !bRecieveOutputEos);
    bRecieveOutputEos = true;
}

class IMXC2VideoDecoderFactory : public C2ComponentFactory {
public:
    IMXC2VideoDecoderFactory(C2String name)
        : mHelper(std::static_pointer_cast<C2ReflectorHelper>(GetImxC2Store()->getParamReflector())),
          mComponentName(name) {
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {
        *component = std::shared_ptr<C2Component>(
                new IMXC2VideoDecoder(mComponentName.c_str(),
                                 id,
                                 std::make_shared<IMXC2VideoDecoder::IntfImpl>(mHelper, mComponentName.c_str())),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id,
            std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new IMXInterface<IMXC2VideoDecoder::IntfImpl>(
                        mComponentName, id, std::make_shared<IMXC2VideoDecoder::IntfImpl>(mHelper, mComponentName)),
                deleter);
        return C2_OK;
    }

    //typedef ::C2ComponentFactory* (*IMXCreateCodec2FactoryFunc)(C2String name);

    virtual ~IMXC2VideoDecoderFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
    C2String mComponentName;
};

extern "C" ::C2ComponentFactory* IMXCreateCodec2Factory(C2String name) {
    ALOGV("in %s", __func__);
    return new ::android::IMXC2VideoDecoderFactory(name);
}

extern "C" void IMXDestroyCodec2Factory(::C2ComponentFactory* factory) {
    ALOGV("in %s", __func__);
    delete factory;
}


} // namespcae android

/* end of file */
