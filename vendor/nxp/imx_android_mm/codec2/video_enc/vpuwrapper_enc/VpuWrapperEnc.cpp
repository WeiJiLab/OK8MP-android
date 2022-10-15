/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "VpuWrapperEnc"

#include <media/stagefright/MediaDefs.h>

#include "VpuWrapperEnc.h"
#include "graphics_ext.h"
#include "Tsm_wrapper.h"

//#define DUMP_YUV
#ifdef  DUMP_YUV
#include "IonAllocator.h"
#include <sys/mman.h>
#endif

namespace android {

#define VPU_ENC_COMP_INFO ALOGI

//#define VPU_ENC_COMP_DBGLOG
#ifdef VPU_ENC_COMP_DBGLOG
#define VPU_ENC_COMP_LOG    ALOGD
#else
#define VPU_ENC_COMP_LOG(...)
#endif

//#define VPU_ENC_COMP_API_DBGLOG
#ifdef VPU_ENC_COMP_API_DBGLOG
#define VPU_ENC_COMP_API_LOG    ALOGI
#else
#define VPU_ENC_COMP_API_LOG(...)
#endif

#define VPU_ENC_COMP_ERR_DBGLOG
#ifdef VPU_ENC_COMP_ERR_DBGLOG
#define VPU_ENC_COMP_ERR_LOG	ALOGE
#define ASSERT(exp)	if(!(exp)) {ALOGE("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}
#else
#define VPU_ENC_COMP_ERR_LOG    ALOGE
#define ASSERT(...)
#endif

#ifdef NULL
#undef NULL
#define NULL 0
#endif

#define Align(ptr,align)    (((unsigned long)ptr+(align)-1)/(align)*(align))

#define ENC_MAX_FRAME_NUM       (VPU_ENC_MAX_NUM_MEM)

#define DEFAULT_ENC_FRM_WIDTH       (320)
#define DEFAULT_ENC_FRM_HEIGHT      (240)
#define DEFAULT_ENC_FRM_RATE        (30)
#define DEFAULT_ENC_FRM_BITRATE     (256 * 1024)

#define DEFAULT_ENC_BUF_IN_CNT      0x2
#define DEFAULT_ENC_BUF_IN_SIZE     (DEFAULT_ENC_FRM_WIDTH*DEFAULT_ENC_FRM_HEIGHT*3/2)
#define DEFAULT_ENC_BUF_OUT_CNT     0x3
#define DEFAULT_ENC_BUF_OUT_SIZE    (1024*1024) //FIXME: set one big enough value !!!

#define VPU_ENC_VP8_BITRATE_THRESHOLD   (20000000) // 20Mbps

#define VPURET2ERR(ret) ((ret != VPU_ENC_RET_SUCCESS) ? BAD_VALUE : OK)
#define CHECK_VPU_RET(ret) if (ret != VPU_ENC_RET_SUCCESS) {ALOGE("%s line %d, ret %d\n", __FUNCTION__, __LINE__, ret); return VPURET2ERR(ret);}
#define CHECK_DEC_STATE(state) \
    if (eVpuEncoderState != state) {\
        VPU_ENC_COMP_ERR_LOG("%s: failure: error state transition, current state=%d\n", __FUNCTION__, eVpuEncoderState);\
        return INVALID_OPERATION;\
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
			VpuEncRetCode ret;
			vpuMem.nSize = pMemBlock->MemSubBlock[i].nSize;
            vpuMem.nVirtAddr = (unsigned long)pMemBlock->MemSubBlock[i].pVirtAddr;
            vpuMem.nPhyAddr = (unsigned long)pMemBlock->MemSubBlock[i].pPhyAddr;
            vpuMem.nCpuAddr = (unsigned long)pMemBlock->MemSubBlock[i].nFd;

            ret = VPU_EncFreeMem(&vpuMem);

            if (ret != VPU_ENC_RET_SUCCESS) {
				VPU_ENC_COMP_LOG("%s: free vpu memory failure, ret=%d\n", __FUNCTION__, ret);
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

    VPU_ENC_COMP_API_LOG("%s: ", __FUNCTION__);

	for (i = 0; i < pMemBlock->nSubBlockNum; i++) {
		size = pMemBlock->MemSubBlock[i].nAlignment + pMemBlock->MemSubBlock[i].nSize;
		if (pMemBlock->MemSubBlock[i].MemType == VPU_MEM_VIRT) {
            unsigned char * ptr = (unsigned char *)malloc(size);
			if (ptr == NULL) {
				VPU_ENC_COMP_LOG("%s: get virtual memory failure, size=%d \r\n", __FUNCTION__, size);
				return 0;
			}
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char *)Align(ptr, pMemBlock->MemSubBlock[i].nAlignment);
        } else {
			VpuMemDesc vpuMem;
			VpuEncRetCode ret;
			vpuMem.nSize = size;
            vpuMem.nType = VPU_MEM_DESC_NORMAL;
			ret = VPU_EncGetMem(&vpuMem);
			if(ret != VPU_ENC_RET_SUCCESS) {
				VPU_ENC_COMP_LOG("%s: get vpu memory failure, size=%d, ret=0x%X \r\n", __FUNCTION__, size, ret);
				return 0;
			}

			pMemBlock->MemSubBlock[i].pVirtAddr = (unsigned char *)Align(vpuMem.nVirtAddr,pMemBlock->MemSubBlock[i].nAlignment);
			pMemBlock->MemSubBlock[i].pPhyAddr = (unsigned char *)Align(vpuMem.nPhyAddr,pMemBlock->MemSubBlock[i].nAlignment);
            pMemBlock->MemSubBlock[i].nFd = (int)vpuMem.nCpuAddr;
            pMemBlock->MemSubBlock[i].nSize = vpuMem.nSize; // update size because phys size is larger due to align with pagesize

            if (pMemBlock->MemSubBlock[i].pVirtAddr != (unsigned char *)vpuMem.nVirtAddr ||
                    pMemBlock->MemSubBlock[i].pPhyAddr != (unsigned char *)vpuMem.nPhyAddr)
                VPU_ENC_COMP_LOG("VPU_DecGetMem not aligned, nAlignment %d", pMemBlock->MemSubBlock[i].nAlignment);
		}
	}

	return 1;
}

int OutFrameBufRegister(int bufferId,
                                  unsigned long pInPhyAddr,
                                  unsigned long pInVirtAddr,
                                  uint32_t capacity,
                                  VpuEncoderMemInfo* pOutEncMem) {

	if (!pInPhyAddr || !pInVirtAddr || !pOutEncMem) {
        return -1;
	}

    int i;
	for (i = 0; i < VPU_ENC_MAX_NUM_MEM; i++) {
		//insert into empty node
		if (NULL == pOutEncMem->phyMem_phyAddr[i]) {
			pOutEncMem->phyMem_phyAddr[i] = pInPhyAddr;
			pOutEncMem->phyMem_virtAddr[i] = pInVirtAddr;
            pOutEncMem->phyMem_size[i] = capacity;
            pOutEncMem->phyMem_bufferId[i] = bufferId;
			pOutEncMem->nPhyNum++;
			return pOutEncMem->nPhyNum;
		}
	}

	return -1;
}

int OutFrameBufNum(VpuEncoderMemInfo* pOutEncMem) {
    return pOutEncMem->nPhyNum;
}

int OutFrameBufExist(unsigned long physAddr, VpuEncoderMemInfo* pInEncMem) {
	int i;

	//physical space
	for (i = 0; i < VPU_ENC_MAX_NUM_MEM; i++) {
		//search matched node
		if(physAddr == pInEncMem->phyMem_phyAddr[i]) {
			return 1;
		}
	}

    return 0;
}

int OutFrameBufClear(VpuEncoderMemInfo* pOutEncMem)
{
	int i;
	//clear all node
	for(i=0;i<VPU_ENC_MAX_NUM_MEM;i++)
	{
		pOutEncMem->phyMem_virtAddr[i]=NULL;
		pOutEncMem->phyMem_phyAddr[i]=NULL;
		pOutEncMem->phyMem_virtAddr[i]=NULL;
		pOutEncMem->phyMem_cpuAddr[i]=NULL;
		pOutEncMem->phyMem_size[i]=0;
	}
	pOutEncMem->nVirtNum=0;
	pOutEncMem->nPhyNum=0;
	return 1;
}

int OutFrameBufPhyFindValid(VpuEncoderMemInfo* pInMem,
                                        unsigned long* pOutPhy,
                                        unsigned long* pOutVirt,
                                        uint32_t* pOutLen) {
	int i;

	for (i = 0; i < VPU_ENC_MAX_NUM_MEM; i++) {
		//find one non-empty physical node
		if (NULL != pInMem->phyMem_phyAddr[i]) {
			*pOutPhy = pInMem->phyMem_phyAddr[i];
			*pOutVirt = pInMem->phyMem_virtAddr[i];
		    *pOutLen = pInMem->phyMem_size[i];
			return i;
		}
	}
	return -1;
}

int OutFrameBufVirtFindValidAndClear(VpuEncoderMemInfo* pInMem, int* bufferId) {
	uint32_t i;

	for (i = 0; i < VPU_ENC_MAX_NUM_MEM; i++) {
		//find one non-empty physical node and return its buffer id
		if (NULL != pInMem->phyMem_phyAddr[i]) {
			*bufferId = pInMem->phyMem_bufferId[i];
			//clear the node
			pInMem->phyMem_virtAddr[i] = 0;
			pInMem->phyMem_phyAddr[i] = 0;
			pInMem->phyMem_virtAddr[i] = 0;
			pInMem->phyMem_cpuAddr[i] = 0;
            pInMem->phyMem_bufferId[i] = -1;
			pInMem->phyMem_size[i] = 0;
			pInMem->nPhyNum--;
			return i;
		}
	}
	return -1;
}

int OutFrameBufPhyClear(VpuEncoderMemInfo* pOutDecMem, unsigned long pInPhyAddr, int* bufferId) {
	//find node according physical address and (1) return virtual address (2) clear node
	uint32_t i;

	for (i = 0; i < VPU_ENC_MAX_NUM_MEM; i++) {
		if (pInPhyAddr == pOutDecMem->phyMem_phyAddr[i]) {
			//return buffer id
			*bufferId = pOutDecMem->phyMem_bufferId[i];
			//clear specified physical node
			pOutDecMem->phyMem_virtAddr[i] = 0;
			pOutDecMem->phyMem_phyAddr[i] = 0;
			pOutDecMem->phyMem_virtAddr[i]= 0;
			pOutDecMem->phyMem_cpuAddr[i] = 0;
            pOutDecMem->phyMem_bufferId[i] = -1;
			pOutDecMem->phyMem_size[i] =0;

			pOutDecMem->nPhyNum--;
			return 1;
		}
	}

	return -1;
}



int SetDefaultEncParam(VpuEncInputParam* pEncParam) {
	pEncParam->eFormat = VPU_V_AVC;
	pEncParam->nPicWidth = DEFAULT_ENC_FRM_WIDTH;
	pEncParam->nPicHeight = DEFAULT_ENC_FRM_HEIGHT;
	pEncParam->nWidthStride = pEncParam->nPicWidth;
	pEncParam->nHeightStride = pEncParam->nPicHeight;
	pEncParam->nRotAngle = 0;
	pEncParam->nFrameRate = 30;
	ASSERT(0 != pEncParam->nFrameRate);
	pEncParam->nBitRate = 0;
	pEncParam->nGOPSize = 15;
	pEncParam->nChromaInterleave = 0;
	pEncParam->sMirror = VPU_ENC_MIRDIR_NONE;

	/* "nQuantParam" is used for all quantization parameters in case of VBR (no rate control).
	The range of value is 1-31 for MPEG-4 and 0-51 for H.264. When rate control is
	enabled, this field is ignored*/
	pEncParam->nQuantParam = 10;

	pEncParam->nEnableAutoSkip = 1;

	pEncParam->nIDRPeriod=pEncParam->nGOPSize;
	pEncParam->nRefreshIntra = 0;
	pEncParam->nIntraFreshNum = 0;
	pEncParam->bEnabledSPSIDR = false;
	pEncParam->nRcIntraQP = 0;
	return 1;
}

VpuWrapperEnc::VpuWrapperEnc(const char* mime)
    : mMime(mime) {

    // init mMime2TypeMap
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_AVC, VPU_V_AVC);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_VP8, VPU_V_VP8);
    mMime2TypeMap.emplace(MEDIA_MIMETYPE_VIDEO_HEVC, VPU_V_HEVC);

    pCodecdataBuf = nullptr;
    nCodecDataLen = 0;

    SetDefaultSetting();
}

