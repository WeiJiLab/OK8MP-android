/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "ProcessBase"

#include <utils/Log.h>
#include <media/stagefright/foundation/AMessage.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <cutils/properties.h>

#include <C2Config.h>
#include <C2Debug.h>
#include <C2PlatformSupport.h>

#include "graphics_ext.h"
#include "Memory.h"
#include "ProcessBase.h"
#include "IonAllocator.h"

#define PROCESS_BASE_API_TRACE
#ifdef PROCESS_BASE_API_TRACE
#define PP_BASE_API_TRACE ALOGV
#else
#define PP_BASE_API_TRACE(...)
#endif

#define PROCESS_BASE_DEBUG
#ifdef PROCESS_BASE_DEBUG
#define PP_BASE_LOG ALOGV
#else
#define PP_BASE_LOG(...)
#endif

#define PP_BASE_ERR_LOG ALOGE

#if defined(__LP64__)
#define G2DENGINE "/vendor/lib64/libg2d"
#else
#define G2DENGINE "/vendor/lib/libg2d"
#endif

namespace android {

static void Reply(const sp<AMessage> &msg, int32_t *err = nullptr) {
    sp<AReplyToken> replyId;
    CHECK(msg->senderAwaitsResponse(&replyId));
    sp<AMessage> reply = new AMessage;
    if (err) {
        reply->setInt32("err", *err);
    }
    reply->postReply(replyId);
}

ProcessBase::PPInputBuffer::PPInputBuffer(
                    unsigned long phys,
                    int id,
                    uint32_t size,
                    uint64_t ts,
                    bool eos)
    : physAddr(phys),
      id(id),
      size(size),
      timestamp(ts),
      eos(eos){
}

ProcessBase::ProcessBase()
    : mLooper(new ALooper),
      mClient(nullptr),
      bInputEos(false){

    mLooper->setName("ProcessBase");
    mLooper->start(false, false, ANDROID_PRIORITY_VIDEO);

    memset(&sInFormat, 0, sizeof(PROCESSBASE_FORMAT));
    memset(&sOutFormat, 0, sizeof(PROCESSBASE_FORMAT));
    memset(&sInMemInfo, 0, sizeof(PROCESS_MEM_INFO));
    memset(&sOutMemInfo, 0, sizeof(PROCESS_MEM_INFO));
    mState = UNINITIALIZED;
    mAsync = false;
    bResChanged = false;
    bAllocateBuffers = false;
    nInputCnt = 0;
    nOutputCnt = 0;
}

ProcessBase::~ProcessBase() {
    if (mLooper != nullptr) {
        mLooper->unregisterHandler(id());
        mLooper->stop();
    }
}

bool ProcessBase::getDefaultG2DLib(char *libName, int size) {
    char value[PROPERTY_VALUE_MAX];

    if((libName == NULL)||(size < strlen(G2DENGINE) + strlen(".so")))
        return false;

    memset(libName, 0, size);
    property_get("vendor.imx.default-g2d", value, "");
    if(strcmp(value, "") == 0) {
        PP_BASE_ERR_LOG("No g2d lib available to be used!");
        return false;
    } else {
        strncpy(libName, G2DENGINE, strlen(G2DENGINE));
        strcat(libName, "-");
        strcat(libName, value);
        strcat(libName, ".so");
    }
    PP_BASE_LOG("Default g2d lib: %s", libName);
    return true;
}

status_t ProcessBase::init(Client* client, const std::shared_ptr<C2BlockPool>& pool) {
    if (client == nullptr || pool.get() == nullptr) {
        PP_BASE_ERR_LOG("init get nullptr ! \n");
        return BAD_VALUE;
    }

    mBlockPool = pool;
    mClient = client;
    (void)mLooper->registerHandler(this);

    nInputCnt = 0;
    nOutputCnt = 0;

    sp<AMessage> reply;
    (new AMessage(kWhatInit, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));

    if (err == OK)
        mState = RUNNING;

    return err;
}

status_t ProcessBase::setConfig(ProcessConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    status_t ret = OK;

    switch (index) {
        case PROCESS_CONFIG_INPUT_FORMAT: {
            memcpy(&sInFormat, pConfig, sizeof(PROCESSBASE_FORMAT));
            break;
        }

        default: {
            ret = DoSetConfig(index, pConfig);
            break;
        }
    }
    return ret;
}

status_t ProcessBase::getConfig(ProcessConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    status_t ret = OK;

    switch (index) {
        case PROCESS_CONFIG_OUTPUT_FORMAT: {
            memcpy(pConfig, &sOutFormat, sizeof(PROCESSBASE_FORMAT));
            break;
        }
        default: {
            ret = DoGetConfig(index, pConfig);
            break;
        }
    }
    return ret;
}

status_t ProcessBase::destroy() {

    sp<AMessage> reply;
    sp<AMessage> msg = new AMessage(kWhatDestroy, this);
    status_t err = msg->postAndAwaitResponse(&reply);
    if (err == OK && reply != NULL) {
        CHECK(reply->findInt32("err", &err));
    }

    return err;
}

status_t ProcessBase::queueInput(void* pVirt, void* pPhys, uint32_t size, uint64_t timestamp,
                                        uint32_t flags, int fd, int id) {
    Mutex::Autolock autoLock(mLock);
    bool empty = mInputQueue.empty();
    // if eos buffer is empty, notify eos right now, otherwise process data then notify eos.
    bInputEos = ((flags & C2FrameData::FLAG_END_OF_STREAM) != 0);

    int index;
    ProcessFrameSet(&sInMemInfo, (unsigned long)pPhys, id, fd, flags, &index);
    mInputQueue.push(index);
    mTimestampQueue.push(timestamp);
    ALOGV("mTimestampQueue.push %lld mInputQueue index=%d,fd=%d,size=%zu,id=%d",
        (long long)timestamp,index,fd,mInputQueue.size(),id);
    nInputCnt ++;

    if(mState != RUNNING)
        mState = RUNNING;

    if(empty || bInputEos)
        (new AMessage(kWhatProcess, this))->post();

    return OK;
}

status_t ProcessBase::flush() {

    if(UNINITIALIZED == mState){
        return OK;
    }

    int32_t err;
    sp<AMessage> reply;
    (new AMessage(kWhatFlush, this))->postAndAwaitResponse(&reply);
    CHECK(reply->findInt32("err", &err));
    return err;
}

status_t ProcessBase::stop() {
    sp<AMessage> reply;
    (new AMessage(kWhatStop, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}
status_t ProcessBase::start() {
    nInputCnt = 0;
    nOutputCnt = 0;

    sp<AMessage> reply;
    (new AMessage(kWhatStart, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}
status_t ProcessBase::reset() {
    return OK;
}

status_t ProcessBase::importOutputBuffers() {
    return OK;
}

status_t ProcessBase::outputBufferReturned(int outputId) {
    sp<AMessage> msg = (new AMessage(kWhatReturnOutBuf, this));
    msg->setInt32("output-id", outputId);
    msg->post();
    return OK;
}

ProcessBlockInfo* ProcessBase::getProcessBlockById(int blockId) {
    if (blockId < 0) {
        PP_BASE_ERR_LOG("%s line %d: invalid fd=%d", __FUNCTION__, __LINE__, blockId);
        return nullptr;
    }
    auto blockIter = std::find_if(mProcessBlocks.begin(), mProcessBlocks.end(),
                                  [blockId](const ProcessBlockInfo& pb) {
                                      return pb.mBlockId == blockId;
                                  });

    if (blockIter == mProcessBlocks.end()) {
        PP_BASE_ERR_LOG("%s line %d: failed: id=%d", __FUNCTION__, __LINE__, blockId);
        return nullptr;
    }
    return &(*blockIter);
}

status_t ProcessBase::AllocateProcessBuffers(uint32_t num) {
    uint32_t i;
    C2MemoryUsage usage(C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE);

    for (i = 0; i < num; i++) {
        std::shared_ptr<C2LinearBlock> outBlock;
        ALOGI("fetchLinearBlock: size %d", sOutFormat.bufferSize);
        c2_status_t err = mBlockPool->fetchLinearBlock(sOutFormat.bufferSize, usage, &outBlock);
        if (err != C2_OK) {
            PP_BASE_ERR_LOG("fetchLinearBlock for Output failed with status %d", err);
            return BAD_VALUE;
        }

        fsl::IonAllocator * pIonAllocator = fsl::IonAllocator::getInstance();
        int ret;
        int fd = outBlock->handle()->data[0];
        uint64_t pPhys = 0;
        int index;

        ret = pIonAllocator->getPhys(fd, sOutFormat.bufferSize, (uint64_t&)pPhys);
        if (ret != 0) {
            PP_BASE_ERR_LOG("Ion get physical address failed, fd %d", fd);
            return BAD_VALUE;
        }

        ProcessBlockInfo info;
        memset(&info, 0, sizeof(ProcessBlockInfo));
        info.mBlockId = i;
        info.mCapacity = sOutFormat.bufferSize;
        info.mFd = fd;
        info.mPhysAddr = pPhys;
        info.mLinearBlock = std::move(outBlock);
        ProcessFrameSet(&sOutMemInfo, pPhys, i, fd, 0, &index);
        mOutputQueue.push(index);
        mProcessBlocks.push_back(std::move(info));

        if(!bAllocateBuffers)
            bAllocateBuffers = true;
    }

    return OK;
}
status_t ProcessBase::AllocateProcessBuffers(uint32_t num, uint32_t num_plane, uint32_t *buf_size) {
    uint32_t i;
    C2MemoryUsage usage(C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE);

    if(num_plane != 2 || buf_size == NULL || buf_size[0] == 0 || buf_size[1] == 0)
        return BAD_VALUE;
    for (i = 0; i < num; i++) {
        std::shared_ptr<C2LinearBlock> outBlock;
        std::shared_ptr<C2LinearBlock> outBlock2;
        ALOGI("fetchLinearBlock: size0=%d, size1=%d",buf_size[0], buf_size[1], sOutFormat.bufferSize);
        c2_status_t err = mBlockPool->fetchLinearBlock(buf_size[0], usage, &outBlock);
        if (err != C2_OK) {
            PP_BASE_ERR_LOG("fetchLinearBlock for Output failed with status %d", err);
            return BAD_VALUE;
        }
        int ret;
        int fd;
        uint64_t pPhys = (int)(outBlock->handle()->data[0]);
        int index;

        err = mBlockPool->fetchLinearBlock(buf_size[1], usage, &outBlock2);
        if (err != C2_OK) {
            PP_BASE_ERR_LOG("fetchLinearBlock for Output failed with status %d", err);
            return BAD_VALUE;
        }
        fd = outBlock2->handle()->data[0];
        ProcessBlockInfo info;
        memset(&info, 0, sizeof(ProcessBlockInfo));
        info.mBlockId = i;
        info.mCapacity = sOutFormat.bufferSize;
        info.mFd = fd;
        info.mPhysAddr = pPhys;
        info.mLinearBlock = std::move(outBlock);
        info.mLinearBlock2 = std::move(outBlock2);
        ProcessFrameSet(&sOutMemInfo, pPhys, i, fd, 0, &index);
        mOutputQueue.push(index);
        mProcessBlocks.push_back(std::move(info));
        ALOGV("AllocateProcessBuffers fd0=%d,fd1=%d",(int)pPhys, fd);

        if(!bAllocateBuffers)
            bAllocateBuffers = true;
    }
    return OK;
}
status_t ProcessBase::FreeProcessBuffers() {
    ALOGV("FreeProcessBuffers");
    for (auto& info : mProcessBlocks) {
        //no need to close fd
        #if 0
        if (info.mVirtAddr > 0 && info.mCapacity > 0)
            munmap((void*)info.mVirtAddr, info.mCapacity);
        ALOGV("FreeProcessBuffers fd=%d,fd2=%d", info.mFd, (int)info.mPhysAddr);
        if(info.mFd > 0)
            close(info.mFd);
        if(info.mPhysAddr > 0 && 0 == info.mVirtAddr){
            int fd2 = (int)info.mPhysAddr;
            close(fd2);
        }
        #endif

        if(info.mLinearBlock != NULL){
            info.mLinearBlock.reset();
            ALOGV("mLinearBlock.reset");
        }
        if(info.mLinearBlock2 != NULL){
            info.mLinearBlock2.reset();
            ALOGV("mLinearBlock2.reset");
        }
        if(info.mGraphicBlock != NULL){
            info.mGraphicBlock.reset();
            ALOGV("mGraphicBlock.reset");
        }

    }

    mProcessBlocks.clear();
    return OK;
}

status_t ProcessBase::onFlush() {
    ALOGV("ProcessBase::onFlush");

    return OK;
}

status_t ProcessBase::onReset() {
    return OK;
}

status_t ProcessBase::clearInputBuffers() {

    int inIndex;
    int inId;
    int inFd;
    unsigned long inPhys;
    uint32_t inFlag= 0;
    Mutex::Autolock autoLock(mLock);

    while (!mInputQueue.empty()) {
        inIndex = mInputQueue.front();
        ProcessFrameGetNode(&sInMemInfo, inIndex, &inPhys, &inId, &inFd, &inFlag);
        NotifyProcessInputUsed(inId);
        ProcessFrameClear(&sInMemInfo,inIndex);
        ALOGV("clearInputBuffers inIndex=%d",inIndex);
        mInputQueue.pop();
    }

    while(!mTimestampQueue.empty()) {
        uint64_t timestamp = mTimestampQueue.front();
        mTimestampQueue.pop();
        ALOGV("clear ts=%lld in kWhatFlush",(long long)timestamp);
    }

    return OK;
}
status_t ProcessBase::clearOutputBuffers() {

    int outIndex;
    int outId;
    int outFd;
    unsigned long outPhys;
    uint32_t outFlag= 0;
    Mutex::Autolock autoLock(mLock);

    while (!mOutputQueue.empty()) {
        outIndex = mOutputQueue.front();
        ProcessFrameGetNode(&sOutMemInfo, outIndex, &outPhys, &outId, &outFd, &outFlag);

        auto blockIter = std::find_if(mProcessBlocks.begin(), mProcessBlocks.end(),
                                      [outPhys](const ProcessBlockInfo& pb) {
                                          return pb.mPhysAddr == outPhys;
                                      });
        if (blockIter != mProcessBlocks.end()){
            if(blockIter->mGraphicBlock != NULL)
                (*blockIter).mGraphicBlock.reset();

            if(blockIter->mLinearBlock != NULL)
                (*blockIter).mLinearBlock.reset();
            ALOGV("mLinearBlock.reset");

            if(blockIter->mLinearBlock2 != NULL)
                (*blockIter).mLinearBlock2.reset();

        }
        ProcessFrameClear(&sOutMemInfo,outIndex);
        ALOGV("clearOutputBuffers outIndex=%d",outIndex);
        mOutputQueue.pop();
    }

    return OK;
}


status_t ProcessBase::videoFormatChanged(PROCESSBASE_FORMAT *newFmt) {
    if (sInFormat.width == newFmt->width && sInFormat.height == newFmt->height) {
        ALOGV("videoFormatChanged do nothing return");
        // resolution not changed, do nothing
        //return OK;
    }

    sInFormat.width = newFmt->width;
    sInFormat.height = newFmt->height;
    sInFormat.stride = newFmt->stride;
    sInFormat.interlaced = newFmt->interlaced;

    int32_t err;
    ALOGV("videoFormatChanged BEGIN");

    do {
        sp<AMessage> reply;
        (new AMessage(kWhatResChanged, this))->postAndAwaitResponse(&reply);
        CHECK(reply->findInt32("err", &err));
    } while (err == WOULD_BLOCK);

    bResChanged = false;
    ALOGV("videoFormatChanged END");
    return err;
}

status_t ProcessBase::onOutputBufferReturned(int outputId) {

    if (!ProcessFrameValid(&sOutMemInfo, outputId)) {
        PP_BASE_ERR_LOG("onOutputBufferReturned invalid outputId: %d", outputId);
        return BAD_VALUE;
    }

    mOutputQueue.push(outputId);

    Post_ProcessMessage();
    return OK;
}

status_t ProcessBase::ProcessFrameSet(PROCESS_MEM_INFO* pMem, unsigned long nPhys, int id, int fd, uint32_t flag, int* pIndex) {
    int i;
    for (i = 0; i < MAX_PROCESS_BUFFER_NUM; i++) {
        if (pMem->phyAddr[i] == 0) {
            pMem->phyAddr[i] = nPhys;
            pMem->id[i] = id;
            pMem->fd[i] = fd;
            pMem->flag[i] = flag;
            *pIndex=i;
            break;
        }
    }

    if (i == MAX_PROCESS_BUFFER_NUM) {
        PP_BASE_ERR_LOG("frame buffer is full ! \n");
        *pIndex = -1;
        return BAD_VALUE;
    }

    return OK;
}

status_t ProcessBase::ProcessFrameClear(PROCESS_MEM_INFO* pMem, int nIndex) {

    if (nIndex >= MAX_PROCESS_BUFFER_NUM)
        return BAD_VALUE;

    pMem->phyAddr[nIndex] = 0;
    pMem->virtAddr[nIndex] = 0;
    pMem->fd[nIndex] = -1;
    pMem->id[nIndex] = 0;
    pMem->flag[nIndex] = 0;

    return OK;
}

status_t ProcessBase::ProcessFrameGetNode(PROCESS_MEM_INFO* pMem, int nIndex, unsigned long* pPhys, int* nId, int * out_fd, uint32_t * flag) {

    if (nIndex >= MAX_PROCESS_BUFFER_NUM)
        return BAD_VALUE;

    *pPhys = pMem->phyAddr[nIndex];
    *nId = pMem->id[nIndex];
    *out_fd = pMem->fd[nIndex];
    *flag = pMem->flag[nIndex];
    return OK;
}

bool ProcessBase::ProcessFrameValid(PROCESS_MEM_INFO* pMem, int nIndex) {
    if(pMem->phyAddr[nIndex] == 0 && pMem->fd[nIndex] == -1){
        return false;
    }
    else {
        return true;
    }
}

void ProcessBase::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatProcess: {
            if (mState != RUNNING) {
                break;
            }
            Mutex::Autolock autoLock(mLock);
            ALOGV("kWhatProcess mInputQueue.size()=%d,mTimestampQueue=%d,mOutputQueue.size()=%d",
                mInputQueue.size(),mTimestampQueue.size(),mOutputQueue.size());
            //last buffer for eos frame, for g2d post process
            if(bInputEos && mInputQueue.size() == 1 && mTimestampQueue.size() == 1){
                ALOGV("mAsync=%d nOutputCnt=%d,nInputCnt=%d",mAsync,nOutputCnt,nInputCnt);
                if(!mAsync){
                    ALOGV("bInputEos call NotifyProcessEos directly");
                    NotifyProcessEos();
                    break;
                }else if(mAsync && nOutputCnt > 0 && nInputCnt > 0 && nOutputCnt >= nInputCnt-1 ){
                    ALOGV("bInputEos async call NotifyProcessEos");
                    NotifyProcessEos();
                    break;
                }
            }

            uint64_t timestamp = 0;
            if(!mTimestampQueue.empty())
                timestamp = mTimestampQueue.front();

            ALOGV("onProcess BEGIN mAsync=%d ts=%lld,size=%d",mAsync, (long long)timestamp, mInputQueue.size());
            if(mInputQueue.size() > 0 || mOutputQueue.size() > 0){
                if(OK == onProcess()){

                    if(mAsync){
                        //send kWhatProcess again only when mInputQueue has buffers for async component
                        if(mInputQueue.size() > 0 || mOutputQueue.size() > 0)
                            (new AMessage(kWhatProcess, this))->post();
                    }
                    else{
                        //send kWhatProcess again only when both mInputQueue and mOutputQueue have buffers
                        if (mInputQueue.size() > 0 && mOutputQueue.size() > 0)
                            (new AMessage(kWhatProcess, this))->post();
                    }
                }
            }
            break;
        }
        case kWhatInit: {
            mState = RUNNING;
            int32_t err = onInit();
            Reply(msg, &err);
            break;
        }
        case kWhatStart: {
            mState = RUNNING;
            int32_t err = onStart();
            Reply(msg, &err);
            break;
        }
        case kWhatFlush: {
            Mutexed<InputBufferQueue>::Locked queue(mInputBufferQueue);
            int pre_state = mState;
            mState = FLUSHING;

            while (!queue->empty()) {
                queue->pop_front();
            }
            clearInputBuffers();

            clearOutputBuffers();

            int32_t err = onFlush();
            nInputCnt = 0;
            nOutputCnt = 0;
            Reply(msg, &err);
            mState = pre_state;
            break;
        }
        case kWhatResChanged: {
            int32_t err;
            if (!mTimestampQueue.empty()) {
                ALOGV("process queue still have buffer not processed");
                err = (int32_t)WOULD_BLOCK;
                usleep(10000); // sleep 10ms because process may take at least 10ms
                Reply(msg, &err);
                break;
            }
            bResChanged = true;
            err = onVideoResChanged();
            Reply(msg, &err);
            break;
        }
        case kWhatStop: {
            Mutexed<InputBufferQueue>::Locked queue(mInputBufferQueue);
            mState = STOPPING;
            while (!queue->empty()) {
                queue->pop_front();
            }
            clearInputBuffers();

            clearOutputBuffers();

            int32_t err = onStop();

            //AllocateProcessBuffers maybe called during init, if so do not call FreeProcessBuffers()
            if(!bAllocateBuffers)
                FreeProcessBuffers();

            mState = STOPPED;
            Reply(msg, &err);
            break;
        }
        case kWhatDestroy: {
            // release resources
            Mutexed<InputBufferQueue>::Locked queue(mInputBufferQueue);

            if(mState != STOPPED){
                int32_t err = onStop();
            }
            mState = STOPPED;
            while (!queue->empty()) {
                queue->pop_front();
            }

            clearInputBuffers();
            clearOutputBuffers();

            int32_t err = onDestroy();

            FreeProcessBuffers();
            Reply(msg, &err);
            break;
        }
        case kWhatReturnOutBuf: {
            int outputId;
            if (msg->findInt32("output-id", &outputId)) {
                ALOGV("kWhatReturnOutBuf id=%d",outputId);
                onOutputBufferReturned(outputId);
            } else {
                PP_BASE_ERR_LOG("kWhatReturnOutBuf can't find a valid output id\n");
            }
            break;
        }
        default: {
            PP_BASE_ERR_LOG("Unrecognized msg: %d", msg->what());
            break;
        }
    }
}

status_t ProcessBase::NotifyProcessInputUsed(int inputId) {
    mClient->notifyProcessInputUsed(inputId);
    return OK;
}

status_t ProcessBase::NotifyProcessDone(int outputId, uint32_t flag) {

    uint64_t timestamp = (uint64_t)(-1);
    {
        if (!mTimestampQueue.empty()) {
            timestamp = mTimestampQueue.front();
            mTimestampQueue.pop();
            ALOGV("NotifyProcessDone timestamp=%lld",timestamp);
        }else
            ALOGE("NotifyProcessDone get no timestamp");
    }
    nOutputCnt ++;
    mClient->notifyProcessDone(outputId, timestamp, flag);

    //handle async process eos event, such as isi preprocess
    if(bInputEos && mAsync && mInputQueue.size() == 1 && mTimestampQueue.size() == 1){
        ALOGV("bInputEos async call NotifyProcessEos");
        NotifyProcessEos();
    }

    return OK;
}

status_t ProcessBase::NotifyProcessEos() {

    mInputQueue.pop();
    mTimestampQueue.pop();
    mClient->notifyProcessEos();
    bInputEos = false;
    mState = STOPPED;

    return OK;
}

status_t ProcessBase::FetchProcessBuffer(int *bufferId, unsigned long *phys) {
    uint32_t i;
    C2MemoryUsage usage(0);
    std::shared_ptr<C2GraphicBlock> outBlock;

    ALOGV("%s, res(%d x %d), pixel fmt %d", __FUNCTION__, sOutFormat.width, sOutFormat.height, sOutFormat.format);

    c2_status_t err = mBlockPool->fetchGraphicBlock(sOutFormat.width, sOutFormat.height,
                                                        sOutFormat.format, usage, &outBlock);
    if (err != C2_OK) {
        PP_BASE_LOG("fetchGraphicBlock for Output failed with status %d", err);
        return BAD_VALUE;
    }
    if(bResChanged){
        ALOGI("drop C2GraphicBlock during resolution change");
        outBlock.reset();
        return BAD_VALUE;
    }

    fsl::Memory *prvHandle = (fsl::Memory*)outBlock->handle();
    unsigned long physAddr = prvHandle->phys;

    auto blockIter = std::find_if(mProcessBlocks.begin(), mProcessBlocks.end(),
                                  [physAddr](const ProcessBlockInfo& pb) {
                                      return pb.mPhysAddr == physAddr;
                                  });

    if (blockIter == mProcessBlocks.end()) {

        // fetch a new graphic buffer
        ProcessBlockInfo info;
        memset(&info, 0, sizeof(ProcessBlockInfo));
        info.mBlockId = static_cast<int32_t>(mProcessBlocks.size());
        info.mCapacity = prvHandle->size;
        info.mFd = prvHandle->fd;
        info.mPhysAddr = prvHandle->phys;
        info.mGraphicBlock = std::move(outBlock);
        *bufferId = info.mBlockId;
        *phys = prvHandle->phys;
        mProcessBlocks.push_back(std::move(info));
        ALOGD("fetch a new graphic buffer: id %d, phys %p,fd=%d", *bufferId, (void*)*phys, info.mFd);
    } else {
        // previous buffer fetch back
        ALOGV("previous buffer fetch back, id %d", blockIter->mBlockId);
        *bufferId = blockIter->mBlockId;
        *phys = prvHandle->phys;
        blockIter->mGraphicBlock = std::move(outBlock);
    }

    return OK;
}
status_t ProcessBase::Post_ProcessMessage()
{

    bool newOutput = (1 == mOutputQueue.size());

    if(newOutput){
        ALOGV("Post_ProcessMessage");
        (new AMessage(kWhatProcess, this))->post();
    }
    return OK;
}
}
