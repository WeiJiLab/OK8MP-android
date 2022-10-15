/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "VpuWrapperDec"

#include <media/stagefright/MediaDefs.h>
#include <C2PlatformSupport.h>
#include <sys/mman.h>

#include "VpuWrapperDec.h"
#include "graphics_ext.h"
#include "Tsm_wrapper.h"
#include "Imx_ext.h"

namespace android {

#define VPU_COMP_INFO ALOGI

#define VPU_COMP_DBGLOG
#ifdef VPU_COMP_DBGLOG
#define VPU_COMP_LOG    ALOGV
#else
#define VPU_COMP_LOG(...)
#endif

#define VPU_COMP_API_DBGLOG
#ifdef VPU_COMP_API_DBGLOG
#define VPU_COMP_API_LOG    ALOGV
#else
#define VPU_COMP_API_LOG(...)
#endif

#define VPU_COMP_ERR_DBGLOG
#ifdef VPU_COMP_ERR_DBGLOG
#define VPU_COMP_ERR_LOG	ALOGE
#define ASSERT(exp)	if(!(exp)) {ALOGE("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}
#else
#define VPU_COMP_ERR_LOG    ALOGE
#define ASSERT(...)
#endif

#define MAX_FRAME_NUM (30)
// align with hantro definition
#define MAX_VC1_FRAME_NUM 15
#define MAX_RV_FRAME_NUM  16

#define DEFAULT_OUT_BUF_CNT (5)
#define FRAME_SURPLUS	(2)
#define FRAME_MIN_FREE_THD (sInitInfo.nMinFrameBufferCount - 3)
#define DEFAULT_BUF_DELAY   (0)

#define INVALID_ID (-1)

//stride and slice height are both 16 for g1 decoder
//stride is 16 and slice height is 8 for g2 decoder
#define IS_G2_DECODER   ((eCodingFormat == VPU_V_HEVC) || (eCodingFormat == VPU_V_VP9))
#undef FRAME_ALIGN
#define FRAME_ALIGN (8)
#define FRAME_ALIGN_WIDTH (FRAME_ALIGN*2)
#define FRAME_ALIGN_HEIGHT (IS_G2_DECODER ? FRAME_ALIGN : FRAME_ALIGN_WIDTH)

#define Align(ptr,align)	(((unsigned long)(ptr)+(align)-1)/(align)*(align))

#define VPURET2ERR(ret) ((ret != VPU_DEC_RET_SUCCESS) ? BAD_VALUE : OK)
#define CHECK_VPU_RET(ret) if (ret != VPU_DEC_RET_SUCCESS) {ALOGE("%s line %d, ret %d\n", __FUNCTION__, __LINE__, ret); return VPURET2ERR(ret);}
#define CHECK_DEC_STATE(state) \
    if (eVpuDecoderState != state) {\
        VPU_COMP_ERR_LOG("%s: failure: error state transition, current state=%d\n", __FUNCTION__, eVpuDecoderState);\
        return INVALID_OPERATION;\
    }



static int pxlfmt2bpp(int pxlfmt) {
	int bpp; // bit per pixel

	switch(pxlfmt) {
    	case HAL_PIXEL_FORMAT_YCbCr_420_P:
    	case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCBCR_420_888:
        case HAL_PIXEL_FORMAT_NV12_TILED:
    	  bpp = 12;
    	  break;
        case HAL_PIXEL_FORMAT_P010:
        case HAL_PIXEL_FORMAT_P010_TILED:
        case HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED:
          bpp = 15;
          break;
    	case HAL_PIXEL_FORMAT_RGB_565:
    	case HAL_PIXEL_FORMAT_YCbCr_422_P:
    	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCBCR_422_I:
            bpp = 16;
            break;
    	default:
    	  bpp = 0;
    	  break;
	}
	return bpp;
}

int ConvertMjpg2PixelFormat(int sourceFormat, int oriColorFmt) {
	int	pixelFormat;
	int interleave = 0;

	switch (oriColorFmt) {
		case HAL_PIXEL_FORMAT_YCbCr_420_SP:
		case HAL_PIXEL_FORMAT_YCbCr_422_SP:
		//FIXME: add 4:0:0/4:4:4/...
			interleave=1;
			break;
		default:
			break;
	}

	switch (sourceFormat) {
		case VPU_COLOR_420:
			pixelFormat = (1 == interleave) ? HAL_PIXEL_FORMAT_YCbCr_420_SP : HAL_PIXEL_FORMAT_YCbCr_420_P;
			break;
		case VPU_COLOR_422H:
			pixelFormat = (1 == interleave) ? HAL_PIXEL_FORMAT_YCbCr_422_SP : HAL_PIXEL_FORMAT_YCbCr_422_P;
			break;
		default:	//unknow
			VPU_COMP_ERR_LOG("unknown mjpg color format: %d \r\n", sourceFormat);
			pixelFormat = 0;
			break;
	}

	return pixelFormat;
}

// frame pool functions
int32_t FramePoolRegisterBuf(unsigned long pInVirtAddr, unsigned long pInPhyAddr,
                                        int fd, int32_t bufferId, VpuDecoderFrmPoolInfo * pOutFrmPool) {
	int32_t i;
	for (i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
		//insert into empty node
		if (0 == pOutFrmPool->nFrm_phyAddr[i]) {
			pOutFrmPool->eFrmState[i] = VPU_COM_FRM_STATE_FREE;
			pOutFrmPool->nFrm_phyAddr[i] = pInPhyAddr;
			pOutFrmPool->nFrm_virtAddr[i] = pInVirtAddr;
            pOutFrmPool->nFrm_bufferId[i] = bufferId;
            pOutFrmPool->nFrm_ionFd[i] = fd;
			pOutFrmPool->nFrmNum++;
			return pOutFrmPool->nFrmNum;
		}
	}
	return -1;
}

int32_t FramePoolBufExist(int32_t bufferId, VpuDecoderFrmPoolInfo* pInFrmPool, int32_t* pOutIndx) {
	int32_t i;

	for(i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
		if (bufferId == pInFrmPool->nFrm_bufferId[i]) {
			*pOutIndx = i;
			return 1;
		}
	}
	return 0;
}

void FramePoolGetBufProperty(VpuDecoderFrmPoolInfo* pInFrmPool, int32_t Index, VpuDecoderFrmState* pOutState, VpuDecOutFrameInfo** ppOutFrame) {
	*pOutState = pInFrmPool->eFrmState[Index];
	*ppOutFrame = &pInFrmPool->outFrameInfo[Index];
}

void FramePoolSetBufState(VpuDecoderFrmPoolInfo* pInFrmPool, int32_t Index,VpuDecoderFrmState eInState) {
	pInFrmPool->eFrmState[Index] = eInState;
}

int32_t FramePoolBufNum(VpuDecoderFrmPoolInfo* pInFrmPool) {
	return pInFrmPool->nFrmNum;
}

void FramePoolClear(VpuDecoderFrmPoolInfo* pOutFrmPool) {
    int i;
    for(i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
        if (pOutFrmPool->nFrm_ionFd[i] >= 0) {
            close(pOutFrmPool->nFrm_ionFd[i]);
        }
    }

	memset(pOutFrmPool, 0, sizeof(VpuDecoderFrmPoolInfo));

    for(i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
        pOutFrmPool->nFrm_bufferId[i] = -1;
        pOutFrmPool->nFrm_ionFd[i] = -1;
	}
}

int32_t FramePoolDecoderOutReset(VpuDecoderFrmPoolInfo* pInFrmPool, int32_t nInFrameBufNum) {
	int32_t i;
	int32_t nDecCnt = 0;
	int32_t nClearedCnt = 0;

	for (i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
    	if(pInFrmPool->nFrm_virtAddr[i]) {
            if (pInFrmPool->eFrmState[i] != VPU_COM_FRM_STATE_OUT) {
			    pInFrmPool->eFrmState[i] = VPU_COM_FRM_STATE_OUT;
			    pInFrmPool->outFrameInfo[i].pDisplayFrameBuf = NULL;
			    nClearedCnt++;
            }
            nDecCnt++;
		}
	}
	ASSERT(nInFrameBufNum == nDecCnt);
	return nClearedCnt;
}

int32_t FramePoolRecordOutFrame(int32_t bufferId, VpuDecoderFrmPoolInfo* pInFrmPool, VpuDecOutFrameInfo* pInFrameInfo) {
	int32_t i;
	//search matched node and restore output frame info
	for(i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
	    if(pInFrmPool->nFrm_bufferId[i] == bufferId) {
			pInFrmPool->outFrameInfo[i] = *pInFrameInfo;
			return i;
		}
	}
	return -1;
}


int32_t FramePoolFindOneDecoderUnOutputed(VpuDecoderFrmPoolInfo* pInFrmPool, VpuDecOutFrameInfo** ppOutFrame) {
	uint32_t i;
	//find one un-outputed frame
	for (i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
			if (pInFrmPool->eFrmState[i] != VPU_COM_FRM_STATE_OUT) {
				*ppOutFrame = &pInFrmPool->outFrameInfo[i];
				return i;
			}
	}
	*ppOutFrame = NULL;
	return -1;
}

int32_t MemFreeBlock(VpuMemInfo* pMemBlock) {
	int i;
    int32_t err = 1; // ok

	for (i = 0; i < pMemBlock->nSubBlockNum; i++) {
		if (pMemBlock->MemSubBlock[i].MemType == VPU_MEM_VIRT && pMemBlock->MemSubBlock[i].pVirtAddr) {
            free(pMemBlock->MemSubBlock[i].pVirtAddr);
            pMemBlock->MemSubBlock[i].pVirtAddr = nullptr;
        } else if (pMemBlock->MemSubBlock[i].MemType == VPU_MEM_PHY && pMemBlock->MemSubBlock[i].pPhyAddr) {
			VpuMemDesc vpuMem;
			VpuDecRetCode ret;
			vpuMem.nSize = pMemBlock->MemSubBlock[i].nSize;
            vpuMem.nType = VPU_MEM_DESC_NORMAL;
            vpuMem.nVirtAddr = (unsigned long)pMemBlock->MemSubBlock[i].pVirtAddr;
            vpuMem.nPhyAddr = (unsigned long)pMemBlock->MemSubBlock[i].pPhyAddr;
            vpuMem.nCpuAddr = (unsigned long)pMemBlock->MemSubBlock[i].nFd;
			ret = VPU_DecFreeMem(&vpuMem);
			if (ret != VPU_DEC_RET_SUCCESS) {
				VPU_COMP_LOG("%s: free vpu memory failure, ret=%d\n", __FUNCTION__, ret);
                err = 0;
			}
			pMemBlock->MemSubBlock[i].pVirtAddr = nullptr;
			pMemBlock->MemSubBlock[i].pPhyAddr = nullptr;
            pMemBlock->MemSubBlock[i].nFd = -1;
		}
	}

	return err;
}

int32_t MemMallocBlock(VpuMemInfo* pMemBlock) {
	int i;
	int size;

    VPU_COMP_API_LOG("%s: ", __FUNCTION__);

	for (i = 0; i < pMemBlock->nSubBlockNum; i++) {
		size = pMemBlock->MemSubBlock[i].nAlignment + pMemBlock->MemSubBlock[i].nSize;
		if (pMemBlock->MemSubBlock[i].MemType == VPU_MEM_VIRT) {
            unsigned char * ptr = (unsigned char *)malloc(size);
			if (ptr == NULL) {
				VPU_COMP_ERR_LOG("%s: get virtual memory failure, size=%d \r\n", __FUNCTION__, size);
				return 0;
			}
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char *)Align(ptr, pMemBlock->MemSubBlock[i].nAlignment);
        } else {
			VpuMemDesc vpuMem;
			VpuDecRetCode ret;
			vpuMem.nSize = size;
            vpuMem.nType = VPU_MEM_DESC_NORMAL;
			ret = VPU_DecGetMem(&vpuMem);
			if(ret != VPU_DEC_RET_SUCCESS) {
				VPU_COMP_ERR_LOG("%s: get vpu memory failure, size=%d, ret=0x%X \r\n", __FUNCTION__, size, ret);
				return 0;
			}

			pMemBlock->MemSubBlock[i].pVirtAddr = (unsigned char *)Align(vpuMem.nVirtAddr,pMemBlock->MemSubBlock[i].nAlignment);
			pMemBlock->MemSubBlock[i].pPhyAddr = (unsigned char *)Align(vpuMem.nPhyAddr,pMemBlock->MemSubBlock[i].nAlignment);
            pMemBlock->MemSubBlock[i].nFd = (int)vpuMem.nCpuAddr;
            pMemBlock->MemSubBlock[i].nSize = vpuMem.nSize; // update size because phys size is larger due to align with pagesize

            if (pMemBlock->MemSubBlock[i].pVirtAddr != (unsigned char *)vpuMem.nVirtAddr ||
                    pMemBlock->MemSubBlock[i].pPhyAddr != (unsigned char *)vpuMem.nPhyAddr)
                VPU_COMP_LOG("VPU_DecGetMem not aligned, nAlignment %d", pMemBlock->MemSubBlock[i].nAlignment);
		}
	}