VpuWrapperEnc::~VpuWrapperEnc() {
}

void VpuWrapperEnc::SetDefaultSetting() {
 	//set default

	//clear internal variable 0
	memset(&sEncInitInfo, 0, sizeof(VpuEncInitInfo));
	memset(&sMemInfo, 0, sizeof(VpuMemInfo));
	memset(&sEncOutFrameInfo, 0, sizeof(VpuEncoderMemInfo));
	memset(&sVpuEncInputPara, 0, sizeof(VpuEncInputParam));
    memset(&sColorDesc, 0, sizeof(VpuColourDesc));

	nHandle = nullptr;
	pInBufferPhys = nullptr;
	pInBufferVirt = nullptr;
	nInSize = 0;
	bInEos = false;
	pOutBufferPhys = nullptr;
    poutBufferVirt = nullptr;
	nOutSize = 0;
	nOutGOPFrameCnt = 1;
	eVpuEncoderState = VPU_ENC_COM_STATE_NONE;

	SetDefaultEncParam(&sVpuEncInputPara);

	return;
}

status_t VpuWrapperEnc::onInit() {
    VpuEncRetCode ret;
    VpuVersionInfo ver;

    VPU_ENC_COMP_API_LOG("%s line %d", __FUNCTION__, __LINE__);

    hTsHandle = tsmCreate();
    if (hTsHandle == nullptr) {
        ALOGE("Create Ts manager failed.\n");
        return BAD_VALUE;
    }

    ret = VPU_EncLoad();
    CHECK_VPU_RET(ret);

    ret = VPU_EncGetVersionInfo(&ver);
    CHECK_VPU_RET(ret);

    VPU_ENC_COMP_LOG("vpu lib version : rel.major.minor=%d.%d.%d \r\n",
        ver.nLibRelease, ver.nLibMajor, ver.nLibMinor);
	VPU_ENC_COMP_LOG("vpu fw version : rel.major.minor=%d.%d.%d \r\n",
        ver.nFwRelease, ver.nFwMajor, ver.nFwMinor);

    auto node = mMime2TypeMap.find(mMime);
    if (node == mMime2TypeMap.end()) {
        VPU_ENC_COMP_ERR_LOG("%s line %d, unsupported decoder mime %s\n", __FUNCTION__, __LINE__, mMime);
        return BAD_VALUE;
    } else
        sVpuEncInputPara.eFormat = node->second;

	if (OK != OpenVpu()) {
		VPU_ENC_COMP_ERR_LOG("%s: vpu init failure \n",__FUNCTION__);
		return BAD_VALUE;
	}

    //eVpuEncoderState = VPU_ENC_COM_STATE_WAIT_FRM;
    eVpuEncoderState= VPU_ENC_COM_STATE_DO_DEC;
    return OK;
}

