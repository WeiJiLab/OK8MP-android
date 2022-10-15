/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef IMX_C2_VIDEO_ENCODER_H_
#define IMX_C2_VIDEO_ENCODER_H_

#include <media/stagefright/foundation/MediaDefs.h>

#include <C2Debug.h>
#include <C2PlatformSupport.h>
#include <IMXC2Interface.h>

#include "IMXC2ComponentBase.h"
#include "ProcessBase.h"
#include "VideoEncoderBase.h"
#include "C2_imx.h"

namespace android {

#define DEFAULT_B_FRAMES 0
#define MAX_B_FRAMES     1

struct FrameConfig {
    uint64_t mIndex;
    uint32_t mBitrate;
    uint32_t mRequestSync;
    FrameConfig() {}
    FrameConfig(FrameConfig *pCfg);
    FrameConfig(uint64_t index, uint32_t bitrate, uint32_t requestSync);
};

class IMXC2VideoEncoder : public IMXC2ComponentBase,
						           public ProcessBase::Client,
						           public VideoEncoderBase::Client {
public:
	class IntfImpl;
    IMXC2VideoEncoder(C2String name, c2_node_id_t id, const std::shared_ptr<IntfImpl> &intfImpl);
    virtual ~IMXC2VideoEncoder();

	// from VideoEncoderBase
    void clearOutputFrameBuffer() override;
    void notifyOutputFrameReady(int32_t outFrameId, uint32_t frameSize,
                                           uint64_t timestamp, int keyFrame, uint32_t offset) override;
    void notifyInputBufferUsed(int32_t input_id) override;
    void notifyFlushDone() override;
    void notifyResetDone() override;
    void notifyEos() override;
    void notifyError(status_t err) override;

	// from ProcessBase
    status_t fetchProcessBuffer(int *bufferId, unsigned long *phys) override;
    status_t notifyProcessInputUsed(int inputId) override;
    status_t notifyProcessDone(int outputId, uint64_t timestamp, uint32_t flag) override;
    status_t notifyProcessOutputClear() override;
    status_t notifyProcessFlushDone() override;
    status_t notifyProcessResetDone() override;
    void notifyProcessError() override;
    void notifyProcessEos() override;

protected:

	// From IMXC2Component
    c2_status_t onInit() override;
    c2_status_t onStop() override;
    void onReset() override;
    void onRelease() override;
    c2_status_t onFlush_sm() override;
    void processWork(const std::unique_ptr<C2Work> &work) override;
    c2_status_t drainInternal(uint32_t drainMode) override;

private:
	sp<ProcessBase> mPreProcess;
	sp<VideoEncoderBase> mEncoder;
	std::shared_ptr<IntfImpl> mIntf;

	C2String mName; //component name or role name ?
    bool bGetBlockPool;
    bool bStarted;
    bool bCodecDataReceived;
    bool bPPEnabled;

    uint32_t nOutBufferNum;

    uint64_t nCurInTimestamp;
    uint64_t nCurOutTimestamp;
    int nCurOutFrameIsKey;
    int32_t nCurOutFrameId;
    uint32_t nCurOutFrameSize;

    typedef std::queue<std::unique_ptr<FrameConfig>> FrameConfigQueue;
    Mutexed<FrameConfigQueue> mFrameCfgQueue;

    // configurations used by component in process
    // (TODO: keep this in intf but make them internal only)
    std::shared_ptr<C2StreamPictureSizeInfo::input> mSize;
    std::shared_ptr<C2StreamPixelFormatInfo::input> mPixelFormat;
    std::shared_ptr<C2StreamIntraRefreshTuning::output> mIntraRefresh;
    std::shared_ptr<C2StreamFrameRateInfo::output> mFrameRate;
    std::shared_ptr<C2StreamBitrateInfo::output> mBitrate;
    std::shared_ptr<C2StreamRequestSyncFrameTuning::output> mRequestSync;

    status_t initComponent();
    status_t releaseComponent();
    status_t getDynamicConfigParam(uint64_t index);
    status_t setDynamicConfigParam(uint64_t index);
    status_t handleOutputFrame(int32_t outFrameId, uint32_t frameSize,
                                        uint64_t timestamp, int keyFrame, uint32_t offset,
                                        const std::unique_ptr<C2Work>& work);
    status_t handleInputUsed(int inputId);
    status_t encoderQueueBuffer(const std::unique_ptr<C2Work> &work, IMXInputBuffer* pInBuffer);
};

} // namespace android

#endif  // IMX_C2_VIDEO_ENCODER_H_