	return 1;
}

status_t VpuWrapperDec::createFetchThread() {
    Mutex::Autolock autoLock(mLock);
    if (FETCH_STATE_NONE == mFetchState) {
        mFetchState = FETCH_STATE_START;

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        pthread_create(&mFetchThread, &attr, FetchThreadWrapper, this);
        pthread_attr_destroy(&attr);

        pFetchThreadMutex = malloc(sizeof(pthread_mutex_t));
        if (!pFetchThreadMutex) {
            ALOGE("malloc pFetchThreadMutex failed");
            return BAD_VALUE;
        }

        pthread_mutexattr_t attr1;
        pthread_mutexattr_init(&attr1);
        pthread_mutexattr_settype(&attr1,PTHREAD_MUTEX_NORMAL);
        pthread_mutex_init((pthread_mutex_t *)(pFetchThreadMutex), &attr1);
        pthread_cond_init(&pFetchThreadCond, NULL);
        pthread_mutexattr_destroy(&attr1);

        ALOGV("createFetchThread done");

    }
    return OK;
}

status_t VpuWrapperDec::destroyFetchThread() {
    if (FETCH_STATE_NONE != mFetchState) {
        mFetchState = FETCH_STATE_STOPPING;
        do {
            usleep(10);
        } while (FETCH_STATE_NONE != mFetchState);

        Mutex::Autolock autoLock(mLock);

        pthread_join(mFetchThread, NULL);

        if (pFetchThreadMutex) {
            free(pFetchThreadMutex);
            pFetchThreadMutex = nullptr;
        }

        pthread_cond_destroy(&pFetchThreadCond);
        ALOGV("destroyFetchThread done");
    }

    return OK;
}

void *VpuWrapperDec::FetchThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<VpuWrapperDec *>(me)->HandleFetchThread();
}

status_t VpuWrapperDec::HandleFetchThread() {
    do {
        if (nFetchBufferNum <= 0) {
            usleep(1000);
            continue;
        }

        GraphicBlockInfo *gbInfo = getFreeGraphicBlock();
        if (!gbInfo) {
            status_t ret = fetchOutputBuffer();
            if (WOULD_BLOCK == ret) {
                continue;
            } else if (OK != ret) {
                bReceiveError = true;
                break;
            } else
                gbInfo = getFreeGraphicBlock();
        }
        if (gbInfo) {
            status_t ret = setOutputBuffer(gbInfo->mBlockId);
            if (ret != OK) {
                // because hantro vpu set the max number of registered frame buffer,
                // if decoder fetch a new buffer but excceed the number, setOutputBuffer failed
                // decoder need to release this buffer and fetch again until fetch a previous
                // registered frame buffer.
                removeGraphicBlockById(gbInfo->mBlockId);
                usleep(5000);
                continue;
            } else {
                Mutex::Autolock autoLock(mLock);
                nFetchBufferNum--;
                pthread_cond_signal(&pFetchThreadCond);
            }
        }
    } while (mFetchState != FETCH_STATE_STOPPING);
    ALOGV("HandleFetchThread stopped");
    mFetchState = FETCH_STATE_NONE;
    return OK;
}