status_t VpuWrapperEnc::onStop() {
    status_t err = OK;
	VPU_ENC_COMP_API_LOG("%s: \r\n",__FUNCTION__);

	//check state
	switch(eVpuEncoderState)
	{
		case VPU_ENC_COM_STATE_NONE:
			//forbidden
			VPU_ENC_COMP_ERR_LOG("%s: failure: error state transition, current state=%d \r\n",__FUNCTION__,eVpuEncoderState);
			return BAD_VALUE;
		default:
			break;
	}

	err = ReleaseVpuSource();

    if (hTsHandle) {
        tsmDestroy(hTsHandle);
        hTsHandle = nullptr;
    }

	//clear handle
	nHandle = nullptr;

	//restore default to support following switch from loaded to idle later.
	SetDefaultSetting();

	//update state
	eVpuEncoderState = VPU_ENC_COM_STATE_LOADED;
    return OK;
}

status_t VpuWrapperEnc::onFlush() {
    status_t err = OK;

    VPU_ENC_COMP_API_LOG("%s: state: %d  \r\n", __FUNCTION__, eVpuEncoderState);

    //check state
    switch (eVpuEncoderState) {
        case VPU_ENC_COM_STATE_DO_DEC:
            break;
        default:
            //forbidden !!!
            VPU_ENC_COMP_ERR_LOG("%s: unknown state transition, current state=%d \r\n", __FUNCTION__, eVpuEncoderState);
    }

    //clear input buffer
    pInBufferPhys = nullptr;
    pInBufferVirt = nullptr;
    nInSize = 0;
    bInEos = false;
    nCurInputId = -1;

    tsmFlush(hTsHandle);

    NotifyFlushDone();

    return err;
}

