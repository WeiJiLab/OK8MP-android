/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef VPU_WRAPPER_DEC_H
#define VPU_WRAPPER_DEC_H

#include<map>

#include"VideoDecoderBase.h"
#include"vpu_wrapper.h"

namespace android {

#define VPU_DEC_MAX_NUM_MEM	(30)

typedef struct
{
	//virtual mem info
	int32_t nVirtNum;
	uint32_t virtMem[VPU_DEC_MAX_NUM_MEM];

	//phy mem info
	int32_t nPhyNum;
	uint32_t phyMem_virtAddr[VPU_DEC_MAX_NUM_MEM];
	uint32_t phyMem_phyAddr[VPU_DEC_MAX_NUM_MEM];
	uint32_t phyMem_cpuAddr[VPU_DEC_MAX_NUM_MEM];
	uint32_t phyMem_size[VPU_DEC_MAX_NUM_MEM];
}VpuDecoderMemInfo;


typedef enum
{
	VPU_COM_FRM_STATE_FREE=0,
	VPU_COM_FRM_STATE_OUT,
}VpuDecoderFrmState;

typedef struct
{
	int32_t nFrmNum;
	VpuDecoderFrmState eFrmState[VPU_DEC_MAX_NUM_MEM];
	unsigned long nFrm_virtAddr[VPU_DEC_MAX_NUM_MEM];
	unsigned long nFrm_phyAddr[VPU_DEC_MAX_NUM_MEM];
    int nFrm_ionFd[VPU_DEC_MAX_NUM_MEM];
    int32_t nFrm_bufferId[VPU_DEC_MAX_NUM_MEM];
	VpuDecOutFrameInfo outFrameInfo[VPU_DEC_MAX_NUM_MEM];
}VpuDecoderFrmPoolInfo;

typedef struct
{
	void * pVirtAddr[VPU_DEC_MAX_NUM_MEM];
	VpuDecOutFrameInfo outFrameInfo[VPU_DEC_MAX_NUM_MEM];
}VpuDecoderOutMapInfo;


typedef enum
{
	VPU_COM_STATE_NONE = 0,
    VPU_COM_STATE_LOADED,
	VPU_COM_STATE_OPENED,
	VPU_COM_STATE_PROCESSING_INIT_INFO,
	VPU_COM_STATE_WAIT_FRM,
	VPU_COM_STATE_DO_DEC,
	VPU_COM_STATE_RE_WAIT_FRM,
	VPU_COM_STATE_DO_OUT,
	VPU_COM_STATE_EOS,
}VpuDecoderState;


typedef enum
{
	VPU_COM_CAPABILITY_FILEMODE=0x1,
	VPU_COM_CAPABILITY_TILE=0x2,
	VPU_COM_CAPABILITY_FRMSIZE=0x4,
}VpuDecoderCapability;

class VpuWrapperDec : public VideoDecoderBase {
public:
    VpuWrapperDec(const char* mime);
    virtual ~VpuWrapperDec();
    bool checkIfPostProcessNeeded() override;
    status_t setOutputBuffer(int32_t bufferId);

protected:
    status_t onInit() override;
    status_t onStart() override;//loaded to idle
    status_t onStop() override;//idle to loaded
    status_t onFlush() override;//flush
    status_t onDestroy() override;//free Buffers
    status_t decodeInternal(std::unique_ptr<IMXInputBuffer> input) override;
    status_t DoSetConfig(DecConfig index, void* pConfig) override;
    status_t DoGetConfig(DecConfig index, void* pConfig) override;
    status_t allocateOutputBuffers() override;
    status_t freeOutputBuffers() override;
    bool OutputBufferFull() override;

private:
    VpuMemInfo sMemInfo;				// required by vpu wrapper
    VpuDecoderFrmPoolInfo sFramePoolInfo;// frame pool info for all output frames: decode + post-process
    VpuDecInitInfo sInitInfo;   // seqinit info
    VpuDecHandle nHandle;       // pointer to vpu object
    VpuCodStd eCodingFormat;
    VpuDecoderState eVpuDecoderState;
    VpuVersionInfo sVpuVer;		//vpu version info

    const char* mMime;

    uint32_t nFreeOutBufferCnt;

	bool bInEos;

    int32_t nCurInputId;
    int32_t nSkippedInputId;

    void* hTsHandle;
    bool bResyncTsm;

    std::map<C2String, VpuCodStd> mMime2TypeMap;

	uint32_t nCapability;			//vpu capability
	int32_t nMaxDurationMsThr;	// control the speed of data consumed by decoder: -1 -> no threshold
	int32_t nMaxBufCntThr;		// control the speed of data consumed by decoder: -1 -> no threshold
	int32_t nFrameWidthStride;	//user may register frames with specified width stride
	int32_t nFrameHeightStride;	//user may register frames with specified height stride
	int32_t nFrameMaxCnt;		//user may register frames with specified count
	bool bReorderDisabled;

    bool bHasCodecColorDesc;
    DecIsoColorAspects sDecoderColorDesc;
    DecIsoColorAspects sParserColorDesc;

    bool bHasHdr10StaticInfo;
    DecStaticHDRInfo sHdr10StaticInfo;

    uint32_t nYOffset;
    uint32_t nUVOffset;

    uint32_t nOwnedInputCnt;

    // none -> start -> stopping -> none
    enum {
        FETCH_STATE_NONE = 0,
        FETCH_STATE_START,
        FETCH_STATE_STOPPING,
    };

    uint32_t mFetchState;

    Mutex mLock;
    pthread_t mFetchThread;
    pthread_cond_t pFetchThreadCond;
    void* pFetchThreadMutex;
    int nFetchBufferNum;

	status_t GetInputDataDepthThreshold(int32_t* pDurationThr, int32_t* pBufCntThr);

	/* virtual function implementation */

	status_t FlushFilter();

	void SetDefaultSetting();
    void ResetDecoder();
	status_t ProcessVpuInitInfo();
	status_t ReleaseVpuSource();
    status_t PortFormatChanged(uint32_t nPortIndex);
    bool DefaultOutputBufferNeeded();
    status_t CheckVpuReturnCode(int32_t bufRetCode, int* decodeAgain);
    status_t GetOutputBuffer();
    status_t OpenVpu();
    status_t CreateOneRegisterFrameBuffer(GraphicBlockInfo* info, VpuFrameBuffer * pFrameBuf);
    status_t DecoderRegisterOneFrame(GraphicBlockInfo* info);
    status_t DecoderRegisterAllFrames();

    status_t createFetchThread();
    status_t destroyFetchThread();
    static void *FetchThreadWrapper(void *);
    status_t HandleFetchThread();
};

}  // namespace android
#endif // VPU_WRAPPER_DEC_H