VpuWrapperDec::VpuWrapperDec(const char* mime)
    : mMime(mime) {
    // init mMime2TypeMap
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_AVC, VPU_V_AVC);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_HEVC, VPU_V_HEVC);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_VP8, VPU_V_VP8);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_VP9, VPU_V_VP9);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_MPEG2, VPU_V_MPEG2);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_MPEG4, VPU_V_MPEG4);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_H263, VPU_V_H263);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_XVID, VPU_V_XVID);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_MJPEG, VPU_V_MJPG);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_VC1, VPU_V_VC1);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_REAL, VPU_V_RV);

    SetDefaultSetting();
}

VpuWrapperDec::~VpuWrapperDec() {
}

void VpuWrapperDec::SetDefaultSetting() {
 	//clear internal variable 0
	memset(&sMemInfo,0,sizeof(VpuMemInfo));
	memset(&sInitInfo,0,sizeof(VpuDecInitInfo));
	memset(&sFramePoolInfo,0,sizeof(VpuDecoderFrmPoolInfo));

    int i;
    for(i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
        sFramePoolInfo.nFrm_bufferId[i] = -1;
        sFramePoolInfo.nFrm_ionFd[i] = -1;
    }

    mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    mOutputFormat.width = DEFAULT_FRM_WIDTH;
    mOutputFormat.height = DEFAULT_FRM_HEIGHT;
    mOutputFormat.bufferNum = DEFAULT_OUT_BUF_CNT;
    mOutputFormat.minBufferNum = DEFAULT_OUT_BUF_CNT;
    mOutputFormat.bufferSize = mOutputFormat.width * mOutputFormat.height * pxlfmt2bpp(mOutputFormat.pixelFormat) / 8;
    mOutputFormat.rect.left = 0;
    mOutputFormat.rect.top = 0;
    mOutputFormat.rect.right = mOutputFormat.width;
    mOutputFormat.rect.bottom = mOutputFormat.height;
    mOutputFormat.interlaced = false;

    nFreeOutBufferCnt = 0;
    nOwnedInputCnt = 0;

	nHandle = nullptr;
    hTsHandle = nullptr;
    bResyncTsm = true;

	eCodingFormat = VPU_V_AVC;

	nFrameWidthStride = -1;	//default: invalid
	nFrameHeightStride = -1;
	nFrameMaxCnt = -1;

	bInEos = false;

    nCurInputId = INVALID_ID;
    nSkippedInputId = INVALID_ID;

	eVpuDecoderState = VPU_COM_STATE_NONE;

	nCapability = 0;

	nMaxDurationMsThr = -1;
	nMaxBufCntThr = -1;

	bReorderDisabled = false;
    bHasCodecColorDesc = false;
    bHasHdr10StaticInfo = false;
    memset(&sDecoderColorDesc,0,sizeof(DecIsoColorAspects));
    memset(&sParserColorDesc,0,sizeof(DecIsoColorAspects));
    memset(&sHdr10StaticInfo,0,sizeof(DecStaticHDRInfo));

    nYOffset = 0;
    nUVOffset = 0;

    mFetchThread = 0;
    mFetchState = FETCH_STATE_NONE;
    pFetchThreadMutex = nullptr;
    nFetchBufferNum = 0;

	return;
}

void VpuWrapperDec::ResetDecoder() {
    VPU_COMP_API_LOG("%s: state: %d  \r\n", __FUNCTION__, eVpuDecoderState);

    bInEos = false;
    bReceiveError = false;
    nCurInputId = INVALID_ID;
    nSkippedInputId = INVALID_ID;
    nOwnedInputCnt = 0;

    tsmFlush(hTsHandle);
    bResyncTsm = true;

	//check state
    switch (eVpuDecoderState) {
        case VPU_COM_STATE_NONE:
        case VPU_COM_STATE_OPENED:
            FramePoolClear(&sFramePoolInfo);
            break;
        case VPU_COM_STATE_DO_DEC:
        case VPU_COM_STATE_EOS: {
            //flush vpu input/output
            FlushFilter();
            break;
    }
    default:
        //forbidden !!!
        VPU_COMP_ERR_LOG("%s: unknown state transition, current state=%d \r\n", __FUNCTION__, eVpuDecoderState);
    }

}

