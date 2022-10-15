/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "VideoEncoderBase"

//#define API_TRACE
#ifdef API_TRACE
#define VEB_API_TRACE ALOGI
#else
#define VEB_API_TRACE(...)
#endif

//#define VEB_INFO_TRACE
#ifdef VEB_INFO_TRACE
#define VEB_INFO ALOGI
#else
#define VEB_INFO(...)
#endif

#include <utils/Log.h>
//#include <cutils/properties.h>
#include <media/stagefright/foundation/AMessage.h>
#include <inttypes.h>
#include <sys/mman.h>

#include <C2Config.h>
#include <C2Debug.h>
#include <C2PlatformSupport.h>

#include "graphics_ext.h"
#include "IonAllocator.h"

#include "VideoEncoderBase.h"

namespace android {

#define DEFAULT_ENC_BUF_OUT_CNT		0x3
#define DEFAULT_ENC_BUF_OUT_SIZE    (2*1024*1024)	//FIXME: set one big enough value !!!

static void Reply(const sp<AMessage> &msg, int32_t *err = nullptr) {
    sp<AReplyToken> replyId;
    CHECK(msg->senderAwaitsResponse(&replyId));
    sp<AMessage> reply = new AMessage;
    if (err) {
        reply->setInt32("err", *err);
    }
    reply->postReply(replyId);
}

VideoFormat::VideoFormat(
                    int format,
                    uint32_t minNumBuffers,
                    uint32_t width,
                    uint32_t height,
                    bool interlaced)
    : pixelFormat(format),
      minBufferNum(minNumBuffers),
      width(width),
      height(height),
      interlaced(interlaced) {
}

IMXInputBuffer::IMXInputBuffer(IMXInputBuffer* pInput)
    : pInputVirt(pInput->pInputVirt),
      pInputPhys(pInput->pInputPhys),
      fd(pInput->fd),
      id(pInput->id),
      size(pInput->size),
      timestamp(pInput->timestamp),
      flag(pInput->flag),
      eos(pInput->eos){

}

IMXInputBuffer::IMXInputBuffer(
                    void* pVirt,
                    void* pPhys,
                    int fd,
                    int id,
                    uint32_t size,
                    uint64_t timestamp,
                    uint32_t flag,
                    bool eos)
    : pInputVirt(pVirt),
      pInputPhys(pPhys),
      fd(fd),
      id(id),
      size(size),
      timestamp(timestamp),
      flag(flag),
      eos(eos){
}

VideoEncoderBase::VideoEncoderBase()
    : bInputEos(false),
      bOutputEos(false),
      mLooper(new ALooper) {
    mInputFormat.bufferNum = kInputBufferCount;
    mInputFormat.bufferSize = kInputBufferSizeFor1080p;
    mInputFormat.width = DEFAULT_FRM_WIDTH;
    mInputFormat.height = DEFAULT_FRM_HEIGHT;

    nOutBufferUsage = 0;//(uint64_t)(C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE);

    mOutputFormat.width = DEFAULT_FRM_WIDTH;
    mOutputFormat.height = DEFAULT_FRM_HEIGHT;
    mOutputFormat.bufferNum = DEFAULT_ENC_BUF_OUT_CNT;
    mOutputFormat.bufferSize = DEFAULT_ENC_BUF_OUT_SIZE;

    mLooper->setName("VideoEncoderBase");
    mLooper->start(false, false, ANDROID_PRIORITY_VIDEO);
}

VideoEncoderBase::~VideoEncoderBase() {
    if (mLooper != NULL) {
        mLooper->unregisterHandler(id());
        mLooper->stop();
    }
}

status_t VideoEncoderBase::RegisterLooper() {
    (void)mLooper->registerHandler(this);
    return OK;
}

status_t VideoEncoderBase::init(Client* client/*const std::shared_ptr<C2BlockPool> &pool*/) {

    VEB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    mClient = client;
    sp<AMessage> reply;
    (new AMessage(kWhatInit, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}

status_t VideoEncoderBase::stop() {

    VEB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    sp<AMessage> reply;
    (new AMessage(kWhatStop, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}

status_t VideoEncoderBase::flush() {

    VEB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    sp<AMessage> reply;
    (new AMessage(kWhatFlush, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}

status_t VideoEncoderBase::destroy() {

    VEB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    sp<AMessage> reply;
    sp<AMessage> msg = new AMessage(kWhatDestroy, this);
    status_t err = msg->postAndAwaitResponse(&reply);
    if (err == OK && reply != NULL) {
        CHECK(reply->findInt32("err", &err));
    }

    return err;
}

status_t VideoEncoderBase::queueInput(IMXInputBuffer * pInBuffer) {

    VEB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    bInputEos = pInBuffer->eos;

    {
        Mutexed<InputBufferQueue>::Locked queue(mInputQueue);
        queue->push_back(std::make_unique<IMXInputBuffer>(pInBuffer));
    }

#if 0
    sp<AMessage> reply;
    (new AMessage(kWhatEncode, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
#else
    (new AMessage(kWhatEncode, this))->post();
    return OK;
#endif
}

status_t VideoEncoderBase::importOutput(std::shared_ptr<C2FrameData> outputBuffer) {
    (void)outputBuffer;
    return OK;
}

status_t VideoEncoderBase::setConfig(EncConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    return DoSetConfig(index, pConfig);
}

status_t VideoEncoderBase::getConfig(EncConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    return DoGetConfig(index, pConfig);
}

status_t VideoEncoderBase::allocateOutputBuffers() {
    uint32_t i;
    C2MemoryUsage usage(nOutBufferUsage);

    VEB_API_TRACE("%s, line %d, mBlockPool %p, try to allocate %d buffers \n",
        __FUNCTION__, __LINE__, mBlockPool.get(), mOutputFormat.bufferNum);

    for (i = 0; i < mOutputFormat.bufferNum; i++) {
        std::shared_ptr<C2LinearBlock> outBlock;
        c2_status_t err = mBlockPool->fetchLinearBlock(mOutputFormat.bufferSize, usage, &outBlock);
        if (err != C2_OK) {
            ALOGE("fetchLinearBlock for Output failed with status %d", err);
            return BAD_VALUE;
        }

        if (OK != appendOutputBuffer(std::move(outBlock), &mOutputFormat.bufferSize))
            return BAD_VALUE;
    }
    return OK;
}

status_t VideoEncoderBase::freeOutputBuffers() {
    VEB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    for (auto& info : mLinearBlocks) {
        if (info.mVirtAddr > 0 && info.mCapacity > 0)
            munmap((void*)info.mVirtAddr, info.mCapacity);
    }

    mLinearBlocks.clear();

    return OK;
}

bool VideoEncoderBase::checkIfPreProcessNeeded(int pixelFormat) {
    return false;
}

status_t VideoEncoderBase::getCodecData(uint8_t** pCodecData, uint32_t* size) {
    (void)pCodecData;
    (void)size;
    return OK;
}

status_t VideoEncoderBase::setLinearBlockPool(const std::shared_ptr<C2BlockPool>& pool) {

    VEB_API_TRACE("%s, line %d, pool=%p", __FUNCTION__, __LINE__, pool.get());

    if (pool.get() == nullptr) {
        ALOGE("setLineatBlockPool get nullptr ! \n");
        return BAD_VALUE;
    }

    mBlockPool = pool;
    return OK;
}

status_t VideoEncoderBase::onInit() {
    /* implement by sub class */
    return OK;
}

status_t VideoEncoderBase::onStop() {
    /* implement by sub class */
    return OK;
}

status_t VideoEncoderBase::onFlush() {
    /* implement by sub class */
    return OK;
}

status_t VideoEncoderBase::onDestroy() {
    /* implement by sub class */
    return OK;
}

// input == nullptr, drain output
status_t VideoEncoderBase::encodeInternal(std::unique_ptr<IMXInputBuffer> input) {
    /* implement by sub class */
    (void)input;
    return OK;
}

status_t VideoEncoderBase::onOutputBufferReturned(int32_t bufferId) {
    (void)bufferId;
    return OK;
}

status_t VideoEncoderBase::getOutputVideoInfo(VideoFormat * info) {
    (void)info;
    return OK;
}

LinearBlockInfo* VideoEncoderBase::getLinearBlockById(int32_t blockId) {
    if (blockId < 0 || blockId >= static_cast<int32_t>(mLinearBlocks.size())) {
        ALOGE("getLinearBlockById failed: id=%d", blockId);
        return nullptr;
    }
    return &mLinearBlocks[blockId];
}

LinearBlockInfo* VideoEncoderBase::getLinearBlockByFd(int fd) {
    if (fd < 0) {
        ALOGE("%s line %d: invalid fd=%d", __FUNCTION__, __LINE__, fd);
        return nullptr;
    }
    auto blockIter = std::find_if(mLinearBlocks.begin(), mLinearBlocks.end(),
                                  [fd](const LinearBlockInfo& lb) {
                                      return lb.mDMABufFd == fd;
                                  });

    if (blockIter == mLinearBlocks.end()) {
        ALOGE("%s line %d: failed: fd=%d", __FUNCTION__, __LINE__, fd);
        return nullptr;
    }
    return &(*blockIter);
}

LinearBlockInfo* VideoEncoderBase::getLinearBlockByPhysAddr(unsigned long physAddr) {
    if (physAddr == 0) {
        ALOGE("%s line %d: invalid physical address=%lld", __FUNCTION__, __LINE__, (long long)physAddr);
        return nullptr;
    }
    auto blockIter = std::find_if(mLinearBlocks.begin(), mLinearBlocks.end(),
                                  [physAddr](const LinearBlockInfo& lb) {
                                      return lb.mPhysAddr == physAddr;
                                  });

    if (blockIter == mLinearBlocks.end()) {
        return nullptr;
    }
    return &(*blockIter);
}

LinearBlockInfo* VideoEncoderBase::getFreeLinearBlock() {
    auto blockIter = std::find_if(mLinearBlocks.begin(), mLinearBlocks.end(),
                                  [](const LinearBlockInfo& lb) {
                                      return lb.mState == LinearBlockInfo::State::OWNED_BY_COMPONENT;;
                                  });

    if (blockIter == mLinearBlocks.end()) {
        return nullptr;
    }
    return &(*blockIter);
}

void VideoEncoderBase::onOutputBufferDone() {
#if 0
    VEB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    C2MemoryUsage usage(nOutBufferUsage);

    std::shared_ptr<C2LinearBlock> outBlock;
    c2_status_t c2_err = mBlockPool->fetchLinearBlock(/* size */,
                                                    mOutputFormat.pixelFormat, usage, &outBlock);
    if (c2_err != C2_OK) {
        ALOGE("fetchLinearBlock for Output failed with status %d", c2_err);
        return ;
    }

    int fd = outBlock->handle()->data[0];
    LinearBlockInfo* info = getLinearBlockByFd(fd);
    if (!info) {
        // TODO: register to vpu
        if (OK != appendOutputBuffer(outBlock))
            return;
        info = getLinearBlockByFd(fd);
        if (!info) {
            ALOGE("%s, line %d, can't handle fd=%d \n", __FUNCTION__, __LINE__, fd);
            return;
        }
        VEB_INFO("%s, fetch a new buffer: fd=%d", __FUNCTION__, fd);
    } else if (info->mState != LinearBlockInfo::State::OWNED_BY_CLIENT) {
        ALOGE("%s, get wrong buffer state %d", __FUNCTION__, info->mState);
        return;
    } else {
        ALOGV("%s, return fd %d, block id %d", __FUNCTION__, fd, (int)info->mBlockId);
        info->mLinearBlock = std::move(outBlock);
        info->mState = LinearBlockInfo::State::OWNED_BY_COMPONENT;
    }

    onOutputBufferReturned(info->mBlockId);
#endif
}

void VideoEncoderBase::returnOutputBufferToEncoder(int32_t blockId) {
    LinearBlockInfo *pInfo = getLinearBlockById(blockId);
    if (pInfo) {
        pInfo->mState = LinearBlockInfo::State::OWNED_BY_COMPONENT;
    } else {
        ALOGE("%s: invalid blockId %d", __FUNCTION__, blockId);
    }
}

void VideoEncoderBase::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatEncode: {
            std::unique_ptr<IMXInputBuffer> input;
            {
                Mutexed<InputBufferQueue>::Locked queue(mInputQueue);
                // there is always have at least one input
                input = std::move(queue->front());
                queue->pop_front(); // pop in NotifyInputBufferUsed ?
            }

            status_t err = encodeInternal(std::move(input));
            //Reply(msg, &err);
            break;
        }
        case kWhatInit: {
            int32_t err = onInit();
            Reply(msg, &err);
            break;
        }
        case kWhatStop: {
            int32_t err = onStop();
            Reply(msg, &err);
            break;
        }
        case kWhatFlush: {
            Mutexed<InputBufferQueue>::Locked queue(mInputQueue);
            while (!queue->empty()) {
                queue->pop_front();
            }
            int32_t err = onFlush();
            Reply(msg, &err);
            break;
        }
        case kWhatDestroy: {
            // release resources
            Mutexed<InputBufferQueue>::Locked queue(mInputQueue);
            while (!queue->empty()) {
                queue->pop_front();
            }
            int32_t err = onDestroy();
            freeOutputBuffers();
            Reply(msg, &err);
            break;
        }
        default: {
            ALOGW("Unrecognized msg: %d", msg->what());
            break;
        }
    }
}

status_t VideoEncoderBase::appendOutputBuffer(std::shared_ptr<C2LinearBlock> block, uint32_t* capacity) {
    fsl::IonAllocator * pIonAllocator = fsl::IonAllocator::getInstance();
    int ret;
    int fd = block->handle()->data[0];
    uint64_t pPhys = 0;
    uint64_t pVirt = 0;
    uint32_t size = *capacity;//mOutputFormat.bufferSize;

    ret = pIonAllocator->getPhys(fd, size, (uint64_t&)pPhys);
    if (ret != 0) {
        ALOGE("Ion get physical address failed, fd %d", fd);
        return BAD_VALUE;
    }

    ret = pIonAllocator->getVaddrs(fd, size, (uint64_t&)pVirt);
    if (ret != 0) {
        ALOGE("Ion get virtual address failed, size=%d, fd %d", size, fd);
        return BAD_VALUE;
    }

    LinearBlockInfo* pInfo = getLinearBlockByPhysAddr(pPhys);
    if (pInfo) {
        // previous output buffer returned to decoder
        pInfo->mDMABufFd = fd;
        pInfo->mVirtAddr = pVirt;
        pInfo->mLinearBlock = std::move(block);
        pInfo->mCapacity = size;
        pInfo->mState = LinearBlockInfo::State::OWNED_BY_COMPONENT;
    } else {
        LinearBlockInfo info;
        memset(&info, 0, sizeof(LinearBlockInfo));
        info.mBlockId = static_cast<int32_t>(mLinearBlocks.size());
        info.mDMABufFd = fd;
        info.mState = LinearBlockInfo::State::OWNED_BY_COMPONENT;
        info.mCapacity = size;
        info.mPhysAddr = pPhys;
        info.mVirtAddr = pVirt;
        info.mLinearBlock = std::move(block);
        mLinearBlocks.push_back(std::move(info));

        VEB_INFO("appendOutputBuffer id %d phys %p, virt %p \n", info.mBlockId, (void*)pPhys, (void*)pVirt);
    }

    return OK;
}

status_t VideoEncoderBase::importOutputBuffers(std::vector<LinearBlockInfo> buffers) {
    return OK;
}

status_t VideoEncoderBase::FetchOutputBuffer(int* bufferId,
                                                    int* fd,
                                                    unsigned long* pPhysAddr,
                                                    unsigned long* pVirtAddr,
                                                    uint32_t* capacity) {
    status_t ret = OK;
    LinearBlockInfo* info = getFreeLinearBlock();

    if (nullptr == info) {
        std::shared_ptr<C2LinearBlock> block;
        C2MemoryUsage usage(nOutBufferUsage);
        c2_status_t status = mBlockPool->fetchLinearBlock(*capacity, usage, &block);
        if (C2_OK != status) {
            ALOGE("fetchLinearBlock for Output failed with status 0x%x", status);
            return BAD_VALUE;
        }

        ret = appendOutputBuffer(block, capacity);
        if (ret != OK)
            return ret;

        info = getFreeLinearBlock();
        if (nullptr == info) {
            // shouldn't come here because we just allocated a free block
            ALOGE("FetchOutputBuffer: can't get a free block");
            return BAD_VALUE;
        }

    }

    *bufferId = info->mBlockId;
    *fd = info->mDMABufFd;
    *pPhysAddr = info->mPhysAddr;
    *pVirtAddr = info->mVirtAddr;
    *capacity = info->mCapacity;
    info->mState = LinearBlockInfo::State::OWNED_BY_VPU;

    return OK;
}

void VideoEncoderBase::ClearOutputFrameBuffer() {
    mClient->clearOutputFrameBuffer();
}

void VideoEncoderBase::NotifyFlushDone () {
    mClient->notifyFlushDone();
}

void VideoEncoderBase::NotifyInputBufferUsed(int32_t input_id) {
    ALOGV("NotifyInputBufferUsed %d", (int)input_id);
    mClient->notifyInputBufferUsed(input_id);
}

void VideoEncoderBase::NotifyOutputFrameReady(int32_t outFrameId, uint32_t frameSize,
                                                         uint64_t timestamp, int keyFrame, uint32_t offset) {
    mClient->notifyOutputFrameReady(outFrameId, frameSize, timestamp, keyFrame, offset);
}

void VideoEncoderBase::NotifyEOS() {
    mClient->notifyEos();
}

void VideoEncoderBase::NotifyError(status_t err) {
    mClient->notifyError(err);
}

} // namespace android

/* end of file */
