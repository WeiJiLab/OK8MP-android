/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#ifndef G2D_POST_PROCESS_H
#define G2D_POST_PROCESS_H

#include <queue>

#include "ProcessBase.h"
#include "g2d.h"
#include "g2dExt.h"

namespace android {

typedef struct g2d_surfaceEx G2DSurfaceEx;

typedef struct {
    void * g2dHandle;
} VpuDecoderGpuHandle;

typedef int (*g2d_func1)(void* handle);
typedef int (*g2d_func2)(void *handle, G2DSurfaceEx *srcEx, G2DSurfaceEx *dstEx);

typedef struct {
    void * hLibHandle;
    g2d_func1 mG2dOpen;
    g2d_func1 mG2dFinish;
    g2d_func1 mG2dClose;
    g2d_func2 mG2dBlitEx;
} G2D_MODULE;



class G2dPostProcess : public ProcessBase {
public:
    G2dPostProcess();
    virtual ~G2dPostProcess();

    status_t onInit() override;
    status_t onDestroy() override;
    status_t onStart() override;
    status_t onStop() override;
protected:

    status_t onProcess() override;
    status_t onVideoResChanged() override;
    status_t DoSetConfig(ProcessConfig index, void* pConfig) override;
    status_t DoGetConfig(ProcessConfig index, void* pConfig) override;

private:
    VpuDecoderGpuHandle* pPPHandle; // preprocess handle
    G2D_MODULE sG2dModule;
    G2DSurfaceEx sSrcSurface;
    G2DSurfaceEx sDstSurface;

    bool bFetchStarted;
    bool bFetchStopped;
    uint32_t nDebugFlag;

    pthread_t mFetchThread;

    status_t createFetchThread();
    status_t destroyFetchThread();
    static void *FetchThreadWrapper(void *);
    status_t HandleFetchThread();

    void setPostProcessParameters();
    void ParseG2dLogLevel();
    void dumpOutBuffer(int fd, uint32_t size);
};

} // namespcae android

#endif // G2D_POST_PROCESS_H
