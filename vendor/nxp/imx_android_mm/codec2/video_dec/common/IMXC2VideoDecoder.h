/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef IMX_VIDEO_DECODER_H_
#define IMX_VIDEO_DECODER_H_

#include <log/log.h>

#include <media/stagefright/foundation/MediaDefs.h>

#include <C2Debug.h>
#include <C2PlatformSupport.h>
#include <IMXC2Interface.h>

#include "IMXC2ComponentBase.h"
#include "ProcessBase.h"
#include "VideoDecoderBase.h"

namespace android {

class IMXC2VideoDecoder : public IMXC2ComponentBase,
						        public ProcessBase::Client,
						        public VideoDecoderBase::Client {
public:

	class IntfImpl;

    IMXC2VideoDecoder(const char* name, c2_node_id_t id, const std::shared_ptr<IntfImpl> &intfImpl);
    virtual ~IMXC2VideoDecoder();

	// from VideoDecoderBase
	void notifyVideoInfo(VideoFormat *pformat) override;
	void clearPictureBuffer() override;
    void notifyPictureReady(int32_t pictureId, uint64_t timestamp) override;
    void notifyInputBufferUsed(int32_t input_id) override;
    void notifySkipInputBuffer(int32_t input_id) override;
    void notifyFlushDone() override;
    void notifyResetDone() override;
    void notifyError(status_t err) override;
    void notifyEos() override;

	// from ProcessBase
    status_t fetchProcessBuffer(int *bufferId, unsigned long *phys) override;
    status_t notifyProcessInputUsed(int inputId) override;
    status_t notifyProcessDone(int outputId, uint64_t timestamp, uint32_t flag) override;
    status_t notifyProcessOutputClear() override;
    status_t notifyProcessFlushDone() override;
    status_t notifyProcessResetDone() override;
    void notifyProcessError() override;
    void notifyProcessEos() override;

    void outputBufferReturned(int32_t pictureId);

protected:

	// From IMXC2Component
    c2_status_t onInit() override;
    c2_status_t onStop() override;
    c2_status_t onFlush_sm() override;
    void onReset() override;
    void onRelease() override;
    void processWork(const std::unique_ptr<C2Work> &work) override;
    c2_status_t drainInternal(uint32_t drainMode) override;


private:
	sp<ProcessBase> mPostProcess;
	sp<VideoDecoderBase> mDecoder;
	std::shared_ptr<IntfImpl> mIntf;
	// The vector of storing allocated output graphic block information.
    std::vector<std::shared_ptr<C2GraphicBlock>> mOutputBufferMap;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mCropWidth;
    uint32_t mCropHeight;
    uint32_t nOutBufferNum;

	C2String mName; //component name or role name ?
    bool bPendingFmtChanged;
    bool bGetGraphicBlockPool;
    bool bSignalOutputEos;
    bool bSignalledError;
    bool bFlushDone;
    bool bPPEnabled;
    bool bReleasingDecoder;
    bool bSupportColorAspects;

    status_t initInternalParam();    // init internel paramters
    void releaseDecoder();    // release decoder instance

    void handleOutputPicture(GraphicBlockInfo* info, uint64_t timestamp, uint32_t flag);
};


} // namespace android

#endif  // IMX_VIDEO_DECODER_H_