status_t VpuWrapperEnc::onDestroy() {
    status_t err = OK;
	VPU_ENC_COMP_API_LOG("%s: \r\n",__FUNCTION__);

	//check state
	switch(eVpuEncoderState)
	{
		case VPU_ENC_COM_STATE_NONE:
			//forbidden
			VPU_ENC_COMP_ERR_LOG("%s: failure: error state transition, current state=%d \r\n",__FUNCTION__,eVpuEncoderState);
			return BAD_VALUE;
		default:
			//invalid state, we need to close/unload vpu
			VPU_ENC_COMP_ERR_LOG("invalid state: %d, close vpu manually \r\n",eVpuEncoderState);
			err = ReleaseVpuSource();
			break;
	}

    if (hTsHandle) {
        tsmDestroy(hTsHandle);
        hTsHandle = nullptr;
    }

	eVpuEncoderState = VPU_ENC_COM_STATE_LOADED;
    return err;
}

status_t VpuWrapperEnc::encodeInternal(std::unique_ptr<IMXInputBuffer> input) {
	VpuEncRetCode ret;
	int index;
	unsigned long nOutPhy;
	unsigned long nOutVirt;
	uint32_t nOutLength;
	VpuEncEncParam sEncEncParam;
	int nValidInput = 1;
	unsigned long nInputPhy, nInputVirt;
	int nInputSize;
    status_t err = OK;
    int encodeCnt = 0;

	switch (eVpuEncoderState) {
		//forbidden state
		case VPU_ENC_COM_STATE_NONE:
		case VPU_ENC_COM_STATE_DO_OUT:
			VPU_ENC_COMP_ERR_LOG("%s: failure: error state transition, current state=%d \r\n", __FUNCTION__, eVpuEncoderState);
			return INVALID_OPERATION;
        case VPU_ENC_COM_STATE_WAIT_FRM: {
            if (OutFrameBufNum(&sEncOutFrameInfo) < sEncInitInfo.nMinFrameBufferCount) {
                VPU_ENC_COMP_LOG("OutFrameBufNum is not enough, request %d \n", sEncInitInfo.nMinFrameBufferCount);
                return OK;
            }

            eVpuEncoderState = VPU_ENC_COM_STATE_LOADED;
            // fallthrough
        }
        case VPU_ENC_COM_STATE_LOADED: {
            if ((nullptr == pInBufferPhys || nInSize <= 0) && (false == bInEos)) {
			    return BAD_VALUE;
			}

			eVpuEncoderState = VPU_ENC_COM_STATE_DO_DEC;
            break;
        }
		case VPU_ENC_COM_STATE_DO_DEC:
			break;
		default:
			VPU_ENC_COMP_ERR_LOG("%s: failure state transition, current state=%d \r\n", __FUNCTION__, eVpuEncoderState);
			return INVALID_OPERATION;
	}

    if (input && input->eos) {
        bInEos = true;
        pInBufferPhys = nullptr;
        pInBufferVirt = nullptr;
        nInSize = 0;
    } else if (input) {
        pInBufferPhys = input->pInputPhys;
        pInBufferVirt = input->pInputVirt;
        nInSize = input->size;
        nCurInputId = input->id;
        tsmSetBlkTs(hTsHandle, nInSize, input->timestamp);

        #ifdef DUMP_YUV
        if (nInSize > 0) {
            unsigned long virtAddr = (unsigned long)pInBufferVirt;
            bool needUnmap = false;
            if (virtAddr == 0) {
                int fd = input->fd;
                fsl::IonAllocator * pIonAllocator = fsl::IonAllocator::getInstance();
                int ret = pIonAllocator->getVaddrs(fd, nInSize, (uint64_t&)virtAddr);
                if (ret != 0) {
                    VPU_ENC_COMP_ERR_LOG("Ion get physical address failed, fd %d", fd);
                } else
                    needUnmap = true;
            }

            if (virtAddr > 0) {
                FILE * pfile;
                pfile = fopen("/data/dumpYUV","ab");
                if(pfile) {
                    ALOGI("dump size %d", nInSize);
                    fwrite((void*)virtAddr,1,nInSize,pfile);
                    fclose(pfile);
                } else {
                    VPU_ENC_COMP_ERR_LOG("open dumpfile failed\n");
                }
            }
            if (needUnmap && virtAddr > 0)
                munmap((void*)virtAddr, nInSize);
        }
        #endif
    }

	if ((nullptr == pInBufferPhys) || (nInSize <= 0)) {
		nValidInput = 0;
		nInputPhy = 0;
		nInputVirt = 0;
		nInputSize = 0;
	} else {
		nInputPhy = (unsigned long)pInBufferPhys;
		nInputVirt = (unsigned long)pInBufferVirt;
		nInputSize = nInSize;
	}

    if (0 == nValidInput) {
		if (bInEos)	{
			//eos: and no output frame
			pOutBufferPhys = nullptr;//no real output frame
			poutBufferVirt = nullptr;
            nInSize = 0;
			NotifyEOS();
            return OK;
		} else {
			//no input buffer
			ALOGE("!!!! nValidInput is true");
            return OK;
		}
    }

    int blockId;
    int fd;
    nOutLength = mOutputFormat.bufferSize;
    if (FetchOutputBuffer(&blockId, &fd, &nOutPhy, &nOutVirt, &nOutLength) != OK)
        return BAD_VALUE;
    if (setOutputBuffer(blockId, nOutPhy, nOutVirt, nOutLength) != OK)
        return BAD_VALUE;
    if (-1 == OutFrameBufPhyFindValid(&sEncOutFrameInfo, &nOutPhy, &nOutVirt, &nOutLength))
		return BAD_VALUE;

	VPU_ENC_COMP_LOG("%s: state: %d, InBuf: virt(%p), phys(%p), inSize(%d), bInEos: %d, OutBuf: virt(%p), phys(%p)\n", \
            __FUNCTION__, eVpuEncoderState, (void*)nInputVirt, (void*)nInputPhy, nInputSize, bInEos,
            (void*)nOutVirt, (void*)nOutPhy);

    memset(&sEncEncParam, 0, sizeof(VpuEncEncParam));
	sEncEncParam.eFormat = sVpuEncInputPara.eFormat;
	sEncEncParam.nPicWidth = sVpuEncInputPara.nWidthStride;//sVpuEncInputPara.nPicWidth;
	sEncEncParam.nPicHeight = sVpuEncInputPara.nHeightStride;//sVpuEncInputPara.nPicHeight;
	sEncEncParam.nFrameRate = sVpuEncInputPara.nFrameRate;
	sEncEncParam.nQuantParam = sVpuEncInputPara.nQuantParam;
	sEncEncParam.nInPhyInput = nInputPhy;
	sEncEncParam.nInVirtInput = nInputVirt;
	sEncEncParam.nInInputSize = nInputSize;
	sEncEncParam.nInPhyOutput = nOutPhy;
	sEncEncParam.nInVirtOutput = nOutVirt;
	sEncEncParam.nInOutputBufLen = nOutLength;

	//(1)check the frame count, for H.264
	//In current design, we will set IDR frame for every I frame
	//(2)check I refresh command by user
	if ((1 == sVpuEncInputPara.nRefreshIntra) ||
        ((VPU_V_AVC == sEncEncParam.eFormat || VPU_V_VP8 == sEncEncParam.eFormat) && (1 == nOutGOPFrameCnt))) {
		sEncEncParam.nForceIPicture = 1;
		sVpuEncInputPara.nRefreshIntra = 0; 	//clear it every time
		nOutGOPFrameCnt = 1;
	} else {
		sEncEncParam.nForceIPicture=0;
	}

	sEncEncParam.nSkipPicture = 0;
	sEncEncParam.nEnableAutoSkip = sVpuEncInputPara.nEnableAutoSkip;

encode_one_frame:
	ret = VPU_EncEncodeFrame(nHandle, &sEncEncParam);

    encodeCnt++;

	if (VPU_ENC_RET_SUCCESS != ret) {
		if (VPU_ENC_RET_FAILURE_TIMEOUT == ret) {
			VPU_ENC_COMP_ERR_LOG("%s: encode frame timeout \r\n",__FUNCTION__);
			VPU_EncReset(nHandle);
		}
		return BAD_VALUE;
	}

	//check input
	if (sEncEncParam.eOutRetCode & VPU_ENC_INPUT_USED) {
        tsmSetFrmBoundary(hTsHandle, 0, nInSize, nullptr);
        pInBufferPhys = nullptr;  //clear input
		pInBufferVirt = nullptr;
		nInSize = 0;

        if(nCurInputId != (-1)) {
            NotifyInputBufferUsed(nCurInputId/*input_id*/);
            nCurInputId = -1;
		}
	} else {
		//not used
	}

	//check output
	if (sEncEncParam.eOutRetCode & VPU_ENC_OUTPUT_DIS) {

		eVpuEncoderState = VPU_ENC_COM_STATE_DO_OUT;

        //record output info
		pOutBufferPhys = (void*)nOutPhy;
        poutBufferVirt = (void*)nOutVirt;
		nOutSize = sEncEncParam.nOutOutputSize;
        GetOutputBuffer();
		VPU_ENC_COMP_LOG("[%d]frame data: %d \r\n", nOutGOPFrameCnt, nOutSize);

        //update count
		nOutGOPFrameCnt++;
		if (nOutGOPFrameCnt > sVpuEncInputPara.nGOPSize) {
			nOutGOPFrameCnt = 1;
		}
	} else if(sEncEncParam.eOutRetCode & VPU_ENC_OUTPUT_SEQHEADER) {
	    // save codec data
	    if (!pCodecdataBuf) {
            pCodecdataBuf = (uint8_t*)malloc(sEncEncParam.nOutOutputSize);
            if (!pCodecdataBuf) {
                VPU_ENC_COMP_ERR_LOG("%s line %d, malloc %d failed", __FUNCTION__, __LINE__, sEncEncParam.nOutOutputSize);
            }
        }
        memcpy(pCodecdataBuf, (void*)nOutVirt, sEncEncParam.nOutOutputSize);
        nCodecDataLen += sEncEncParam.nOutOutputSize;
		VPU_ENC_COMP_LOG("sequence header: %d \r\n",sEncEncParam.nOutOutputSize);

	}

    if (!(sEncEncParam.eOutRetCode & VPU_ENC_INPUT_USED)) {
	    if (encodeCnt > 100)
            NotifyError(BAD_VALUE);
        else
            goto encode_one_frame; // input buffer is not consumed, call encode again.
    }

    return err;
}

