/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */
#ifndef V4L2_DECODER_H
#define V4L2_DECODER_H

#include "VideoDecoderBase.h"
#include "V4l2Dev.h"


namespace android {

class V4l2Dec : public VideoDecoderBase{
public:
    V4l2Dec(const char* mime);
    status_t onInit() override;
    status_t onStart() override;
    status_t onFlush() override;
    status_t onStop() override;
    status_t onDestroy() override;

    bool checkIfPostProcessNeeded() override;

    status_t prepareOutputBuffers();
    status_t destroyOutputBuffers();
    status_t decodeInternal(std::unique_ptr<IMXInputBuffer> input);
    //status_t queueInput(C2FrameData * input, int32_t input_id);
    status_t queueOutput(GraphicBlockInfo* pInfo);
    status_t queueOutput(int index);
    status_t dequeueInputBuffer();
    status_t dequeueOutputBuffer();

    status_t importOutputBuffers(std::vector<GraphicBlockInfo> buffers) override;

protected:
    virtual ~V4l2Dec();
    status_t DoSetConfig(DecConfig index, void* pConfig) override;
    status_t DoGetConfig(DecConfig index, void* pConfig) override;

    status_t allocateOutputBuffers() override;
    status_t freeOutputBuffers() override;
    bool OutputBufferFull() override;
private:
    enum {
        kInputBufferPlaneNum = 1,
        kOutputBufferPlaneNum = 2,

    };

    struct VideoFramePlane {
        int32_t fd;
        uint64_t vaddr;
        uint64_t paddr;
        uint32_t size;
        uint32_t length;
        uint32_t offset;
    };

    // Record for input buffers.
    struct InputRecord {
        InputRecord();
        ~InputRecord();
        bool at_device;    // held by device.
        VideoFramePlane plane;
        int32_t input_id;
        int64_t ts;
    };


    struct OutputRecord {
        OutputRecord();
        OutputRecord(OutputRecord&&) = default;
        ~OutputRecord();
        bool at_device;
        VideoFramePlane planes[kOutputBufferPlaneNum];
        int32_t picture_id;     // picture buffer id as returned to PictureReady().
        uint32_t flag;
        std::shared_ptr<C2GraphicBlock> mGraphicBlock;
    };

    enum {
        UNINITIALIZED,
        STOPPED,
        RUNNING,
        STOPPING,
        FLUSHING,
        RES_CHANGING,
    };

    const char* mMime;
    pthread_t mPollThread;
    pthread_t mFetchThread;

    V4l2Dev* pDev;
    int32_t mFd;

    enum v4l2_memory mInMemType;//support userptr and dma
    enum v4l2_memory mOutMemType;//support userptr and dma


    uint32_t mInFormat;//v4l2 input format
    std::vector<InputRecord> mInputBufferMap;
    std::vector<OutputRecord> mOutputBufferMap;

    Mutex mLock;
    Mutex mThreadLock;

    bool bPollStarted;
    bool bPollStopped;
    bool bFetchStarted;
    bool bFetchStopped;

    bool bInputStreamOn;
    bool bOutputStreamOn;

    bool bNeedPostProcess;
    bool bMpeg2;
    bool bH264;
    bool bLowLatency;

    int mState;

    uint32_t mVpuOwnedOutputBufferNum;

    uint32_t mOutputPlaneSize[kOutputBufferPlaneNum];
    uint32_t mOutFormat;//v4l2 output format

    uint32_t mVc1Format;

    uint32_t mInCnt;
    uint32_t mOutCnt;
    uint32_t nDebugFlag;
    uint64_t mLastInputTs;
    int mLastInputId;

    VideoColorAspect mIsoColorAspect;

    status_t prepareInputParams();
    status_t SetInputFormats();
    status_t prepareOutputParams();
    status_t SetOutputFormats();

    status_t prepareInputBuffers();
    status_t createInputBuffers();
    status_t destroyInputBuffers();

    status_t createPollThread();
    status_t destroyPollThread();
    status_t createFetchThread();
    status_t destroyFetchThread();

    status_t startInputStream();
    status_t stopInputStream();
    status_t startOutputStream();
    status_t stopOutputStream();

    status_t stopStreaming();
    status_t startStreaming();

    status_t onDequeueEvent();

    static void *PollThreadWrapper(void *);
    status_t HandlePollThread();
    static void *FetchThreadWrapper(void *);
    status_t HandleFetchThread();

    status_t handleFormatChanged();
    void ParseVpuLogLevel();
    void dumpInputBuffer(void* inBuf, uint32_t size);
    void dumpOutputBuffer(void* buf, uint32_t size);
};



}
#endif
