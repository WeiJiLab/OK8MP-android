/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef VIDEO_ENCODER_BASE_H
#define VIDEO_ENCODER_BASE_H

#include <C2Buffer.h>
#include <C2Work.h>

#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/Mutexed.h>

namespace android {

#define DEFAULT_FRM_WIDTH   176
#define DEFAULT_FRM_HEIGHT  144

typedef enum {
    ENC_CONFIG_BIT_RATE = 0,
    ENC_CONFIG_FRAME_RATE,
    ENC_CONFIG_INTRA_REFRESH,
    ENC_CONFIG_COLOR_FORMAT,
} EncConfig;

typedef struct {
    uint32_t videoSignalTypePresentFlag;
    uint32_t fullRange;
    uint32_t colourDescriptionPresentFlag;
    uint32_t primaries;
    uint32_t transfer;
    uint32_t matrixCoeffs;
} EncIsoColorAspects;

struct LinearBlockInfo {
    enum class State {
        OWNED_BY_COMPONENT = 0,    // Owned by this component.
        OWNED_BY_CLIENT,       // Owned by client.
        OWNED_BY_VPU,          // Owned by vpu
    };

    // The ID of this block used for VPU.
    int32_t mBlockId = -1;
    // The ID of this block used in block pool. It indicates slot index for bufferqueue-backed
    // block pool, and buffer ID of BufferPoolData for bufferpool block pool.
    uint32_t mPoolId = 0;
    State mState = State::OWNED_BY_COMPONENT;
    // Linear block buffer allocated from allocator. The linear block should be owned until
    // it is passed to client.
    std::shared_ptr<C2LinearBlock> mLinearBlock;
    // HAL pixel format used while importing to VPU.
    int mPixelFormat; //HalPixelFormat mPixelFormat;
    uint64_t mTimestamp;
    int mDMABufFd;
    unsigned long mPhysAddr;
    unsigned long mVirtAddr;
    uint32_t mCapacity;
};

typedef struct {
    int eColorFormat;  // input color format
	int nPicWidth;
	int nPicHeight;
	int nWidthStride;
	int nHeightStride;
	int nRotAngle;
	int nFrameRate;
	int nBitRate;			/*unit: bps*/
	int nGOPSize;
	//VpuEncMirrorDirection sMirror;
	int nQuantParam;
	int nEnableAutoSkip;
	int nIDRPeriod;		//for H.264
	int nRefreshIntra;	//IDR for H.264
	bool bEnabledSPSIDR; //SPS/PPS is added for every IDR frame
	int nRcIntraQP;		//0: auto; >0: qp value
	int nProfile;
    int nLevel;

    EncIsoColorAspects sColorAspects;
} EncInputParam;


struct VideoFormat {
    int pixelFormat = 0;//HalPixelFormat::UNKNOWN;
    uint32_t minBufferNum = 0;
    uint32_t width;
    uint32_t height;
    uint32_t bufferNum;
    uint32_t bufferSize;
    bool interlaced;

    VideoFormat() {}
    VideoFormat(int format, uint32_t minNumBuffers, uint32_t width, uint32_t height,
                bool interlaced);
};

struct IMXInputBuffer{
    void* pInputVirt;
    void* pInputPhys;
    int fd;
    int id;
    uint32_t size;
    uint64_t timestamp;
    uint32_t flag;
    bool eos;