status_t VpuWrapperDec::ProcessVpuInitInfo() {
    VPU_COMP_API_LOG("%s line %d", __FUNCTION__, __LINE__);
	VpuDecRetCode ret;
	int nChanged=0;
    uint32_t padWidth, padHeight;

    eVpuDecoderState = VPU_COM_STATE_PROCESSING_INIT_INFO;

    //process init info
	ret = VPU_DecGetInitialInfo(nHandle, &sInitInfo);
	if (VPU_DEC_RET_SUCCESS != ret) {
		VPU_COMP_ERR_LOG("%s: vpu get init info failure: ret=0x%X \r\n", __FUNCTION__, ret);
		return BAD_VALUE;
	}

    padWidth = Align(sInitInfo.nPicWidth, FRAME_ALIGN_WIDTH);
    padHeight = Align(sInitInfo.nPicHeight , FRAME_ALIGN_HEIGHT);

    //check change for nFrameWidth/nFrameHeight
    if ((mOutputFormat.width != padWidth) || (mOutputFormat.height != padHeight)) {
        mOutputFormat.width = padWidth;
        mOutputFormat.height = padHeight;
        nChanged = 1;
    }

    //check color format, only for mjpg : 4:2:0/4:2:2(ver/hor)/4:4:4/4:0:0
    if (VPU_V_MJPG == eCodingFormat) {
        int pixelFormat;
        pixelFormat = ConvertMjpg2PixelFormat(sInitInfo.nMjpgSourceFormat, mOutputFormat.pixelFormat);

        if (pixelFormat == 0) {
            return BAD_TYPE;
        } else if(pixelFormat != mOutputFormat.pixelFormat) {
            mOutputFormat.pixelFormat = pixelFormat;
            nChanged = 1;
        }

    }

    if(mOutputFormat.pixelFormat != HAL_PIXEL_FORMAT_YCbCr_420_SP && mOutputFormat.pixelFormat != HAL_PIXEL_FORMAT_YCbCr_422_SP
        && mOutputFormat.pixelFormat != 0){
        if(IS_G2_DECODER){
            mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_NV12_G2_TILED_COMPRESSED;
        }else{
            mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_NV12_G1_TILED;
        }
    }
	//TODO: check change for sOutCrop ?
	//...

	//set crop info
	VPU_COMP_LOG("%s: original init info: [top,left,bottom,right]=[%d,%d,%d,%d], \r\n",__FUNCTION__,
		sInitInfo.PicCropRect.nTop, sInitInfo.PicCropRect.nLeft, sInitInfo.PicCropRect.nBottom, sInitInfo.PicCropRect.nRight);

    mOutputFormat.rect.left = sInitInfo.PicCropRect.nLeft;
    mOutputFormat.rect.top = sInitInfo.PicCropRect.nTop;
    mOutputFormat.rect.right = sInitInfo.PicCropRect.nRight;
    mOutputFormat.rect.bottom = sInitInfo.PicCropRect.nBottom;

    //check change for mOutputFormat.bufferNumDec
    if (sInitInfo.nMinFrameBufferCount + FRAME_SURPLUS != mOutputFormat.bufferNum) {
        mOutputFormat.bufferNum = sInitInfo.nMinFrameBufferCount + FRAME_SURPLUS;
    }

    mOutputFormat.minBufferNum = sInitInfo.nMinFrameBufferCount;

    // some SD streams need one more buffer to avoid getting a VPU_DEC_NO_ENOUGH_BUF from vpu wrapper(MA-11547, 11550, 11551)
    if(sInitInfo.nPicWidth <= 1920 && sInitInfo.nPicHeight <= 1088)
        mOutputFormat.bufferNum++;

    nFrameMaxCnt = (nFrameMaxCnt < mOutputFormat.bufferNum + FRAME_SURPLUS ?
                        nFrameMaxCnt : mOutputFormat.bufferNum + FRAME_SURPLUS);

    nChanged = 1;

    VPU_COMP_LOG("%s: Init OK, [width x height]=[%d x %d] \r\n", __FUNCTION__, sInitInfo.nPicWidth, sInitInfo.nPicHeight);
    VPU_COMP_LOG("%s: rect [left, right, top, bottom]=[%d, %d, %d, %d]\n", __FUNCTION__,
        mOutputFormat.rect.left, mOutputFormat.rect.right, mOutputFormat.rect.top, mOutputFormat.rect.bottom);
    VPU_COMP_LOG("mOutputFormat.bufferNumDec:%d ,nPadWidth: %d, nPadHeight: %d \r\n", mOutputFormat.bufferNum, mOutputFormat.width, mOutputFormat.height);

    if(sInitInfo.hasColorDesc){
        sDecoderColorDesc.colourPrimaries = sInitInfo.ColourDesc.colourPrimaries;
        sDecoderColorDesc.transferCharacteristics = sInitInfo.ColourDesc.transferCharacteristics;
        sDecoderColorDesc.matrixCoeffs = sInitInfo.ColourDesc.matrixCoeffs;
        sDecoderColorDesc.fullRange = sInitInfo.ColourDesc.fullRange;
        bHasCodecColorDesc = true;

        VPU_COMP_INFO("hasColorDesc, colourPrimaries %d transferCharacteristics %d matrixCoeffs %d fullRange %d",
            sDecoderColorDesc.colourPrimaries, sDecoderColorDesc.transferCharacteristics,
            sDecoderColorDesc.matrixCoeffs, sDecoderColorDesc.fullRange);
    }

    if(sInitInfo.hasHdr10Meta) {
        bHasHdr10StaticInfo = true;
        sHdr10StaticInfo.mR[0] = (uint16_t)sInitInfo.Hdr10Meta.redPrimary[0];
        sHdr10StaticInfo.mR[1] = (uint16_t)sInitInfo.Hdr10Meta.redPrimary[1];
        sHdr10StaticInfo.mG[0] = (uint16_t)sInitInfo.Hdr10Meta.greenPrimary[0];
        sHdr10StaticInfo.mG[1] = (uint16_t)sInitInfo.Hdr10Meta.greenPrimary[1];
        sHdr10StaticInfo.mB[0] = (uint16_t)sInitInfo.Hdr10Meta.bluePrimary[0];
        sHdr10StaticInfo.mB[1] = (uint16_t)sInitInfo.Hdr10Meta.bluePrimary[1];
        sHdr10StaticInfo.mW[0] = (uint16_t)sInitInfo.Hdr10Meta.whitePoint[0];
        sHdr10StaticInfo.mW[1] = (uint16_t)sInitInfo.Hdr10Meta.whitePoint[1];
        sHdr10StaticInfo.mMaxDisplayLuminance = (uint16_t)(sInitInfo.Hdr10Meta.maxMasteringLuminance/10000);
        sHdr10StaticInfo.mMinDisplayLuminance = (uint16_t)sInitInfo.Hdr10Meta.minMasteringLuminance;
        sHdr10StaticInfo.mMaxContentLightLevel = (uint16_t)sInitInfo.Hdr10Meta.maxContentLightLevel;
        sHdr10StaticInfo.mMaxFrameAverageLightLevel = (uint16_t)sInitInfo.Hdr10Meta.maxFrameAverageLightLevel;
    }

    if (sInitInfo.nBitDepth == 10) {
        switch (mOutputFormat.pixelFormat) {
            case HAL_PIXEL_FORMAT_YCbCr_420_SP:
                mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_P010;
                break;
            case HAL_PIXEL_FORMAT_NV12_G1_TILED:
                mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_P010_TILED;
                break;
            case HAL_PIXEL_FORMAT_NV12_G2_TILED_COMPRESSED:
                mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED;
                break;
            default:
                VPU_COMP_ERR_LOG("unknown color format for HDR10: 0x%x", mOutputFormat.pixelFormat);
        }
    }

    VPU_COMP_INFO("Vpu InitInfo hasHdr10Meta %s, hasColorDesc %s, nBitDepth %d, eColorFormat 0x%x, minBufferCnt %d, nFrameSize %d",
        sInitInfo.hasHdr10Meta?"yes":"no", sInitInfo.hasColorDesc?"yes":"no" ,sInitInfo.nBitDepth,
        mOutputFormat.pixelFormat, sInitInfo.nMinFrameBufferCount, sInitInfo.nFrameSize);

	if (nChanged) {
	    VPU_COMP_LOG("out format change width=%d,height=%d", mOutputFormat.width, mOutputFormat.height);
		mOutputFormat.bufferSize = mOutputFormat.width * mOutputFormat.height * pxlfmt2bpp(mOutputFormat.pixelFormat) / 8;
        if(mOutputFormat.bufferSize < (uint32_t)sInitInfo.nFrameSize)
            mOutputFormat.bufferSize = sInitInfo.nFrameSize;
        //sOutFmt.nStride = sInitInfo.nBitDepth == 10 ? mOutputFormat.width * 5 / 4 : mOutputFormat.width;
        //sOutFmt.nSliceHeight = mOutputFormat.height;
        if (FETCH_STATE_START == mFetchState) {
            destroyFetchThread();
            createFetchThread();

            Mutex::Autolock autoLock(mLock);
            nFetchBufferNum = 0;
        }
        FramePoolClear(&sFramePoolInfo);
        //update state
	    eVpuDecoderState = VPU_COM_STATE_WAIT_FRM;
        if ( OK != outputFormatChanged())
            return BAD_VALUE;
    }

	return OK;
}

status_t VpuWrapperDec::onInit() {
    VPU_COMP_API_LOG("%s line %d", __FUNCTION__, __LINE__);
    VpuDecRetCode ret;

    hTsHandle = tsmCreate();
    if (hTsHandle == NULL) {
        VPU_COMP_ERR_LOG("Create Ts manager failed.\n");
        return BAD_VALUE;
    }

    auto node = mMime2TypeMap.find(mMime);
    if (node == mMime2TypeMap.end()) {
        ALOGE("%s line %d, unsupported decoder mime %s\n", __FUNCTION__, __LINE__, mMime);
        return BAD_VALUE;
    } else
        eCodingFormat = node->second;

    if (eCodingFormat == VPU_V_VC1 || eCodingFormat == VPU_V_VC1_AP)
        nFrameMaxCnt = MAX_VC1_FRAME_NUM;
    else if (eCodingFormat == VPU_V_RV)
        nFrameMaxCnt = MAX_RV_FRAME_NUM;
    else
        nFrameMaxCnt = MAX_FRAME_NUM;

    ret = VPU_DecLoad();
    CHECK_VPU_RET(ret);

    ret = VPU_DecGetVersionInfo(&sVpuVer);
    CHECK_VPU_RET(ret);

	eVpuDecoderState = VPU_COM_STATE_LOADED;
    return OK;
}

status_t VpuWrapperDec::onStart() {
    VPU_COMP_API_LOG("%s line %d", __FUNCTION__, __LINE__);
    CHECK_DEC_STATE(VPU_COM_STATE_LOADED);

    VpuDecRetCode ret;

    // workaround for MA-17234: CTS read framebuffer too often lead to buffer pool time out.
    // need to allocate framebuffer as cacheable for these videos.
    if ((eCodingFormat == VPU_V_AVC || eCodingFormat == VPU_V_VP8 || eCodingFormat == VPU_V_HEVC) &&
        (mInputFormat.width == 1920 && mInputFormat.height == 1080)) {
        nOutBufferUsage = (uint64_t)(C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE | GRALLOC_USAGE_PRIVATE_2);
    }

    ret = VPU_DecQueryMem(&sMemInfo);
    CHECK_VPU_RET(ret);

    if (0 == MemMallocBlock(&sMemInfo)) {
		VPU_COMP_ERR_LOG("%s line %d: malloc memory failure\n",__FUNCTION__, __LINE__);
	    return BAD_VALUE;
    }

    if (OK != OpenVpu() || nHandle == nullptr) {
		VPU_COMP_ERR_LOG("%s: open vpu failure\n",__FUNCTION__);
		return BAD_VALUE;
	}

	//check capability
	int capability = 0;
	ret = VPU_DecGetCapability(nHandle, VPU_DEC_CAP_FRAMESIZE, &capability);
	if ((ret == VPU_DEC_RET_SUCCESS) && capability) {
		nCapability |= VPU_COM_CAPABILITY_FRMSIZE;
	}

    eVpuDecoderState = VPU_COM_STATE_OPENED;
    return OK;
}