status_t VpuWrapperEnc::setOutputBuffer(int bufferId, unsigned long pPhyAddr, unsigned long pVirtAddr, uint32_t capacity) {
    VpuDecRetCode ret;
    VpuDecOutFrameInfo * pFrameInfo;
    int32_t exist;

    exist = OutFrameBufExist(pPhyAddr, &sEncOutFrameInfo);

    VPU_ENC_COMP_LOG("%s, buffer id %d, exist %d", __FUNCTION__, bufferId, exist);

    if (exist == 0) {
		//register output frame buffer
		if (-1 == OutFrameBufRegister(bufferId, pPhyAddr, pVirtAddr, capacity, &sEncOutFrameInfo)) {
			VPU_ENC_COMP_ERR_LOG("%s: failure: unvalid buffer ! \r\n",__FUNCTION__);
			return BAD_VALUE;
		}
    } else {
        VPU_ENC_COMP_LOG("%s line %d, enter error state", __FUNCTION__, __LINE__);
        return BAD_VALUE;
    }
    return OK;
}

status_t VpuWrapperEnc::getOutputVideoInfo(VideoFormat * info) {
    return OK;
}

status_t VpuWrapperEnc::DoSetConfig(EncConfig index, void* pConfig) {
    switch (index) {
        case ENC_CONFIG_BIT_RATE: {
            int kbps;
            sVpuEncInputPara.nBitRate = (*(int*)pConfig);
            kbps = sVpuEncInputPara.nBitRate/1000;
            if (VPU_ENC_RET_SUCCESS != VPU_EncConfig(nHandle, VPU_ENC_CONF_BIT_RATE, &kbps)) {
                VPU_ENC_COMP_ERR_LOG("%s line %d: failure\r\n", __FUNCTION__, __LINE__);
                return BAD_VALUE;
            }
            VPU_ENC_COMP_LOG("config bit rate %d", sVpuEncInputPara.nBitRate);
            break;
        }
        case ENC_CONFIG_FRAME_RATE: {
            int frameRate = (*(int*)pConfig);
            if (sVpuEncInputPara.nFrameRate != frameRate) {
                int bps, kbps;
                bps = sVpuEncInputPara.nBitRate * sVpuEncInputPara.nFrameRate / frameRate;
                kbps = bps/1000;
                if (VPU_ENC_RET_SUCCESS == VPU_EncConfig(nHandle, VPU_ENC_CONF_BIT_RATE, &kbps)) {
                    sVpuEncInputPara.nFrameRate = frameRate;
                    sVpuEncInputPara.nBitRate = bps;
                } else {
                    VPU_ENC_COMP_ERR_LOG("%s line %d: failure\r\n", __FUNCTION__, __LINE__);
                    return BAD_VALUE;
                }
                VPU_ENC_COMP_LOG("config frame rate %d, bit rate %d", frameRate, bps);
            }
            break;
        }
        case ENC_CONFIG_INTRA_REFRESH: {
            sVpuEncInputPara.nRefreshIntra = (*(int*)pConfig);
            VPU_ENC_COMP_LOG("config RefreshIntra %d", sVpuEncInputPara.nRefreshIntra);
            break;
        }
        case ENC_CONFIG_COLOR_FORMAT: {
            sVpuEncInputPara.ePixelFormat = (*(int*)pConfig);
            break;
        }
        default:
            return BAD_VALUE;
    }

    return OK;
}

