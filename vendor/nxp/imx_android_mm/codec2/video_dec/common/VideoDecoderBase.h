/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef VIDEO_DECODER_BASE_H
#define VIDEO_DECODER_BASE_H

#include <C2Buffer.h>
#include <C2Work.h>

#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/Mutexed.h>

namespace android {

#define DEFAULT_FRM_WIDTH   176
#define DEFAULT_FRM_HEIGHT  144

typedef enum {
    DEC_CONFIG_INPUT_FORMAT = 0,
    DEC_CONFIG_OUTPUT_FORMAT,
    DEC_CONFIG_OUTPUT_DELAY,
    DEC_CONFIG_VC1_SUB_FORMAT,
    DEC_CONFIG_HAL_PIXEL_FORMAT,
    DEC_CONFIG_HDR10_STATIC_INFO,
    DEC_CONFIG_COLOR_ASPECTS,
    DEC_CONFIG_LOW_LATENCY,
} DecConfig;

typedef struct {
    uint32_t nSize;
    uint16_t mR[2]; // display primary 0
    uint16_t mG[2]; // display primary 1
    uint16_t mB[2]; // display primary 2
    uint16_t mW[2]; // white point
    uint16_t mMaxDisplayLuminance; // in cd/m^2
    uint16_t mMinDisplayLuminance; // in 0.0001 cd/m^2
    uint16_t mMaxContentLightLevel; // in cd/m^2
    uint16_t mMaxFrameAverageLightLevel; // in cd/m^2
} DecStaticHDRInfo;

typedef struct {
    uint32_t colourPrimaries;
    uint32_t transferCharacteristics;
    uint32_t matrixCoeffs;
    uint32_t fullRange;
} DecIsoColorAspects;

struct GraphicBlockInfo {
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
    // Graphic block buffer allocated from allocator. The graphic block should be owned until
    // it is passed to client.
    std::shared_ptr<C2GraphicBlock> mGraphicBlock;
    // HAL pixel format used while importing to VPU.
    int mPixelFormat; //HalPixelFormat mPixelFormat;
    uint64_t mTimestamp;
    int mDMABufFd;
    unsigned long mPhysAddr;
    unsigned long mVirtAddr;
    uint32_t mCapacity;
};

struct VideoRect {
	uint32_t left;
	uint32_t top;
	uint32_t right;
	uint32_t bottom;
    VideoRect() {}
    VideoRect(VideoRect &rect);
    VideoRect(uint32_t left, uint32_t right, uint32_t top, uint32_t bottom);
};

struct VideoFormat {
    int pixelFormat = 0;//HalPixelFormat::UNKNOWN;
    uint32_t minBufferNum = 0;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bufferNum;
    uint32_t bufferSize;
    VideoRect rect;
    bool interlaced;

