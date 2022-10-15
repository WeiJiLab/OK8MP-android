/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "G2dPreProcess"

#include <cutils/properties.h>
#include <utils/Log.h>
#include <dlfcn.h>

#include "G2dPreProcess.h"
#include "graphics_ext.h"

//#define DUMP_G2D_OUTPUT
#ifdef DUMP_G2D_OUTPUT
#include "IonAllocator.h"
#include <sys/mman.h>
#endif

#define G2D_PRE_PROCESS_API_TRACE
#ifdef G2D_PRE_PROCESS_API_TRACE
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

namespace android {

g2d_format ConvertColorFmtPixel2G2D(int pixelColorFmt) {
  g2d_format g2dColorFmt = G2D_YUYV;

  switch(pixelColorFmt) {
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_YCBCR_420_888:
        g2dColorFmt = G2D_NV12;
        break;
    case HAL_PIXEL_FORMAT_YCbCr_420_P:
        g2dColorFmt = G2D_I420;
        break;
    case HAL_PIXEL_FORMAT_YV12:
        g2dColorFmt = G2D_YV12;
        break;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
        g2dColorFmt = G2D_YUYV;
        break;
    case HAL_PIXEL_FORMAT_RGB_565:
        g2dColorFmt = G2D_RGB565;
        break;
    case HAL_PIXEL_FORMAT_RGB_888:
        g2dColorFmt = G2D_RGB888;
        break;
    case HAL_PIXEL_FORMAT_RGBA_8888:
        g2dColorFmt = G2D_RGBA8888;
        break;
    case HAL_PIXEL_FORMAT_RGBX_8888:
        g2dColorFmt = G2D_RGBX8888;
        break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
        g2dColorFmt = G2D_BGRA8888;
        break;
    default:
      G2DPP_ERR_LOG("unknown pixelColorFmt %d\n", pixelColorFmt);
      break;
  }

  return g2dColorFmt;
}

#ifdef DUMP_G2D_OUTPUT
void dumpOutput(int fd, uint32_t size) {
    if (fd < 0 || size == 0)
        return;

    unsigned long virtAddr = 0;
    bool needUnmap = false;
    fsl::IonAllocator * pIonAllocator = fsl::IonAllocator::getInstance();
    int ret = pIonAllocator->getVaddrs(fd, size, (uint64_t&)virtAddr);

    if (ret != 0) {
        G2DPP_ERR_LOG("Ion get physical address failed, fd %d", fd);
    } else
        needUnmap = true;

    if (virtAddr > 0) {
        FILE * pfile;
        pfile = fopen("/data/dumpG2DYUV","ab");
        if(pfile) {
            G2DPP_LOG("dump size %d", size);
            fwrite((void*)virtAddr,1,size,pfile);
            fclose(pfile);
        } else {
            G2DPP_ERR_LOG("open dumpfile failed\n");
        }
    }
    if (needUnmap && virtAddr > 0)
        munmap((void*)virtAddr, size);
}
#endif

G2dPreProcess::G2dPreProcess() {
    pPPHandle = nullptr;
    mAsync = false;
    memset(&sG2dModule, 0, sizeof(G2D_MODULE));
    memset(&sOutMemInfo, 0, sizeof(PROCESS_MEM_INFO));
}

G2dPreProcess::~G2dPreProcess() {
}

status_t G2dPreProcess::onProcess() {
    if (mInputQueue.size() == 0 || mOutputQueue.size() == 0) {
        return OK;
    }

    int inIndex, outIndex;
    int inId, outId;
    int inFd, outFd;
    unsigned long inPhys, outPhys;
    uint32_t inFlag = 0;
    uint32_t outFlag = 0;

    inIndex = mInputQueue.front();
    outIndex = mOutputQueue.front();

    ProcessFrameGetNode(&sInMemInfo, inIndex, &inPhys, &inId, &inFd, &inFlag);
    ProcessFrameGetNode(&sOutMemInfo, outIndex, &outPhys, &outId, &outFd, &outFlag);

    if (!inPhys || !outPhys) {
        G2DPP_ERR_LOG("invalid address: inPhys %p outPhys %p", (void*)inPhys, (void*)outPhys);
        return BAD_VALUE;
    }

    G2DPP_LOG("G2dPreProcess::doProcess inPhys: %p outPhys: %p", (void*)inPhys, (void*)outPhys);

    if (sG2dModule.mG2dOpen(&pPPHandle->g2dHandle) == -1) {
        G2DPP_ERR_LOG("g2d_open failed\n");
        return BAD_VALUE;
    } else if (nullptr == pPPHandle->g2dHandle) {
        G2DPP_ERR_LOG("g2d_open return null handle\n");
        return BAD_VALUE;
    }


    pPPHandle->sInput.planes[0] = (int)inPhys;
    if (pPPHandle->sInput.format == G2D_YV12) {
        int Ysize = pPPHandle->sInput.width * pPPHandle->sInput.height;
        pPPHandle->sInput.planes[1] = pPPHandle->sInput.planes[0] + Ysize;
        pPPHandle->sInput.planes[2] = pPPHandle->sInput.planes[0] + Ysize + Ysize / 4;
    }
    pPPHandle->sOutput.planes[0] = (int)outPhys;

    if (sG2dModule.mG2dBlit(pPPHandle->g2dHandle, &pPPHandle->sInput, &pPPHandle->sOutput) != 0) {
        G2DPP_ERR_LOG("g2d_blit failed\n");
        return BAD_VALUE;
    }

    if (sG2dModule.mG2dFinish(pPPHandle->g2dHandle) != 0) {
        G2DPP_ERR_LOG("g2d_finish failed\n");
        return BAD_VALUE;
    }
    if (sG2dModule.mG2dClose(pPPHandle->g2dHandle) != 0) {
        G2DPP_ERR_LOG("g2d_close failed\n");
        return BAD_VALUE;
    }

    #ifdef DUMP_G2D_OUTPUT
    dumpOutput(outFd, sOutFormat.bufferSize);
    #endif

    pPPHandle->g2dHandle = NULL;
    mInputQueue.pop();
    mOutputQueue.pop();

    ProcessFrameClear(&sInMemInfo, inIndex);

    NotifyProcessInputUsed(inId);
    NotifyProcessDone(outId, 0);

    return OK;
}

status_t G2dPreProcess::onInit() {
    char g2dlibName[PATH_MAX] = {0};
    uint32_t alignedWidth = 0;
    uint32_t alignedHeight = 0;

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
    sG2dModule.mG2dBlit = (g2d_func2)dlsym(sG2dModule.hLibHandle, "g2d_blit");

    if (!sG2dModule.mG2dOpen || !sG2dModule.mG2dOpen ||
            !sG2dModule.mG2dOpen || !sG2dModule.mG2dOpen) {
        G2DPP_ERR_LOG("dlsym failed, err: %s", dlerror());
        return BAD_VALUE;
    }

    pPPHandle = (VpuEncoderGpuHandle*)malloc(sizeof(VpuEncoderGpuHandle));
    if (!pPPHandle)
        return BAD_VALUE;

    memset(&pPPHandle->sInput, 0, sizeof(G2DSurface));
    memset(&pPPHandle->sOutput, 0, sizeof(G2DSurface));

    // MA-16413 workaround: gralloc allocate YV12 buffer and align width with 32, as encoder still
    // be configured with original width, need to use g2d to crop aligned width to original width
    // vendor/nxp-opensource/imx/display/display/MemoryDesc.cpp#144, YV12 width is aligned with 32
    if (sInFormat.format == HAL_PIXEL_FORMAT_YV12 && (sInFormat.width % 32) != 0) {
        alignedWidth = ((sInFormat.width +31) & ~31);
    } else
        alignedWidth = sInFormat.width;

    alignedHeight = sInFormat.height;

    pPPHandle->sInput.format = ConvertColorFmtPixel2G2D(sInFormat.format);
    pPPHandle->sInput.left = 0;
    pPPHandle->sInput.top = 0;
    pPPHandle->sInput.right = sInFormat.width;
    pPPHandle->sInput.bottom = sInFormat.height;
    pPPHandle->sInput.width = alignedWidth;
    pPPHandle->sInput.height = alignedHeight;
    pPPHandle->sInput.stride = alignedWidth;

    pPPHandle->sOutput.format = G2D_YUYV;
    pPPHandle->sOutput.left = 0;
    pPPHandle->sOutput.top = 0;
    pPPHandle->sOutput.right = sInFormat.width;
    pPPHandle->sOutput.bottom = sInFormat.height;
    pPPHandle->sOutput.width = sInFormat.width;
    pPPHandle->sOutput.height = sInFormat.height;
    pPPHandle->sOutput.stride = sInFormat.stride;

    sOutFormat.width = pPPHandle->sOutput.width;
    sOutFormat.height = pPPHandle->sOutput.height;
    sOutFormat.stride = pPPHandle->sOutput.stride;
    sOutFormat.format = HAL_PIXEL_FORMAT_YCbCr_422_I;
    sOutFormat.bufferSize = sOutFormat.width * sOutFormat.height * 2;

    G2DPP_LOG("input right %d bottom %d", pPPHandle->sInput.right, pPPHandle->sInput.bottom);
    G2DPP_LOG("input aligned w h = %d x %d", alignedWidth, alignedHeight);

    if (OK != AllocateProcessBuffers(DEFAULT_PROCESS_BUFFER_NUM))
        return BAD_VALUE;

    return OK;
}

status_t G2dPreProcess::onDestroy() {
    if (pPPHandle) {
        free(pPPHandle);
        pPPHandle = nullptr;
    }

    if (sG2dModule.hLibHandle) {
        // no need to close lib handle becuase it can be used for next instance.
        // follow the usage as hwcomposer/camera.
        //dlclose(sG2dModule.hLibHandle);
        memset(&sG2dModule, 0, sizeof(G2D_MODULE));
    }

    return OK;
}
status_t G2dPreProcess::onStart()
{
    return OK;
}
status_t G2dPreProcess::onStop()
{
    return OK;
}
status_t G2dPreProcess::DoSetConfig(ProcessConfig index, void* pConfig) {
    return BAD_VALUE; // not support any index yet
}

status_t G2dPreProcess::DoGetConfig(ProcessConfig index, void* pConfig) {
    return BAD_VALUE; // not support any index yet
}

ProcessBase * CreatePreProcessInstance() {
    return static_cast<ProcessBase *>(new G2dPreProcess());
}


} // namespcae android