status_t VpuWrapperDec::onStop() {
    VPU_COMP_API_LOG("%s: \r\n",__FUNCTION__);

    //check state
    switch(eVpuDecoderState) {
        case VPU_COM_STATE_NONE:
            // fall through
        case VPU_COM_STATE_LOADED:
            // decoder is already stopped
            return OK;
        default:
            break;
    }

	ResetDecoder();
    ReleaseVpuSource();

    FramePoolClear(&sFramePoolInfo);
    freeOutputBuffers();
    nFreeOutBufferCnt = 0;
    nFetchBufferNum = 0;
    nOwnedInputCnt = 0;

    eVpuDecoderState = VPU_COM_STATE_LOADED;

    return OK;
}

status_t VpuWrapperDec::onFlush() {
    VPU_COMP_API_LOG("%s: \r\n",__FUNCTION__);
    ResetDecoder();
    NotifyFlushDone();

    return OK;
}

status_t VpuWrapperDec::onDestroy() {
    status_t err = OK;
	VPU_COMP_API_LOG("%s: \r\n",__FUNCTION__);

	//check state
    switch(eVpuDecoderState) {
        case VPU_COM_STATE_NONE:
            //forbidden
            VPU_COMP_ERR_LOG("%s: failure: error state transition, current state=%d \r\n",__FUNCTION__,eVpuDecoderState);
            return BAD_VALUE;
        case VPU_COM_STATE_LOADED:
            break;
        default:
            err = ReleaseVpuSource();
        break;
	}

    if (hTsHandle)
        tsmDestroy(hTsHandle);

    //unload
    VpuDecRetCode ret = VPU_DecUnLoad();
    if (ret != VPU_DEC_RET_SUCCESS) {
        VPU_COMP_ERR_LOG("%s: vpu unload failure: ret=0x%X \r\n",__FUNCTION__,ret);
        err = BAD_VALUE;
    }

    eVpuDecoderState = VPU_COM_STATE_NONE;
    return err;
}