status_t VpuWrapperEnc::DoGetConfig(EncConfig index, void* pConfig) {
    return OK;
}

bool VpuWrapperEnc::checkIfPreProcessNeeded(int pixelFormat) {
    switch (pixelFormat) {
#ifndef HANTRO_VC8000E
        // vc8000e don't need pre process because it emplement full video range;
        // h1 need pre process to handle RGB
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_RGB_888:
		case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
		case HAL_PIXEL_FORMAT_BGRA_8888:
#endif
        case HAL_PIXEL_FORMAT_YV12:
            VPU_ENC_COMP_LOG("need pre-process, pixel format %x", pixelFormat);
            return true;
        default:
            VPU_ENC_COMP_LOG("no need pre-process, pixel format %x", pixelFormat);
            return false;
    }
}

void VpuWrapperEnc::initEncInputParamter(EncInputParam *pInPara) {
    if (!pInPara) {
        VPU_ENC_COMP_ERR_LOG("invalid encoder input parameter !");
        return;
    }

    sVpuEncInputPara.nPicWidth = pInPara->nPicWidth;
    sVpuEncInputPara.nPicHeight = pInPara->nPicHeight;
    sVpuEncInputPara.nWidthStride = pInPara->nWidthStride;
    sVpuEncInputPara.nHeightStride = pInPara->nHeightStride;
    sVpuEncInputPara.nFrameRate = pInPara->nFrameRate;
    sVpuEncInputPara.nRotAngle = pInPara->nRotAngle;
    sVpuEncInputPara.nBitRate = pInPara->nBitRate;
    sVpuEncInputPara.nGOPSize = pInPara->nGOPSize;
    sVpuEncInputPara.nIDRPeriod = pInPara->nIDRPeriod;
    sVpuEncInputPara.nRefreshIntra = pInPara->nRefreshIntra;
    sVpuEncInputPara.bEnabledSPSIDR = pInPara->bEnabledSPSIDR;
    sVpuEncInputPara.nRcIntraQP = pInPara->nRcIntraQP;
    sVpuEncInputPara.nEnableAutoSkip = pInPara->nEnableAutoSkip;
    sVpuEncInputPara.nQuantParam = pInPara->nQuantParam;
    sVpuEncInputPara.ePixelFormat = pInPara->eColorFormat;

    memcpy(&sVpuEncInputPara.sColorInfo, &pInPara->sColorAspects, sizeof(EncIsoColorAspects));

    if (pInPara->eColorFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP ||
            pInPara->eColorFormat == HAL_PIXEL_FORMAT_YCBCR_420_888) {
        sVpuEncInputPara.nChromaInterleave = 1;
    }
}

