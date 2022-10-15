/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "G2dPostProcess"

#include <cutils/properties.h>
#include <utils/Log.h>
#include <dlfcn.h>

#include "G2dPostProcess.h"
#include "graphics_ext.h"
#include <sys/mman.h>

#define G2D_POST_PROCESS_API_TRACE
#ifdef G2D_POST_PROCESS_API_TRACE
#define G2DPP_API_TRACE ALOGV
#else
#define G2DPP_API_TRACE(...)
#endif

#define G2D_PRE_PROCESS_DEBUG
#ifdef G2D_PRE_PROCESS_DEBUG
#define G2DPP_LOG ALOGV
#else
#define G2DPP_LOG(...)
#endif

#define G2DPP_ERR_LOG ALOGE

//#define G2D_LOG_TS

#define G2D_POST_LOG_LEVELFILE "/data/g2d_post_level"
#define G2D_POST_INPUT_FILE "/data/g2d_post_in.yuv"
#define G2D_POST_OUTPUT_FILE "/data/g2d_post_out.yuv"

#define DUMP_G2D_POST_FLAG_INPUT     0x1
#define DUMP_G2D_POST_FLAG_OUTPUT    0x2

namespace android {

G2dPostProcess::G2dPostProcess() {
    pPPHandle = nullptr;
    mFetchThread = 0;
    mAsync = false;

    memset(&sG2dModule, 0, sizeof(G2D_MODULE));
    memset(&sOutMemInfo, 0, sizeof(PROCESS_MEM_INFO));
    memset(&sSrcSurface, 0, sizeof(G2DSurfaceEx));
    memset(&sDstSurface, 0, sizeof(G2DSurfaceEx));
    bFetchStarted = false;
    bFetchStopped = true;
}

G2dPostProcess::~G2dPostProcess() {
}

status_t G2dPostProcess::createFetchThread() {
    Mutex::Autolock autoLock(mLock);
    if (!bFetchStarted) {
        ALOGV("createFetchThread mFetchThread=%d",mFetchThread);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        bFetchStarted = true;
        bFetchStopped = false;

        pthread_create(&mFetchThread, &attr, FetchThreadWrapper, this);
        pthread_attr_destroy(&attr);
    }
    return OK;
}

status_t G2dPostProcess::destroyFetchThread() {

    if (bFetchStarted) {
        ALOGV("destroyFetchThread mFetchThread=%d",mFetchThread);
        bFetchStarted = false;
        do {
            usleep(1000);
        } while (!bFetchStopped);
        Mutex::Autolock autoLock(mLock);
        pthread_join(mFetchThread, NULL);
    }
    return OK;
}

void *G2dPostProcess::FetchThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<G2dPostProcess *>(me)->HandleFetchThread();
}

status_t G2dPostProcess::HandleFetchThread() {
    while(bFetchStarted){
        ALOGV("HandleFetchThread loop begin mOutputQueue size=%d",mOutputQueue.size());

        // only start fetching when process output buffer isn't enough
        if (mOutputQueue.size() >= 3 || RUNNING != mState) {
            usleep(5000);
            continue;
        }

        int bufferId, index;
        unsigned long phys;

        if (OK != FetchProcessBuffer(&bufferId, &phys)) {
            usleep(5000);
            continue;
        }

        if(!bFetchStarted)
            break;

        Mutex::Autolock autoLock(mLock);

        if(!bFetchStarted){
            break;
        }

        ProcessFrameSet(&sOutMemInfo, phys, bufferId, -1, 0, &index);
        mOutputQueue.push(index);
        Post_ProcessMessage();
    }
    ALOGV("HandleFetchThread stopped");
    bFetchStopped = true;
    return OK;
}

