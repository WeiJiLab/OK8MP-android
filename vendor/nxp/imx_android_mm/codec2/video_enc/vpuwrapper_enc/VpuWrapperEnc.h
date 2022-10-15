/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef VPU_WRAPPER_ENC_H
#define VPU_WRAPPER_ENC_H

#include <map>

#include"VideoEncoderBase.h"
#include"vpu_wrapper.h"

namespace android {
#define VPU_ENC_MAX_NUM_MEM (36)
typedef struct
{
	//virtual mem info
	int nVirtNum;
	unsigned long virtMem[VPU_ENC_MAX_NUM_MEM];

	//phy mem info
	int nPhyNum;
	unsigned long phyMem_virtAddr[VPU_ENC_MAX_NUM_MEM];
	unsigned long phyMem_phyAddr[VPU_ENC_MAX_NUM_MEM];
	int phyMem_cpuAddr[VPU_ENC_MAX_NUM_MEM];
    int phyMem_bufferId[VPU_ENC_MAX_NUM_MEM];
	uint32_t phyMem_size[VPU_ENC_MAX_NUM_MEM];
}VpuEncoderMemInfo;

typedef enum {
    VPU_ENC_COM_STATE_NONE = 0,
    VPU_ENC_COM_STATE_WAIT_FRM,
    VPU_ENC_COM_STATE_LOADED,
    VPU_ENC_COM_STATE_DO_DEC,
    VPU_ENC_COM_STATE_DO_OUT,
} VpuEncoderState;

typedef struct {
    VpuCodStd eFormat;
    int ePixelFormat;
    int nPicWidth;
    int nPicHeight;
    int nWidthStride;
    int nHeightStride;
    int nRotAngle;
    int nFrameRate;
    int nBitRate;           /*unit: bps*/
    int nGOPSize;
    int nChromaInterleave;
    VpuEncMirrorDirection sMirror;
    int nQuantParam;

    int nEnableAutoSkip;
    int nIDRPeriod;     //for H.264
    int nRefreshIntra;  //IDR for H.264
    int nIntraFreshNum;
    bool bEnabledSPSIDR; //SPS/PPS is added for every IDR frame
    int nRcIntraQP;     //0: auto; >0: qp value

    EncIsoColorAspects sColorInfo;
} VpuEncInputParam;


class VpuWrapperEnc : public VideoEncoderBase {
public:
    VpuWrapperEnc(const char* mime);
    virtual ~VpuWrapperEnc();

    status_t DoSetConfig(EncConfig index, void* pConfig) override;
    status_t DoGetConfig(EncConfig index, void* pConfig) override;
    status_t getCodecData(uint8_t** pCodecData, uint32_t* size) override;
    bool checkIfPreProcessNeeded(int pixelFormat) override;
    void initEncInputParamter(EncInputParam *pInPara) override;

protected:
    status_t onInit() override;
    status_t onStop() override;//idle to loaded
    status_t onFlush() override;//flush
    status_t onDestroy() override;//free Buffers
    status_t encodeInternal(std::unique_ptr<IMXInputBuffer> input) override;
    status_t getOutputVideoInfo(VideoFormat * info) override;
    status_t setOutputBuffer(int bufferId, unsigned long pPhyAddr, unsigned long pVirtAddr, uint32_t capacity);

private:
    VpuMemInfo sMemInfo;
    VpuEncoderMemInfo sEncOutFrameInfo;
    VpuEncInitInfo sEncInitInfo;    // seqinit info
    VpuEncHandle nHandle;       // pointer to vpu object

    const char* mMime;

    void* pInBufferPhys;
    void* pInBufferVirt;
    int32_t nInSize;
    void* pOutBufferPhys;
    void* poutBufferVirt;
    int32_t nOutSize;
    int32_t nOutGOPFrameCnt;    // used for H.264: [1,2,...,GOPSize]

    VpuColourDesc sColorDesc;
    VpuEncoderState eVpuEncoderState;
    VpuEncInputParam sVpuEncInputPara;

	bool bInEos;
    bool bStarted;

    std::unique_ptr<IMXInputBuffer> mCurInputBuffer;
    int32_t nCurInputId;

    void* hTsHandle;

    std::map<const char*, VpuCodStd> mMime2TypeMap;

    uint8_t* pCodecdataBuf;
    uint32_t nCodecDataLen;


	/* virtual function implementation */

	status_t FlushFilter();

	void SetDefaultSetting();
	status_t ReleaseVpuSource();
    bool DefaultOutputBufferNeeded();
    status_t GetOutputBuffer();
    status_t OpenVpu();
};

}  // namespace android
#endif // VPU_WRAPPER_ENC_H