status_t VpuWrapperEnc::getCodecData(uint8_t** pCodecData, uint32_t* size) {
    status_t ret = OK;

    if (!pCodecData || !size)
        return BAD_VALUE;

    if (pCodecdataBuf && nCodecDataLen > 0) {
        *pCodecData = pCodecdataBuf;
        *size = nCodecDataLen;
    }

    return ret;
}

status_t VpuWrapperEnc::FlushFilter()
{
	VpuEncRetCode ret;
	VPU_ENC_COMP_LOG("%s: \r\n",__FUNCTION__);

    //clear input buffer
	pInBufferPhys = NULL;
	pInBufferVirt = NULL;
	nInSize = 0;
	bInEos = false;

	//clear out frame info
	OutFrameBufClear(&sEncOutFrameInfo);

	return OK;
}

status_t VpuWrapperEnc::ReleaseVpuSource()
{
	VpuEncRetCode ret;
    status_t err;

	//close vpu
	if (nHandle) {
		ret = VPU_EncClose(nHandle);
		if (ret != VPU_ENC_RET_SUCCESS)
		{
			VPU_ENC_COMP_ERR_LOG("%s: vpu close failure: ret=0x%X \r\n",__FUNCTION__,ret);
			err = BAD_VALUE;
		}
	}

	//release mem
	if (0 == MemFreeBlock(&sMemInfo)) {
		VPU_ENC_COMP_ERR_LOG("%s: free memory failure !  \r\n",__FUNCTION__);
		err = BAD_VALUE;
	}

	//unload
	ret = VPU_EncUnLoad();
	if (ret != VPU_ENC_RET_SUCCESS) {
		VPU_ENC_COMP_ERR_LOG("%s: vpu unload failure: ret=0x%X \r\n",__FUNCTION__,ret);
		err = BAD_VALUE;
	}

    if (pCodecdataBuf) {
        free(pCodecdataBuf);
        pCodecdataBuf = nullptr;
        nCodecDataLen = 0;
    }

	return err;
}


status_t VpuWrapperEnc::GetOutputBuffer() {
	int bufferId = -1;
    int isKeyFrame = 0;
    uint64_t timestamp = -1;

	VPU_ENC_COMP_API_LOG("%s: state: %d \r\n", __FUNCTION__, eVpuEncoderState);

	//check state
	switch(eVpuEncoderState) {
		case VPU_ENC_COM_STATE_DO_OUT:
			//update state
			eVpuEncoderState = VPU_ENC_COM_STATE_DO_DEC;
			break;
		default:
			//forbidden
			VPU_ENC_COMP_ERR_LOG("%s: failure state transition, current state=%d \r\n",__FUNCTION__,eVpuEncoderState);
			return BAD_VALUE;
	}

	if (NULL == pOutBufferPhys) {
		//no real output frame: for eos case
		if (-1 == OutFrameBufVirtFindValidAndClear(&sEncOutFrameInfo, &bufferId)) {
			VPU_ENC_COMP_ERR_LOG("%s: failure: can not find one valid virtual address !!! \r\n",__FUNCTION__);
			return BAD_VALUE;
		}

	} else {
		if (-1 == OutFrameBufPhyClear(&sEncOutFrameInfo, (unsigned long)pOutBufferPhys, &bufferId)) {
			VPU_ENC_COMP_ERR_LOG("%s: failure: unvalid output physical address !!! \r\n",__FUNCTION__);
			return BAD_VALUE;
		}
	}

    timestamp = tsmGetFrmTs(hTsHandle, nullptr);
    isKeyFrame = (1 == nOutGOPFrameCnt);
    NotifyOutputFrameReady(bufferId, nOutSize, timestamp, isKeyFrame, 0);
    return OK;
}