    VideoFormat() {}
    VideoFormat(int format, uint32_t minNumBuffers, uint32_t width, uint32_t height,
                VideoRect &rect, bool interlaced);
};


class VideoDecoderBase
    : public AHandler, public std::enable_shared_from_this<VideoDecoderBase> {
public:
    // The adaptor client interface. This interface should be implemented in the component side.
    class Client {
    public:
        // Callback to tell client how many and what size of buffers to provide.
        virtual void notifyVideoInfo(VideoFormat *pFormat) {(void)pFormat;}

        // CallBack to tell client to free picture buffers.
        virtual void clearPictureBuffer() {}

        // Callback to deliver decoded pictures ready to be displayed.
        virtual void notifyPictureReady(int32_t pictureId, uint64_t timestamp) {(void)pictureId; (void)timestamp;}

        // Callback to notify that decoder has decoded the end of the bitstream buffer with specified ID.
        virtual void notifyInputBufferUsed(int32_t input_id) {(void)input_id;}

        // Callback to notify that decode skip this buffer
        virtual void notifySkipInputBuffer(int32_t input_id) {(void)input_id;}

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

    VideoDecoderBase();
    virtual ~VideoDecoderBase();

    status_t init(Client* client/*const std::shared_ptr<C2BlockPool> &pool*/);//load componnent and init parameters
    status_t start();//loaded to idle
    status_t stop();//idle to loaded
    status_t flush();//flush
    status_t destroy();//free Buffers
    status_t setConfig(DecConfig index, void* pConfig);
    status_t getConfig(DecConfig index, void* pConfig);
    virtual bool checkIfPostProcessNeeded() {return false;}

    status_t setGraphicBlockPool(const std::shared_ptr<C2BlockPool> &pool);
    status_t queueInput(uint8_t *pInBuf, uint32_t size, uint64_t timestamp, uint32_t flags, int fd, int id);
    status_t fetchOutputBuffer();
    virtual status_t importOutputBuffers(std::vector<GraphicBlockInfo> buffers);
    GraphicBlockInfo* getGraphicBlockById(int32_t blockId);
    GraphicBlockInfo* getGraphicBlockByPhysAddr(unsigned long physAddr);
    GraphicBlockInfo* getFreeGraphicBlock();
    status_t removeGraphicBlockById(int32_t blockId);
    void returnOutputBufferToDecoder(int32_t blockId);
    status_t queueOutput(int32_t blockId);
protected:

    enum {
        kInputBufferCount = 8,
        kInputBufferSizeFor1080p = 1024 * 1024,
        // Input bitstream buffer size for up to 4k streams.
        kInputBufferSizeFor4k = 4 * kInputBufferSizeFor1080p,
        kDefaultOutputBufferCount = 8,
    };

    struct IMXInputBuffer{
        void* pInBuffer;
        int fd;
        int id;
        uint32_t size;
        uint64_t timestamp;
        bool eos;
        bool csd;

        IMXInputBuffer() {}
        IMXInputBuffer(void* pBuffer, int fd, int id, uint32_t size, uint64_t ts, bool eos, bool codecdata);
    };

    bool bInputEos;
    bool bOutputEos;
    bool bAdaptiveMode;
    bool bSecureMode;
    bool bReceiveError;

    bool bCodecDataReceived;
    bool bCodecDataQueued;

    VideoFormat mInputFormat;
    VideoFormat mOutputFormat;

    uint64_t nOutBufferUsage;

    uint8_t* pCodecDataBuf;
    uint32_t nCodecDataLen;

    std::vector<GraphicBlockInfo> mGraphicBlocks;

    void onMessageReceived(const sp<AMessage> &msg) override;
    virtual status_t onInit();
    virtual status_t onStart();//loaded to idle
    virtual status_t onStop();//idle to loaded
    virtual status_t onFlush();//flush
    virtual status_t onDestroy();//free Buffers
    virtual status_t decodeInternal(std::unique_ptr<IMXInputBuffer> input);

    virtual status_t DoSetConfig(DecConfig index, void* pConfig) {return OK;}
    virtual status_t DoGetConfig(DecConfig index, void* pConfig) {return OK;}

    virtual status_t allocateOutputBuffers() {return OK;}
    virtual status_t freeOutputBuffers() {return OK;}

    virtual bool OutputBufferFull() {return false;}

    status_t outputFormatChanged();
    int getBlockPoolAllocatorId();

    void ClearPictureBuffer();
    void NotifyPictureReady(int32_t pictureId, uint64_t timestamp);
    void NotifyInputBufferUsed(int32_t input_id);
    void NotifySkipInputBuffer(int32_t input_id);
    void NotifyFlushDone();
    void NotifyResetDone();
    void NotifyEOS();
    void NotifyError(status_t err);

private:
    enum {
        kWhatDecode,
        kWhatInit,
        kWhatStart,
        kWhatFlush,
        kWhatStop,
        kWhatReset,
        kWhatDestroy,
        kWhatQueueOutput,
    };

    sp<ALooper> mLooper;

    Client* mClient;

    Mutex mGBLock; // graphic buffer lock

    typedef std::list<std::unique_ptr<IMXInputBuffer>> InputBufferQueue;
    Mutexed<InputBufferQueue> mInputQueue;
    std::shared_ptr<C2BlockPool> mBlockPool;

    bool bOutputFmtChangedPending;
    bool bReleasingDecoder;

    status_t onOutputFormatChanged();
    status_t appendOutputBuffer(std::shared_ptr<C2GraphicBlock> block, int32_t* blockId);
};

VideoDecoderBase * CreateVideoDecoderInstance(const char* mime);
}
#endif // VIDEO_DECODER_BASE_H