status_t G2dPostProcess::onProcess() {
    status_t ret = OK;
    int inIndex, outIndex;
    int inId, outId;
    int inFd, outFd;
    unsigned long inPhys, outPhys;
    uint32_t inFlag= 0;
    uint32_t outFlag= 0;

    if (mInputQueue.size() == 0 || mOutputQueue.size() == 0) {
        return BAD_VALUE;
    }

    // now we have input and output, post process can star
    inIndex = mInputQueue.front();
    ProcessFrameGetNode(&sInMemInfo, inIndex, &inPhys, &inId, &inFd, &inFlag);

    outIndex = mOutputQueue.front();
    ProcessFrameGetNode(&sOutMemInfo, outIndex, &outPhys, &outId, &outFd, &outFlag);

    if(!inPhys && (inFlag & C2FrameData::FLAG_END_OF_STREAM)){
        NotifyProcessEos();
        ALOGE("G2dPostProcess NotifyProcessEos !");
        return BAD_VALUE;
    }

    if (!inPhys || !outPhys) {
        G2DPP_ERR_LOG("invalid address: inPhys %p outPhys %p inId %d, outId %d, inIndex %d, outIndex %d",
            (void*)inPhys, (void*)outPhys, inId, outId, inIndex, outIndex);
        return BAD_VALUE;
    }

    G2DPP_LOG("G2dPostProcess::doProcess inPhys: %p outPhys: %p, w*h(%d x %d), inId %d, outId %d inIndex %d, outIndex %d",
        (void*)inPhys, (void*)outPhys, sInFormat.width, sInFormat.height, inId, outId, inIndex, outIndex);

    //enable the macro can make workaround for g2d
    #if 0
    sSrcSurface.base.left = 0;
    sSrcSurface.base.top = 0;
    sSrcSurface.base.right = sInFormat.width;
    sSrcSurface.base.bottom = sInFormat.height;
    sSrcSurface.base.width = sInFormat.width;
    sSrcSurface.base.height = sInFormat.height;
    sSrcSurface.base.stride = sInFormat.stride;

    sDstSurface.base.left = 0;
    sDstSurface.base.top = 0;
    sDstSurface.base.right = sOutFormat.width;
    sDstSurface.base.bottom = sOutFormat.height;

    sDstSurface.base.stride = sOutFormat.stride;
    sDstSurface.base.width = sOutFormat.width;
    sDstSurface.base.height = sOutFormat.height;
    #endif

    sSrcSurface.base.planes[0] = (int)inPhys;
    sSrcSurface.base.planes[1] = (int)inPhys + sSrcSurface.base.stride * sSrcSurface.base.height;
    ALOGV("src plane[0]=0x%x,plane[1]=0x%x, stride=%d,src w=%d",
        sSrcSurface.base.planes[0],sSrcSurface.base.planes[1],sSrcSurface.base.stride,sSrcSurface.base.width);

    sDstSurface.base.planes[0] = (int)outPhys;
    if(sDstSurface.base.format == G2D_NV12){
        sDstSurface.base.planes[1] = (int)outPhys + sDstSurface.base.stride * sDstSurface.base.height;
    }

    ALOGV("dst plane[0]=0x%x,plane[1]=0x%x, stride=%d,src w=%d",
        sDstSurface.base.planes[0],sDstSurface.base.planes[1],sDstSurface.base.stride,sDstSurface.base.width);

    #ifdef G2D_LOG_TS
    struct timeval tv, tv1;
    gettimeofday (&tv, nullptr);
    #endif

    if (sG2dModule.mG2dBlitEx(pPPHandle->g2dHandle, &sSrcSurface, &sDstSurface) != 0) {
        G2DPP_ERR_LOG("g2d_blitEx failed\n");
        return BAD_VALUE;
    }

    if (sG2dModule.mG2dFinish(pPPHandle->g2dHandle) != 0) {
        G2DPP_ERR_LOG("g2d_finish failed\n");
        return BAD_VALUE;
    }

    #ifdef G2D_LOG_TS
    gettimeofday (&tv1, nullptr);
    ALOGV("G2dProcess::Process cost: %d\n", (tv1.tv_sec-tv.tv_sec)*1000+(tv1.tv_usec-tv.tv_usec)/1000);
    #endif

    dumpOutBuffer(outId,sOutFormat.bufferSize);
    ALOGV("G2dProcess::Process mG2dFinish");

    mInputQueue.pop();
    ProcessFrameClear(&sInMemInfo, inIndex);

    mOutputQueue.pop();
    ProcessFrameClear(&sOutMemInfo, outIndex);

    NotifyProcessInputUsed(inId);
    NotifyProcessDone(outId,inFlag);
    if(inFlag & C2FrameData::FLAG_END_OF_STREAM)
        NotifyProcessEos();

    return OK;
}

