/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#ifndef ISI_PRE_PROCESS_H
#define ISI_PRE_PROCESS_H

#include <queue>
#include "V4l2Dev.h"
#include "ProcessBase.h"

namespace android {

class IsiPreProcess : public ProcessBase{
public:
    status_t onInit() override;
    status_t onDestroy() override;
    status_t onStart() override;
    status_t onStop() override;
protected:
    status_t onProcess() override;
    status_t DoSetConfig(ProcessConfig index, void* pConfig) override;
    status_t DoGetConfig(ProcessConfig index, void* pConfig) override;
    status_t onFlush();

private:
    enum {
        kInputBufferPlaneNum = 1,
        kOutputBufferPlaneNum = 2,
    };

    int32_t mFd;
    V4l2Dev* pDev;

    pthread_t mPollThread;
    
    enum v4l2_memory mInMemType;//support userptr and dma
    enum v4l2_memory mOutMemType;//support userptr and dma
 
    uint32_t mInFormat;//v4l2 output format
    uint32_t mOutFormat;//v4l2 capture format
    uint32_t mOutputPlaneSize[kOutputBufferPlaneNum];

    uint32_t mWidthAlign;
    uint32_t mHeightAlign;

    Mutex mLock;

    uint64_t mAddOutCnt;
    
    bool bPollStarted;
    bool bInputStreamOn;
    bool bOutputStreamOn;
    bool bSyncFrame;

    uint32_t getV4l2Format(uint32_t color_format);
    status_t prepareInputParams();
    status_t prepareOutputParams();
    status_t SetInputFormats();
    status_t SetOutputFormats();
    status_t prepareInputBuffers();
    status_t prepareOutputBuffers();
    status_t HandlePollThread();
    status_t createPollThread();
    static void *PollThreadWrapper(void *);
    status_t destroyPollThread();
    status_t dequeueInputBuffer();
    status_t dequeueOutputBuffer();
    status_t queueInput();
    status_t queueOutput();
    status_t startInputStream();
    status_t stopInputStream();
    status_t startOutputStream();
    status_t stopOutputStream();
    status_t destroyInputBuffers();
    status_t destroyOutputBuffers();

};

}
#endif