status_t VpuWrapperDec::decodeInternal(std::unique_ptr<IMXInputBuffer> input) {
	VpuDecRetCode ret;
	VpuBufferNode InData;
	int bufRetCode;
	uint8_t* pBitstream;
	int32_t readbytes;
	int32_t enableFileMode=0;
	int32_t capability=0;
    int32_t nSecureBufferAllocSize = 0;
    status_t err = OK;
    uint32_t loopCnt = 0;

	VPU_COMP_API_LOG("%s: state: %d, InBuf: %p, data size: %d, bInEos: %d\n", \
        __FUNCTION__, eVpuDecoderState, input->pInBuffer, input->size, bInEos);

    if (nSkippedInputId != INVALID_ID) {
        NotifySkipInputBuffer(nSkippedInputId);
        nSkippedInputId = INVALID_ID;
    }

decode_one_frame:

    if (bReceiveError) {
        VPU_COMP_ERR_LOG("bReceiveError true, notify error and return\n");
        err = UNKNOWN_ERROR;
        NotifyError(err);
        return err;
    }

    bufRetCode = 0;

	switch (eVpuDecoderState) {
        case VPU_COM_STATE_LOADED:
            if (OK != onStart())
                return BAD_VALUE;
            // fall through
        case VPU_COM_STATE_OPENED:
            // go to decode to get init info from vpu
            break;
		case VPU_COM_STATE_WAIT_FRM: {
            if (OK != DecoderRegisterAllFrames()) {
                return BAD_VALUE;
            }
            if (FramePoolBufNum(&sFramePoolInfo) >= sInitInfo.nMinFrameBufferCount) {
				eVpuDecoderState = VPU_COM_STATE_DO_DEC;
			}

            if (FETCH_STATE_NONE == mFetchState) {
                createFetchThread();
            }
            break;
        }
        case VPU_COM_STATE_RE_WAIT_FRM: {
            // make sure free buffer cnt is enough after flush
            if (nFreeOutBufferCnt >= sInitInfo.nMinFrameBufferCount) {
                eVpuDecoderState = VPU_COM_STATE_DO_DEC;
            } else {
                // need more output buffer
            }
            //fall through
        }
		case VPU_COM_STATE_DO_DEC:
			break;
		//forbidden state & unknow state
		default:
			VPU_COMP_ERR_LOG("%s: failure state transition, current state=%d \r\n", __FUNCTION__, eVpuDecoderState);
			return INVALID_OPERATION;
	}

	//for all codecs
	pBitstream = (input->pInBuffer == nullptr) ? NULL : (uint8_t*)input->pInBuffer;
	readbytes = input->size;

    bInEos = input->eos;

    if (nCurInputId == INVALID_ID && readbytes > 0) {
        if (bResyncTsm) {
            tsmReSync(hTsHandle, input->timestamp, MODE_AI);
            bResyncTsm = false;
        }
        tsmSetBlkTs(hTsHandle, readbytes, input->timestamp);
        nCurInputId = input->id;
        nOwnedInputCnt++;
    }

	//check eos or null data
	if(pBitstream == NULL && bInEos == true) {
	    //create and send EOS data (with length=0)
        pBitstream = (uint8_t*)0x01;
		readbytes = 0;
	}

	//EOS: 		0==readbytesp && Bitstream!=NULL
	//non-EOS: 	0==readbytesp && Bitstream==NULL

	//seq init
	//decode bitstream buf
	VPU_COMP_LOG("%s: pBitstream=%p, readbytes=%d  \r\n",__FUNCTION__, pBitstream, readbytes);
    memset(&InData, 0, sizeof(VpuBufferNode));
	InData.nSize = readbytes;
	InData.pPhyAddr = NULL;
	InData.pVirAddr = pBitstream;

    if (pCodecDataBuf && nCodecDataLen > 0) {
	    InData.sCodecData.pData = pCodecDataBuf;
	    InData.sCodecData.nSize = nCodecDataLen;
    }

    // MA-17685: don't send codecdata again for vc1, otherwise it will cause mosaic after seek
    if (pCodecDataBuf && nCodecDataLen > 0 && !(eCodingFormat == VPU_V_VC1 || eCodingFormat == VPU_V_VC1_AP)) {
        int reset = 0;
        VPU_DecConfig(nHandle, VPU_DEC_CONF_RESET_CODECDATA, &reset);
    }
#if 0
    if(bEnableAndroidNativeHandleBuffer && !bInEos && readbytes > 0) {
        int fd = (intptr_t)pBitstream;
        GetHwBuffer((OMX_PTR)fd,(OMX_PTR *)&InData.pPhyAddr);
        #ifdef HANTRO_VPU
        int32_t fd2;
        OMX_PTR dataBuf = NULL;
        GetFdAndAddr((OMX_PTR)fd,&fd2,(OMX_PTR*)&dataBuf);
        InData.pVirAddr = (unsigned char *)dataBuf;
        VPU_COMP_LOG("fd=%d,phyaddr=%p,virt=%p",fd,InData.pPhyAddr,InData.pVirAddr);
        #endif

        #if 0//for debug purpose
        int *buf = (int*)mmap(0, readbytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        InData.pVirAddr = (unsigned char *)buf;
        #endif
    }
#endif

	ret = VPU_DecDecodeBuf(nHandle, &InData, (int32_t*)&bufRetCode);
	if(VPU_DEC_RET_SUCCESS != ret) {
		VPU_COMP_ERR_LOG("%s: vpu dec buf failure: ret=0x%X \r\n",__FUNCTION__,ret);
		if(VPU_DEC_RET_FAILURE_TIMEOUT == ret) {
			VPU_DecReset(nHandle);
			NotifyError(TIMED_OUT);
		}
        NotifyError(UNKNOWN_ERROR);
        //TODO: need remove below line
        return OK;
		//return UNKNOWN_ERROR;
	}

    VPU_COMP_LOG("%s: bufRetCode: 0x%X  \r\n", __FUNCTION__, bufRetCode);

    int decodeAgain = 0;
    err = CheckVpuReturnCode(bufRetCode, &decodeAgain);

    if (err == OK && decodeAgain) {
        if (loopCnt++ > 50) {
            VPU_COMP_ERR_LOG("it seems like a dead loop, quit now \n");
            return OK;
        }
        goto decode_one_frame;
    }

    if (pCodecDataBuf && nCodecDataLen > 0) {
        nCodecDataLen = 0; // codecdata is handled, reset data length here.
        bCodecDataQueued = true;
    }

    if (err) {
        NotifyError(err);
    }

    //TODO: need remove below line
    err = OK;

    return err;
}

status_t VpuWrapperDec::setOutputBuffer(int32_t bufferId) {
    VpuDecRetCode ret;
    VpuDecOutFrameInfo * pFrameInfo;
    int32_t exist;
    int32_t index;

    GraphicBlockInfo* info = getGraphicBlockById(bufferId);
    if (info == nullptr) {
        VPU_COMP_ERR_LOG("%s: failure: unvalid buffer id: %d\n", __FUNCTION__, bufferId);
	    return BAD_VALUE;
    }

    exist = FramePoolBufExist(bufferId, &sFramePoolInfo, &index);

    VPU_COMP_LOG("%s, buffer id %d, exist %d, index %d", __FUNCTION__, bufferId, exist, index);

    if (exist < 0) {
		VPU_COMP_ERR_LOG("%s: failure: unvalid buffer id: %d\n", __FUNCTION__, bufferId);
		return BAD_VALUE;
	} else if (exist == 0) {
	    // check if current out frame number reach the max value
	    if (FramePoolBufNum(&sFramePoolInfo) >= nFrameMaxCnt)
            return BAD_VALUE;
        // can't register buffer just when res changed, as vpu will memset this buffer based on framesize
        // if framesize increased, memset crash because this buffer size is less than new framesize.
        else if (eVpuDecoderState == VPU_COM_STATE_PROCESSING_INIT_INFO)
            return BAD_VALUE;
		//register frame buffer
        if (eVpuDecoderState == VPU_COM_STATE_DO_DEC) {
            Mutex::Autolock autoLock(mLock);
            status_t err = DecoderRegisterOneFrame(info);
            if (err != OK) {
                VPU_COMP_ERR_LOG("%s: failure: can't register buffer id: %d\n", __FUNCTION__, bufferId);
                return BAD_VALUE;
            }
            VPU_COMP_LOG("return register frame, free buffers: %d", nFreeOutBufferCnt);
        }
        else{
            VPU_COMP_LOG("setOutputBuffer incorrect state %d, don't register", eVpuDecoderState);
            return BAD_VALUE;
        }
    } else {
		VpuDecoderFrmState eState;
		FramePoolGetBufProperty(&sFramePoolInfo, index, &eState, &pFrameInfo);
        VPU_COMP_LOG("set_output_buffer FramePoolGetBufProperty, state = %d\n", eState);

		if (eState == VPU_COM_FRM_STATE_OUT) {
			if (NULL != pFrameInfo->pDisplayFrameBuf && eVpuDecoderState != VPU_COM_STATE_PROCESSING_INIT_INFO) {
				//clear displayed frame
				Mutex::Autolock autoLock(mLock);
		        ret = VPU_DecOutFrameDisplayed(nHandle, sFramePoolInfo.outFrameInfo[index].pDisplayFrameBuf);
				if(VPU_DEC_RET_SUCCESS != ret) {
					VPU_COMP_ERR_LOG("%s: vpu clear frame display failure: ret=0x%X\n", __FUNCTION__, ret);
					return BAD_VALUE;
				}
                nFreeOutBufferCnt++;
                VPU_COMP_LOG("return out frame, free buffers: %d", nFreeOutBufferCnt);
			} else {
				//this is fake output frame which is stolen at some special steps, include EOS, flush,...
				VPU_COMP_LOG("application return one stolen buffer id: %d \n", bufferId);
			}
			//update buffer state
			FramePoolSetBufState(&sFramePoolInfo, index, VPU_COM_FRM_STATE_FREE);
            info->mState = GraphicBlockInfo::State::OWNED_BY_VPU;
		}
		else
		{
			VPU_COMP_ERR_LOG("%s: failure: repeat setting output buffer id: %d \r\n", __FUNCTION__, bufferId);
			return BAD_VALUE;
		}
    }
    return OK;
}

status_t VpuWrapperDec::DoSetConfig(DecConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    status_t ret = OK;

    switch (index) {
        case DEC_CONFIG_VC1_SUB_FORMAT: {
            if (strcmp(mMime, MEDIA_MIMETYPE_VIDEO_VC1) != 0) {
                VPU_COMP_ERR_LOG("DoSetConfig DEC_CONFIG_VC1_SUB_FORMAT only support for VC1");
                return BAD_VALUE;
            }

            int* format = (int*)pConfig;
            // TODO: remove this OMX_VIDEO_WMVFormat9=0x08, OMX_VIDEO_WMVFormatWVC1=0x7f000002
            if (*format == 0x08)
                eCodingFormat = VPU_V_VC1;
            else if (*format == 0x7f000002)
                eCodingFormat = VPU_V_VC1_AP;

            VPU_COMP_LOG("vc1 sub-format 0x%x eCodingFormat %d", *format, eCodingFormat);
        }
        default:
            ret = BAD_VALUE;
            break;
    }
    return ret;
}

status_t VpuWrapperDec::DoGetConfig(DecConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    status_t ret = OK;

    switch (index) {
        case DEC_CONFIG_OUTPUT_DELAY: {
            int *pOutputDelayValue = (int*)pConfig;
            if (eCodingFormat == VPU_V_VC1 || eCodingFormat == VPU_V_VC1_AP)
                *pOutputDelayValue = 8;
            else if (eCodingFormat == VPU_V_RV)
                *pOutputDelayValue = 8;
            else
                *pOutputDelayValue = 16;
            break;
        }
        case DEC_CONFIG_HDR10_STATIC_INFO: {
            if (bHasHdr10StaticInfo | bHasCodecColorDesc)
                memcpy(pConfig, &sHdr10StaticInfo, sizeof(DecStaticHDRInfo));
            else
                ret = BAD_VALUE;
            break;
        }
        case DEC_CONFIG_COLOR_ASPECTS: {
            if (bHasCodecColorDesc)
                memcpy(pConfig, &sDecoderColorDesc, sizeof(DecIsoColorAspects));
            else
                ret = BAD_VALUE;
            break;
        }
        default:
            ret = BAD_VALUE;
            break;
    }

    return ret;
}

status_t VpuWrapperDec::allocateOutputBuffers() {
    status_t ret = OK;
    int32_t i;

    VPU_COMP_LOG("%s, line %d, try to allocate %d buffers \n", __FUNCTION__, __LINE__, mOutputFormat.bufferNum);

    for (i = 0; i < mOutputFormat.bufferNum; i++) {
        do {
            ret = fetchOutputBuffer();
        } while (WOULD_BLOCK == ret);

        if (ret != OK)
            break;
    }

    return ret;
}

status_t VpuWrapperDec::freeOutputBuffers() {
    VPU_COMP_LOG("%s, line %d, mGraphicBlocks size=%d", __FUNCTION__, __LINE__, (int)mGraphicBlocks.size());
    {
        Mutex::Autolock autoLock(mLock);
        nFetchBufferNum = 0;
    }

    for (auto& info : mGraphicBlocks) {
        if (info.mVirtAddr > 0 && info.mCapacity > 0)
            munmap((void*)info.mVirtAddr, info.mCapacity);

        info.mGraphicBlock.reset();
    }

    mGraphicBlocks.clear();

    return OK;
}

bool VpuWrapperDec::OutputBufferFull() {
    return (FramePoolBufNum(&sFramePoolInfo) >= nFrameMaxCnt);
}

bool VpuWrapperDec::checkIfPostProcessNeeded() {
    return false;
}

status_t VpuWrapperDec::FlushFilter()
{
	VpuDecRetCode ret;
	VPU_COMP_LOG("%s: \r\n",__FUNCTION__);

    Mutex::Autolock autoLock(mLock);

	ret = VPU_DecFlushAll(nHandle);
	if (VPU_DEC_RET_SUCCESS != ret) {
		VPU_COMP_ERR_LOG("%s: vpu flush failure: ret=0x%X \r\n",__FUNCTION__,ret);
		if (VPU_DEC_RET_FAILURE_TIMEOUT == ret) {
			VPU_DecReset(nHandle);
		}
		return BAD_VALUE;
	}

	//since vpu will auto clear all buffers(is equal to setoutput() operation), we need to add additional protection(set VPU_COM_STATE_WAIT_FRM).
	//otherwise, vpu may return one buffer which is still not set by user.
	eVpuDecoderState = VPU_COM_STATE_RE_WAIT_FRM;

	return OK;
}

status_t VpuWrapperDec::ReleaseVpuSource() {
    VpuDecRetCode ret;
    status_t err;

    if (mFetchThread) {
        destroyFetchThread();
    }

    //close vpu
    if (nHandle) {
        ret = VPU_DecClose(nHandle);
        if (ret != VPU_DEC_RET_SUCCESS) {
            VPU_COMP_ERR_LOG("%s: vpu close failure: ret=0x%X \r\n",__FUNCTION__,ret);
            err = BAD_VALUE;
        }
    }

    //release mem
    if (0 == MemFreeBlock(&sMemInfo)) {
        VPU_COMP_ERR_LOG("%s: free memory failure !  \r\n",__FUNCTION__);
        err = BAD_VALUE;
    }

    return err;
}

status_t VpuWrapperDec::CheckVpuReturnCode(int32_t bufRetCode, int* decodeAgain) {
    status_t err = OK;

    //check input buff
	if (bufRetCode & VPU_DEC_INPUT_USED) {
		if(nCurInputId != INVALID_ID) {
            NotifyInputBufferUsed(nCurInputId/*input_id*/);
            if (bufRetCode & VPU_DEC_SKIP) {
                nSkippedInputId = nCurInputId;
            }
            nCurInputId = INVALID_ID;
            if (nOwnedInputCnt > mOutputFormat.bufferNum) {
                nOwnedInputCnt--;
                NotifySkipInputBuffer(INVALID_ID);
            }
		}
        if (bInEos && !(bufRetCode & VPU_DEC_OUTPUT_EOS)) {
            // vpu just consumed the last input, need call VPU_DecDecodeBuf again to trigger eos.
            *decodeAgain = 1;
        }
    } else {
	    *decodeAgain = 1;
    }

	//check init info
	if (bufRetCode & VPU_DEC_INIT_OK) {
		err = ProcessVpuInitInfo();
        if ((eCodingFormat == VPU_V_AVC || eCodingFormat == VPU_V_VP9 || eCodingFormat == VPU_V_HEVC) && \
                mOutputFormat.pixelFormat != HAL_PIXEL_FORMAT_YCbCr_420_SP) {
            VpuBufferNode input;
            memset(&input, 0, sizeof(VpuBufferNode));
            //do not care about the result and bufRetCode
            (void)VPU_DecDecodeBuf(nHandle, &input, (int32_t*)&bufRetCode);
        }
		return err;
	}

	//check resolution change
	if (bufRetCode & VPU_DEC_RESOLUTION_CHANGED) {
		/*in such case:
		   (1) the frames which haven't been output (in vpu or post-process) will be discard, So, video may be not complete continuous.
		   (2) user need to re-allocate buffers, so buffer state will be meaningless.
		   (3) the several frames dropped may affect timestamp if user don't use decoded handle to map frames between decoded and display ?
		 */
		VPU_COMP_LOG("resolution changed \r\n");

		//get new init info to re-set some variables
		err = ProcessVpuInitInfo();
		return err;
	}

	//check decoded info
	if ((nCapability & VPU_COM_CAPABILITY_FRMSIZE) && (bufRetCode & VPU_DEC_ONE_FRM_CONSUMED)) {
		VPU_COMP_LOG("one frame is decoded \r\n");
        VpuDecFrameLengthInfo sLengthInfo;
	    VpuDecRetCode ret;
	    ret = VPU_DecGetConsumedFrameInfo(nHandle, &sLengthInfo);
        if (VPU_DEC_RET_SUCCESS == ret) {
		    tsmSetFrmBoundary(hTsHandle, sLengthInfo.nStuffLength, sLengthInfo.nFrameLength, sLengthInfo.pFrame);
	    }
	}

    //check output buff
    if (bufRetCode & VPU_DEC_OUTPUT_DIS) {
        eVpuDecoderState = VPU_COM_STATE_DO_OUT;
        GetOutputBuffer();
        {
            Mutex::Autolock autoLock(mLock);
            nFetchBufferNum++;
        }
    } else if (bufRetCode & VPU_DEC_OUTPUT_EOS) {
        VPU_COMP_INFO("vpu return output eos\n");
        *decodeAgain = 0;
        eVpuDecoderState = VPU_COM_STATE_EOS;
        NotifyEOS();
    }

    //check "no enough buf"
    if ((bufRetCode & VPU_DEC_NO_ENOUGH_BUF) && (*decodeAgain == 1)) {
        #if 0
        int i = 0;
        for(i = 0; i < VPU_DEC_MAX_NUM_MEM; i++) {
            ALOGI("nFrm[%d]: bufferId %d, phy %p, state %d",
                i, sFramePoolInfo.nFrm_bufferId[i], (void*)sFramePoolInfo.nFrm_phyAddr[i], sFramePoolInfo.eFrmState[i]);
        }
        #endif

        struct timeval now;
        struct timespec outtime;
        int wait_ret;

        pthread_mutex_lock((pthread_mutex_t *)pFetchThreadMutex);
        gettimeofday(&now, NULL);
        outtime.tv_sec = now.tv_sec + 1;
        outtime.tv_nsec = now.tv_usec * 1000;
        wait_ret = pthread_cond_timedwait(&pFetchThreadCond, (pthread_mutex_t *)pFetchThreadMutex, &outtime);
        pthread_mutex_unlock((pthread_mutex_t *)pFetchThreadMutex);

        if (wait_ret) {
            ALOGW("pthread_cond_timedwait timeout after 1s, ret=%d", wait_ret);
        }
        VPU_COMP_LOG("free buffers: %d, request min buffers: %d", nFreeOutBufferCnt, sInitInfo.nMinFrameBufferCount);
    }

    if ((bufRetCode & VPU_DEC_NO_ENOUGH_INBUF) && (false == bInEos)) {
        // do nothing because input buffer will arrive to vpu in next processing.
    }

    return err;

}

status_t VpuWrapperDec::GetOutputBuffer() {
    VpuDecRetCode ret;
    bool bOutLast = false;
	int32_t bufferId = -1;
    int32_t index;
    uint64_t timestamp = -1;

	VPU_COMP_API_LOG("%s: state: %d \r\n", __FUNCTION__, eVpuDecoderState);

	//check state
	switch(eVpuDecoderState) {
		case VPU_COM_STATE_DO_OUT:
			//update state
			eVpuDecoderState = VPU_COM_STATE_DO_DEC;
			break;
		default:
			//forbidden
			VPU_COMP_ERR_LOG("%s: failure state transition, current state=%d \r\n",__FUNCTION__,eVpuDecoderState);
			return BAD_VALUE;
	}

	//get output frame

	VpuDecOutFrameInfo sFrameInfo;

	ret = VPU_DecGetOutputFrame(nHandle, &sFrameInfo);
	if (VPU_DEC_RET_SUCCESS != ret) {
		VPU_COMP_ERR_LOG("%s: vpu get output frame failure: ret=0x%X \r\n", __FUNCTION__, ret);
		return BAD_VALUE;
	}

    bufferId = sFrameInfo.pDisplayFrameBuf->nBufferId;

	//find the matched node in frame pool based on frame virtual address, and record output frame info
	index = FramePoolRecordOutFrame(bufferId, &sFramePoolInfo, &sFrameInfo);
	if(-1 == index) {
		VPU_COMP_ERR_LOG("%s: can't find matched node in frame pool: buffer id = %d !!!\n",__FUNCTION__, bufferId);
		return BAD_VALUE;
	}

    timestamp = tsmGetFrmTs(hTsHandle, NULL);

	//update crop info for every output frame
    mOutputFormat.rect.left = sFrameInfo.pExtInfo->FrmCropRect.nLeft;
    mOutputFormat.rect.right = sFrameInfo.pExtInfo->FrmCropRect.nRight;
    mOutputFormat.rect.top = sFrameInfo.pExtInfo->FrmCropRect.nTop;
    mOutputFormat.rect.bottom = sFrameInfo.pExtInfo->FrmCropRect.nBottom;

    nYOffset = sFrameInfo.pExtInfo->rfc_luma_offset;
    nUVOffset = sFrameInfo.pExtInfo->rfc_chroma_offset;

    FramePoolSetBufState(&sFramePoolInfo, index, VPU_COM_FRM_STATE_OUT);

    nFreeOutBufferCnt--;
    nOwnedInputCnt--;

    NotifyPictureReady(bufferId, timestamp);

    return OK;
}

status_t VpuWrapperDec::OpenVpu() {
	VpuDecOpenParam decOpenParam;
	VpuDecRetCode ret;
	int32_t para;

    VPU_COMP_LOG("%s: codec format: %d, %d/%d, pixelFormat 0x%x, mime %s\n",
        __FUNCTION__, eCodingFormat, mOutputFormat.width, mOutputFormat.height, mOutputFormat.pixelFormat, mMime);

	memset(&decOpenParam, 0, sizeof(VpuDecOpenParam));
	//set open params
	decOpenParam.CodecFormat = eCodingFormat;

#ifdef SUPPORT_VIDEO_10BIT
    decOpenParam.nEnableVideoCompressor = 1;
#else
    // 8mq/8mp do 10bit to 8bit convert as default
    decOpenParam.nPixelFormat = 1;
#endif

	//FIXME: for MJPG, we need to add check for 4:4:4/4:2:2 ver/4:0:0  !!!
	if (HAL_PIXEL_FORMAT_YCbCr_420_SP == mOutputFormat.pixelFormat ||
            HAL_PIXEL_FORMAT_YCbCr_422_SP == mOutputFormat.pixelFormat) {
		decOpenParam.nChromaInterleave = 1;
	} else {
		decOpenParam.nChromaInterleave = 0;
	}

    if(mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_NV12_G1_TILED ||
            mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_NV12_G2_TILED ||
            mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_NV12_G2_TILED_COMPRESSED ||
            mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_P010_TILED ||
            mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED) {
        decOpenParam.nTiled2LinearEnable = true;
        VPU_COMP_INFO("enable tile format");
    }

	//for special formats, such as package VC1 header,...
	decOpenParam.nPicWidth = mOutputFormat.width;
	decOpenParam.nPicHeight = mOutputFormat.height;
    decOpenParam.nAdaptiveMode = (bAdaptiveMode == true) ? 1 : 0;
    decOpenParam.nSecureMode = (bSecureMode == true) ? 1 : 0;
    decOpenParam.nReorderEnable = 1;
    //decOpenParam.nSecureBufferAllocSize = (bSecureMode == true) ? nSecureBufferAllocSize : 0;

	//open vpu
	ret = VPU_DecOpen(&nHandle, &decOpenParam, &sMemInfo);
	if (ret != VPU_DEC_RET_SUCCESS) {
		VPU_COMP_ERR_LOG("%s: vpu open failure: ret=0x%X !\n",__FUNCTION__,ret);
		return BAD_VALUE;
	}

	//set default config
	para = VPU_DEC_SKIPNONE;
	ret = VPU_DecConfig(nHandle, VPU_DEC_CONF_SKIPMODE, &para);
	if (VPU_DEC_RET_SUCCESS != ret) {
		VPU_COMP_ERR_LOG("%s: vpu config failure: config=0x%X, ret=%d \n",__FUNCTION__,(uint32_t)VPU_DEC_CONF_SKIPMODE,ret);
		VPU_DecClose(nHandle);
		return BAD_VALUE;
	}
	para = DEFAULT_BUF_DELAY;
	ret = VPU_DecConfig(nHandle, VPU_DEC_CONF_BUFDELAY, &para);
	if (VPU_DEC_RET_SUCCESS != ret) {
		VPU_COMP_ERR_LOG("%s: vpu config failure: config=0x%X, ret=%d \n",__FUNCTION__,(uint32_t)VPU_DEC_CONF_SKIPMODE,ret);
		VPU_DecClose(nHandle);
		return BAD_VALUE;
	}

	return OK;
}

status_t VpuWrapperDec::CreateOneRegisterFrameBuffer(GraphicBlockInfo* info, VpuFrameBuffer * pFrameBuf) {
    status_t err = OK;
    int32_t frameNum;
    int ionFd = -1;
    bool cacheable = (nOutBufferUsage & (C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE));

    memset(pFrameBuf, 0, sizeof(VpuFrameBuffer));

    // if framebuffer is cacheable, need to register ion fd to vpu to flush.
    if (cacheable)
        ionFd = dup(info->mDMABufFd);

    // register output frame buffer to frame pool/vpu
    frameNum = FramePoolRegisterBuf(info->mVirtAddr, info->mPhysAddr, ionFd, info->mBlockId, &sFramePoolInfo);
    if (frameNum == -1) {
        VPU_COMP_ERR_LOG("%s: register frame failure: frame pool is full ! \r\n", __FUNCTION__);
        return BAD_VALUE;
    }

    pFrameBuf->pbufY = (unsigned char*)info->mPhysAddr;
	pFrameBuf->pbufVirtY = (unsigned char*)info->mVirtAddr;
    pFrameBuf->nBufferId = info->mBlockId;
    pFrameBuf->nIonFd = ionFd;

    info->mState = GraphicBlockInfo::State::OWNED_BY_VPU;

    return OK;
}

status_t VpuWrapperDec::DecoderRegisterOneFrame(GraphicBlockInfo* info) {
    status_t err = OK;
    VpuFrameBuffer frameBuf;
    int32_t frameNum;

    memset(&frameBuf, 0, sizeof(VpuFrameBuffer));

    if (OK != CreateOneRegisterFrameBuffer(info, &frameBuf))
        return BAD_VALUE;

    VpuDecRetCode ret;
	ret = VPU_DecRegisterFrameBuffer(nHandle, &frameBuf, 1);
    if (VPU_DEC_RET_SUCCESS == ret)
        nFreeOutBufferCnt++;

    return OK;
}

status_t VpuWrapperDec::DecoderRegisterAllFrames() {
    status_t err = OK;
    VpuFrameBuffer frameBuf[MAX_FRAME_NUM];
    int32_t i;
    int32_t frameNum = 0;

    CHECK_DEC_STATE(VPU_COM_STATE_WAIT_FRM);

    memset(frameBuf, 0, MAX_FRAME_NUM * sizeof(VpuFrameBuffer));

    // register output frame buffer to frame pool/vpu
    for(i = 0; i < mOutputFormat.bufferNum; i++) {
        GraphicBlockInfo* info = getFreeGraphicBlock();
        if (info == nullptr)
            break; // no more free block

        if (OK != CreateOneRegisterFrameBuffer(info, &frameBuf[i]))
            break;

        frameNum++;
    }

    if (frameNum != mOutputFormat.bufferNum) {
        VPU_COMP_ERR_LOG("%s: failed, not enough output buffer, request %d register %d\n",
            __FUNCTION__, mOutputFormat.bufferNum, frameNum);
        return BAD_VALUE;
    }

    VpuDecRetCode ret;
	ret = VPU_DecRegisterFrameBuffer(nHandle, frameBuf, frameNum);
    if (VPU_DEC_RET_SUCCESS != ret) {
        return BAD_VALUE;
    }

    eVpuDecoderState = VPU_COM_STATE_DO_DEC;
    nFreeOutBufferCnt = frameNum;

    return OK;
}

VideoDecoderBase * CreateVideoDecoderInstance(const char* mime) {
    return static_cast<VideoDecoderBase *>(new VpuWrapperDec(mime));
}

}  // namespace android

// end of file