status_t G2dPostProcess::onVideoResChanged() {
    if (bFetchStarted) {
        destroyFetchThread();
        FreeProcessBuffers();
        Mutex::Autolock autoLock(mLock);
        memset(&sOutMemInfo, 0, sizeof(PROCESS_MEM_INFO));
        std::queue<int> empty;
        std::swap(mOutputQueue, empty);
    }

    setPostProcessParameters();

    if (!bFetchStarted) {
        createFetchThread();
    }

    ALOGV("onVideoResChanged done");

    return OK;
}

status_t G2dPostProcess::onInit() {
    G2DPP_LOG("%s", __FUNCTION__);

    char g2dlibName[PATH_MAX] = {0};

    if(getDefaultG2DLib(g2dlibName, PATH_MAX)){
        sG2dModule.hLibHandle = dlopen(g2dlibName, RTLD_NOW);
    }

    if (nullptr == sG2dModule.hLibHandle) {
        G2DPP_ERR_LOG("dlopen %s failed, err: %s", g2dlibName, dlerror());
        return BAD_VALUE;
    }

    sG2dModule.mG2dOpen = (g2d_func1)dlsym(sG2dModule.hLibHandle, "g2d_open");
    sG2dModule.mG2dClose = (g2d_func1)dlsym(sG2dModule.hLibHandle, "g2d_close");
    sG2dModule.mG2dFinish = (g2d_func1)dlsym(sG2dModule.hLibHandle, "g2d_finish");
    sG2dModule.mG2dBlitEx = (g2d_func2)dlsym(sG2dModule.hLibHandle, "g2d_blitEx");

    if (!sG2dModule.mG2dOpen || !sG2dModule.mG2dClose ||
            !sG2dModule.mG2dFinish || !sG2dModule.mG2dBlitEx) {
        G2DPP_ERR_LOG("dlsym failed, err: %s", dlerror());
        return BAD_VALUE;
    }

    pPPHandle = (VpuDecoderGpuHandle*)malloc(sizeof(VpuDecoderGpuHandle));
    if (!pPPHandle)
        return BAD_VALUE;

    if (sG2dModule.mG2dOpen(&pPPHandle->g2dHandle) == -1) {
        G2DPP_ERR_LOG("g2d_open failed\n");
        return BAD_VALUE;
    } else if (nullptr == pPPHandle->g2dHandle) {
        G2DPP_ERR_LOG("g2d_open return null handle\n");
        return BAD_VALUE;
    }

    setPostProcessParameters();
    ParseG2dLogLevel();
    createFetchThread();

    return OK;
}
status_t G2dPostProcess::onStart()
{
    createFetchThread();
    return OK;
}
status_t G2dPostProcess::onStop()
{
    destroyFetchThread();
    return OK;
}
status_t G2dPostProcess::onDestroy() {
    G2DPP_LOG("%s", __FUNCTION__);

    onStop();

    if (pPPHandle && pPPHandle->g2dHandle && sG2dModule.mG2dClose) {
        if (sG2dModule.mG2dClose(pPPHandle->g2dHandle) != 0) {
            G2DPP_ERR_LOG("g2d_close failed\n");
            return BAD_VALUE;
        }
        pPPHandle->g2dHandle = NULL;
    }

    if (pPPHandle) {
        free(pPPHandle);
        pPPHandle = nullptr;
    }

    if (sG2dModule.hLibHandle) {
        dlclose(sG2dModule.hLibHandle);
        memset(&sG2dModule, 0, sizeof(G2D_MODULE));
    }

    return OK;
}