    IMXInputBuffer() {}
    IMXInputBuffer(IMXInputBuffer* pInput);
    IMXInputBuffer(void* pVirt, void* pPhys, int fd, int id, uint32_t size, uint64_t timestamp, uint32_t flag, bool eos);
};


class VideoEncoderBase
    : public AHandler, public std::enable_shared_from_this<VideoEncoderBase> {
public:
    // The adaptor client interface. This interface should be implemented in the component side.
    class Client {
    public:
        // Callback to tell client how many and what size of buffers to provide.
        virtual void notifyVideoInfo(VideoFormat *pFormat) {(void)pFormat;}

        // CallBack to tell client to free picture buffers.
        virtual void clearOutputFrameBuffer() {}

        // Callback to deliver decoded pictures ready to be displayed.
        virtual void notifyOutputFrameReady(int32_t outFrameId, uint32_t frameSize,
                                                        uint64_t timestamp, int keyFrame, uint32_t offset) {
            (void)outFrameId;
            (void)frameSize;
            (void)timestamp;
            (void)keyFrame;
            (void)offset;
        }

        // Callback to notify that decoder has decoded the end of the bitstream buffer with specified ID.
        virtual void notifyInputBufferUsed(int32_t input_id) {(void)input_id;}

        // Callback to tell client how many and what size of buffers to provide.
        virtual void fetchOutputBuffer(int* bufferId,
                                      unsigned long* pPhysAddr,
                                      unsigned long* pVirtAddr,
                                      uint32_t* capacity) {
            (void)bufferId;
            (void)pPhysAddr;
            (void)pVirtAddr;
            (void)capacity;
        }

        // Flush completion callback.
        virtual void notifyFlushDone() {}

        // Reset completion callback.
        virtual void notifyResetDone() {}

        // Callback to notify about errors. Note that errors in initialize() will not be reported
        // here, instead of by its returned value.
        virtual void notifyError(status_t err) {(void)err;}

        virtual void notifyEos() {}
    protected:
        virtual ~Client() = default;
    };

    VideoEncoderBase();
    virtual ~VideoEncoderBase();

    status_t init(Client* client/*const std::shared_ptr<C2BlockPool> &pool*/);//load componnent and init parameters
    status_t RegisterLooper();
    status_t stop();//idle to loaded
    status_t flush();//flush
    status_t destroy();//free Buffers
    status_t setConfig(EncConfig index, void* pConfig);
    status_t getConfig(EncConfig index, void* pConfig);
    virtual status_t DoSetConfig(EncConfig index, void* pConfig) {return OK;}
    virtual status_t DoGetConfig(EncConfig index, void* pConfig) {return OK;}
    virtual status_t getCodecData(uint8_t** pCodecData, uint32_t* size);
    virtual bool checkIfPreProcessNeeded(int pixelFormat);

    //status_t setClient(Client* client);
    status_t setLinearBlockPool(const std::shared_ptr<C2BlockPool>& pool);
    status_t queueInput(IMXInputBuffer * pInBuffer);
    status_t importOutput(std::shared_ptr<C2FrameData> outputBuffer);
    virtual status_t importOutputBuffers(std::vector<LinearBlockInfo> buffers);
    LinearBlockInfo* getLinearBlockById(int32_t blockId);
    LinearBlockInfo* getLinearBlockByFd(int fd);
    LinearBlockInfo* getLinearBlockByPhysAddr(unsigned long physAddr);
    LinearBlockInfo* getFreeLinearBlock();

    void onOutputBufferDone();
    void returnOutputBufferToEncoder(int32_t blockId);
    virtual void initEncInputParamter(EncInputParam *pInPara) {(void)pInPara;}

protected:

    uint32_t mWidth;
    uint32_t mHeight;
    bool bInputEos;
    bool bOutputEos;

    VideoFormat mInputFormat;
    VideoFormat mOutputFormat;

    void onMessageReceived(const sp<AMessage> &msg) override;
    virtual status_t onInit();
    virtual status_t onStop();//idle to loaded
    virtual status_t onFlush();//flush
    virtual status_t onDestroy();//free Buffers
    virtual status_t encodeInternal(std::unique_ptr<IMXInputBuffer> input);
    virtual status_t onOutputBufferReturned(int32_t bufferId);
    virtual status_t getOutputVideoInfo(VideoFormat * info);

    status_t FetchOutputBuffer(int* bufferId,
                                      int* fd,
                                      unsigned long* pPhysAddr,
                                      unsigned long* pVirtAddr,
                                      uint32_t* capacity);

    void ClearOutputFrameBuffer();
    void NotifyOutputFrameReady(int32_t outFrameId, uint32_t frameSize, uint64_t timestamp, int keyFrame, uint32_t offset);
    void NotifyInputBufferUsed(int32_t input_id);
    void NotifyFlushDone();
    void NotifyResetDone();
    void NotifyEOS();
    void NotifyError(status_t err);
    enum {
        kInputBufferCount = 8,
        kInputBufferSizeFor1080p = 1024 * 1024,
        // Input bitstream buffer size for up to 4k streams.
        kInputBufferSizeFor4k = 4 * kInputBufferSizeFor1080p,
        kDefaultOutputBufferCount = 8,
    };
private:
    enum {
        kWhatEncode,
        kWhatInit,
        kWhatFlush,
        kWhatStop,
        kWhatReset,
        kWhatDestroy,
    };


    sp<ALooper> mLooper;
    Client* mClient;

    typedef std::list<std::unique_ptr<IMXInputBuffer>> InputBufferQueue;
    Mutexed<InputBufferQueue> mInputQueue;
    std::shared_ptr<C2BlockPool> mBlockPool;
    std::vector<LinearBlockInfo> mLinearBlocks;
    IMXInputBuffer mCurInputBuffer;

    uint64_t nOutBufferUsage;

    status_t allocateOutputBuffers();
    status_t freeOutputBuffers();
    status_t appendOutputBuffer(std::shared_ptr<C2LinearBlock> block, uint32_t* capacity);
};

VideoEncoderBase * CreateVideoEncoderInstance(const char* mime);

}
#endif // VIDEO_ENCODER_BASE_H