VpuColorFormat VPUCom_ConvertPixelFmt2Vpu(int pixelFormat) {
  VpuColorFormat vpuColorFmt = VPU_COLOR_420;

  switch((int)pixelFormat) {
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_YCbCr_420_P:
    case HAL_PIXEL_FORMAT_YCBCR_420_888:
        vpuColorFmt = VPU_COLOR_420;
        break;
    case HAL_PIXEL_FORMAT_YCBCR_422_I:
        vpuColorFmt = VPU_COLOR_422YUYV;
        break;
#ifdef HANTRO_VC8000E
    case HAL_PIXEL_FORMAT_RGBA_8888: // fall through
    case HAL_PIXEL_FORMAT_RGBX_8888:
        vpuColorFmt = VPU_COLOR_ARGB8888;
        break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
        vpuColorFmt = VPU_COLOR_BGRA8888;
        break;
#else
    case HAL_PIXEL_FORMAT_RGBA_8888: // fall through
    case HAL_PIXEL_FORMAT_RGBX_8888:
        vpuColorFmt = VPU_COLOR_BGRA8888;
        break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
        vpuColorFmt = VPU_COLOR_ARGB8888;
        break;
#endif
    default:
      VPU_ENC_COMP_LOG("unknown pixel format %d\n", pixelFormat);
      break;
  }

  return vpuColorFmt;
}

status_t VpuWrapperEnc::OpenVpu() {
	VpuEncRetCode ret;
	VpuEncOpenParamSimp sEncOpenParam;

	ret = VPU_EncQueryMem(&sMemInfo);
	if (ret != VPU_ENC_RET_SUCCESS) {
		VPU_ENC_COMP_ERR_LOG("%s: vpu query memory failure: ret=0x%X \r\n",__FUNCTION__,ret);
		return BAD_VALUE;
	}

	if (0 == MemMallocBlock(&sMemInfo)) {
		VPU_ENC_COMP_ERR_LOG("%s: malloc memory failure: \r\n",__FUNCTION__);
		return BAD_VALUE;
	}

	memset(&sEncOpenParam, 0, sizeof(VpuEncOpenParamSimp));

	sEncOpenParam.eFormat = sVpuEncInputPara.eFormat;
	sEncOpenParam.nPicWidth = sVpuEncInputPara.nPicWidth;
	sEncOpenParam.nPicHeight = sVpuEncInputPara.nPicHeight;
	sEncOpenParam.nRotAngle = sVpuEncInputPara.nRotAngle;
	sEncOpenParam.nFrameRate = sVpuEncInputPara.nFrameRate;
	sEncOpenParam.nBitRate = sVpuEncInputPara.nBitRate / 1000; // convert bps -> kbps
	sEncOpenParam.nGOPSize = sVpuEncInputPara.nGOPSize;
	sEncOpenParam.nIntraQP = sVpuEncInputPara.nRcIntraQP;
	sEncOpenParam.nChromaInterleave = sVpuEncInputPara.nChromaInterleave;
	sEncOpenParam.sMirror = sVpuEncInputPara.sMirror;
    sEncOpenParam.eColorFormat = VPUCom_ConvertPixelFmt2Vpu(sVpuEncInputPara.ePixelFormat);
    sEncOpenParam.sColorAspects.nColourDescPresentFlag = sVpuEncInputPara.sColorInfo.colourDescriptionPresentFlag;
    sEncOpenParam.sColorAspects.nPrimaries = sVpuEncInputPara.sColorInfo.primaries;
    sEncOpenParam.sColorAspects.nMatrixCoeffs = sVpuEncInputPara.sColorInfo.matrixCoeffs;
    sEncOpenParam.sColorAspects.nTransfer = sVpuEncInputPara.sColorInfo.transfer;
    sEncOpenParam.sColorAspects.nVideoSignalPresentFlag = sVpuEncInputPara.sColorInfo.videoSignalTypePresentFlag;
    sEncOpenParam.sColorAspects.nFullRange = sVpuEncInputPara.sColorInfo.fullRange;

	if (1 == sEncOpenParam.nChromaInterleave) {
		//for reduce the bus loading, we set tile format for vpu internal frame buffers.
		sEncOpenParam.nMapType = 1;
		sEncOpenParam.nLinear2TiledEnable = 1;
	}

	//open vpu
	ret = VPU_EncOpenSimp(&nHandle, &sMemInfo, &sEncOpenParam);
	if (ret != VPU_ENC_RET_SUCCESS) {
		VPU_ENC_COMP_ERR_LOG("%s: vpu open failure: ret=0x%X \r\n",__FUNCTION__,ret);
		return BAD_VALUE;
	}

	//set default config
	ret = VPU_EncConfig(nHandle, VPU_ENC_CONF_NONE, NULL);
	if (VPU_ENC_RET_SUCCESS != ret) {
		VPU_ENC_COMP_ERR_LOG("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(int)VPU_ENC_CONF_NONE,ret);
		return BAD_VALUE;
	}

	if (true == sVpuEncInputPara.bEnabledSPSIDR) {
		ret = VPU_EncConfig(nHandle, VPU_ENC_CONF_ENA_SPSPPS_IDR, NULL);
		if(VPU_ENC_RET_SUCCESS!=ret){
			VPU_ENC_COMP_ERR_LOG("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(int)VPU_ENC_CONF_ENA_SPSPPS_IDR,ret);
			return BAD_VALUE;
		}
	}

	//get initinfo
	ret = VPU_EncGetInitialInfo(nHandle, &sEncInitInfo);
	if (VPU_ENC_RET_SUCCESS != ret) {
		VPU_ENC_COMP_ERR_LOG("%s: init vpu failure \r\n",__FUNCTION__);
		return BAD_VALUE;
	}

	return OK;
}

VideoEncoderBase * CreateVideoEncoderInstance(const char* mime) {
    return static_cast<VideoEncoderBase *>(new VpuWrapperEnc(mime));
}

}  // namespace android

// end of file