void G2dPostProcess::setPostProcessParameters() {

    sOutFormat.width = sInFormat.width;
    sOutFormat.height = sInFormat.height;
    sOutFormat.stride = sInFormat.width;

    if(sInFormat.format == HAL_PIXEL_FORMAT_P010 || (sInFormat.flag & PROCESSBASE_FORMAT_FLAG_NV12)){
        sOutFormat.format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
        sOutFormat.bufferSize = sOutFormat.width * sOutFormat.height * 3/2;
        ALOGV("sOutFormat.format HAL_PIXEL_FORMAT_YCbCr_420_SP");
    }else{
        sOutFormat.format = HAL_PIXEL_FORMAT_YCbCr_422_I;
        sOutFormat.bufferSize = sOutFormat.width * sOutFormat.height * 2;
        ALOGV("sOutFormat.format HAL_PIXEL_FORMAT_YCbCr_422_I");
    }

    sSrcSurface.base.format = G2D_NV12;

    sSrcSurface.base.left = 0;
    sSrcSurface.base.top = 0;
    sSrcSurface.base.right = sInFormat.width;
    sSrcSurface.base.bottom = sInFormat.height;

    sSrcSurface.base.width = sInFormat.width;
    sSrcSurface.base.height = sInFormat.height;
    sSrcSurface.base.stride = sInFormat.stride;
    sSrcSurface.tiling = G2D_AMPHION_TILED;

    if (sInFormat.interlaced) {
        sSrcSurface.tiling = (enum g2d_tiling)(sSrcSurface.tiling | G2D_AMPHION_INTERLACED);
        G2DPP_LOG("it is interlaced source");
    }

    if(sInFormat.format == HAL_PIXEL_FORMAT_P010){
        sSrcSurface.tiling = G2D_AMPHION_TILED_10BIT;
        ALOGV("10 bit video");
    }

    ALOGV("sSrcSurface w=%d,h=%d,right=%d,bottom=%d,stride=%d", 
        sSrcSurface.base.width, sSrcSurface.base.height, sSrcSurface.base.right, sSrcSurface.base.bottom, sSrcSurface.base.stride);

    sDstSurface.tiling = G2D_LINEAR;

    if(sOutFormat.format == HAL_PIXEL_FORMAT_YCbCr_420_SP){
        sDstSurface.base.format = G2D_NV12;
    }else
        sDstSurface.base.format = G2D_YUYV;

    sDstSurface.base.left = 0;
    sDstSurface.base.top = 0;
    sDstSurface.base.right = sOutFormat.width;
    sDstSurface.base.bottom = sOutFormat.height;

    sDstSurface.base.stride = sOutFormat.stride;
    sDstSurface.base.width = sOutFormat.width;
    sDstSurface.base.height = sOutFormat.height;

    ALOGV("sDstSurface w=%d,h=%d,right=%d,bottom=%d,stride=%d", 
        sDstSurface.base.width, sDstSurface.base.height, sDstSurface.base.right, sDstSurface.base.bottom, sDstSurface.base.stride);
}

status_t G2dPostProcess::DoSetConfig(ProcessConfig index, void* pConfig) {
    return BAD_VALUE; // not support any index yet
}

status_t G2dPostProcess::DoGetConfig(ProcessConfig index, void* pConfig) {
    return BAD_VALUE; // not support any index yet
}
void G2dPostProcess::ParseG2dLogLevel()
{
    int level=0;
    FILE* fpVpuLog;
    nDebugFlag = 0;
    
    fpVpuLog=fopen(G2D_POST_LOG_LEVELFILE, "r");
    if (NULL==fpVpuLog){
        return;
    }

    char symbol;
    int readLen = 0;

    readLen = fread(&symbol,1,1,fpVpuLog);
    if(feof(fpVpuLog) != 0){
        ;
    }
    else{
        level=atoi(&symbol);
        if((level<0) || (level>255)){
            level=0;
        }
    }
    fclose(fpVpuLog);

    nDebugFlag=level;

    if(nDebugFlag != 0)
        ALOGV("ParseVpuLogLevel nDebugFlag=%x",nDebugFlag);
    return;
}
void G2dPostProcess::dumpOutBuffer(int id, uint32_t size)
{
    FILE * pfile = NULL;
    void* buf = NULL;

    if(!(nDebugFlag & DUMP_G2D_POST_FLAG_OUTPUT))
        return;

    ProcessBlockInfo* pInfo = getProcessBlockById(id);

    if(pInfo->mFd <= 0){
        ALOGV("dumpOutBuffer invalid fd");
        return;
    }

    pfile = fopen(G2D_POST_OUTPUT_FILE,"ab");

    if(pfile){
        buf = mmap(0, size, PROT_READ, MAP_SHARED, pInfo->mFd, 0);
        fwrite(buf,1,size,pfile);
        ALOGV("dumpOutBuffer write %d",size);
        munmap(buf, size);
        fclose(pfile);
    }else
        ALOGV("dumpOutBuffer failed to open %s",G2D_POST_OUTPUT_FILE);
    return;
}

ProcessBase * CreatePostProcessInstance() {
    return static_cast<ProcessBase *>(new G2dPostProcess());
}


} // namespcae android
