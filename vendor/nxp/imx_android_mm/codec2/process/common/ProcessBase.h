/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */


#ifndef PROCESS_BASE_H
#define PROCESS_BASE_H

#include <queue>
#include <C2Buffer.h>
#include <C2Work.h>

#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/Mutexed.h>

namespace android {

#define DEFAULT_PROCESS_BUFFER_NUM 3
#define MAX_PROCESS_BUFFER_NUM 32

typedef struct {
    int nNum;
    int id[MAX_PROCESS_BUFFER_NUM];
    unsigned long virtAddr[MAX_PROCESS_BUFFER_NUM];
    unsigned long phyAddr[MAX_PROCESS_BUFFER_NUM];
    unsigned long fd[MAX_PROCESS_BUFFER_NUM];
    uint32_t size[MAX_PROCESS_BUFFER_NUM];
    uint32_t flag[MAX_PROCESS_BUFFER_NUM];
} PROCESS_MEM_INFO;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t stride;
    uint32_t bufferSize;
    uint32_t flag;
    bool interlaced;
} PROCESSBASE_FORMAT;
#define PROCESSBASE_FORMAT_FLAG_NV12    (1)

typedef struct {
    int32_t mBlockId = -1;
    int mFd;
    unsigned long mPhysAddr;
    unsigned long mVirtAddr;
    uint32_t mCapacity;
    uint64_t mTimestamp;
    std::shared_ptr<C2LinearBlock> mLinearBlock;
    std::shared_ptr<C2LinearBlock> mLinearBlock2;
    std::shared_ptr<C2GraphicBlock> mGraphicBlock;

} ProcessBlockInfo;

typedef enum {
    PROCESS_CONFIG_INPUT_FORMAT = 0,
    PROCESS_CONFIG_OUTPUT_FORMAT,
    PROCESS_CONFIG_INTRA_REFRESH,
} ProcessConfig;

class ProcessBase : public AHandler {
public:
    class Client{
    public:
        virtual status_t fetchProcessBuffer(int *bufferId, unsigned long *phys);
        virtual status_t notifyProcessInputUsed(int inputId);
        virtual status_t notifyProcessDone(int outputId, uint64_t timestamp, uint32_t flag);
        virtual status_t notifyProcessOutputClear();
        virtual status_t notifyProcessFlushDone();
        virtual status_t notifyProcessResetDone();
        virtual void notifyProcessError();
        virtual void notifyProcessEos();
    protected:
            virtual ~Client() = default;
    };


    ProcessBase();
    virtual ~ProcessBase();

    status_t init(Client* client, const std::shared_ptr<C2BlockPool>& pool);
    status_t destroy();
    status_t queueInput(void* pVirt, void* pPhys, uint32_t size, uint64_t timestamp, uint32_t flags, int fd, int id);
    status_t flush();
    status_t reset();
    status_t stop();
    status_t start();
    status_t importOutputBuffers();
    status_t outputBufferReturned(int outputId);
    ProcessBlockInfo* getProcessBlockById(int blockId);
    status_t setConfig(ProcessConfig index, void* pConfig);
    status_t getConfig(ProcessConfig index, void* pConfig);
    status_t videoFormatChanged(PROCESSBASE_FORMAT *newFmt);

protected:
    struct PPInputBuffer{
        unsigned long physAddr;
        int id;
        uint32_t size;
        uint64_t timestamp;
        bool eos;

        PPInputBuffer() {}
        PPInputBuffer(unsigned long phys, int id, uint32_t size, uint64_t ts, bool eos);
    };

    enum {
        UNINITIALIZED,
        STOPPED,
        RUNNING,
        STOPPING,
        FLUSHING,
    };

    PROCESSBASE_FORMAT sInFormat;
    PROCESSBASE_FORMAT sOutFormat;

    PROCESS_MEM_INFO sInMemInfo;
    PROCESS_MEM_INFO sOutMemInfo;

    std::queue<int> mInputQueue;
    std::queue<int> mOutputQueue;

    std::shared_ptr<C2BlockPool> mBlockPool;

    int mState;

    bool mAsync;
    bool bOutputEos;
    uint64_t nInputCnt;
    uint64_t nOutputCnt;

    // preprocess use AllocateProcessBuffers to allocate linear buffer
    status_t AllocateProcessBuffers(uint32_t num);
    status_t AllocateProcessBuffers(uint32_t num, uint32_t num_plane, uint32_t *buf_size);
    // postprocess use FetchProcessBuffer to fetch graphic buffer
    status_t FetchProcessBuffer(int *bufferId, unsigned long *phys);
    status_t FreeProcessBuffers();
    status_t onFlush();
    status_t onReset();
    status_t clearInputBuffers();
    status_t clearOutputBuffers();

    status_t ProcessFrameSet(PROCESS_MEM_INFO* pMem, unsigned long nPhys, int id, int fd, uint32_t flag, int* pIndex);
    status_t ProcessFrameClear(PROCESS_MEM_INFO* pMem, int nIndex);
    status_t ProcessFrameGetNode(PROCESS_MEM_INFO* pMem, int nIndex, unsigned long* pPhys, int* nId, int * out_fd, uint32_t *flag );
    bool ProcessFrameValid(PROCESS_MEM_INFO* pMem, int nIndex);

    virtual status_t onInit() {return OK;}
    virtual status_t onConfig() {return OK;}
    virtual status_t onDestroy() {return OK;}
    void onMessageReceived(const sp<AMessage> &msg) override;
    virtual status_t onOutputBufferReturned(int outputId);
    virtual status_t onProcess() {return OK;}
    virtual status_t onVideoResChanged() {return OK;}
    virtual status_t onStart() {return OK;}
    virtual status_t onStop() {return OK;}

    status_t NotifyProcessInputUsed(int inputId);
    status_t NotifyProcessDone(int outputId, uint32_t flag);
    status_t NotifyProcessEos();
    virtual status_t DoSetConfig(ProcessConfig index, void* pConfig){return OK;}
    virtual status_t DoGetConfig(ProcessConfig index, void* pConfig){return OK;}

    bool getDefaultG2DLib(char *libName, int size);
    status_t Post_ProcessMessage();
    Mutex mLock;

private:

    enum {
        kWhatInit,
        kWhatProcess,
        kWhatReturnOutBuf,
        kWhatFlush,
        kWhatResChanged,
        kWhatStop,
        kWhatDestroy,
        kWhatStart,
    };

    sp<ALooper> mLooper;
    Client* mClient;

    bool bRunning;
    bool bInputEos;
    bool bResChanged;
    bool bAllocateBuffers;

    typedef std::list<std::unique_ptr<PPInputBuffer>> InputBufferQueue;
    Mutexed<InputBufferQueue> mInputBufferQueue;
    std::queue<uint64_t> mTimestampQueue;


    std::vector<ProcessBlockInfo> mProcessBlocks;
    uint32_t nDebugFlag;

    void ParseVpuLogLevel();
    void dumpInputBuffer(void* buf, uint32_t size);
    void dumpOutputBuffer(void* buf, uint32_t size);
};

ProcessBase * CreatePreProcessInstance();
ProcessBase * CreatePostProcessInstance();

}

#endif    // PROCESS_BASE_H
