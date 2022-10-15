/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "VideoDecoderBase"

//#define API_TRACE
#ifdef API_TRACE
#define VDB_API_TRACE ALOGV
#else
#define VDB_API_TRACE(...)
#endif

//#define VDB_INFO_TRACE
#ifdef VDB_INFO_TRACE
#define VDB_INFO ALOGV
#else
#define VDB_INFO(...)
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
#include "Memory.h"
#include "IonAllocator.h"

#include "VideoDecoderBase.h"

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

VideoRect::VideoRect(VideoRect &rect) {
    left = rect.left;
    right = rect.right;
    top = rect.top;
    bottom = rect.bottom;
}

VideoRect::VideoRect(uint32_t left, uint32_t right, uint32_t top, uint32_t bottom)
    : left(left),
      top(top),
      right(right),
      bottom(bottom) {
}

VideoFormat::VideoFormat(
                    int format,
                    uint32_t minNumBuffers,
                    uint32_t width,
                    uint32_t height,
                    VideoRect &rect,
                    bool interlaced)
    : pixelFormat(format),
      minBufferNum(minNumBuffers),
      width(width),
      height(height),
      rect(rect),
      interlaced(interlaced) {
}

VideoDecoderBase::IMXInputBuffer::IMXInputBuffer(
                    void* pBuffer,
                    int fd,
                    int id,
                    uint32_t size,
                    uint64_t ts,
                    bool eos,
                    bool codecdata)
    : pInBuffer(pBuffer),
      fd(fd),
      id(id),
      size(size),
      timestamp(ts),
      eos(eos),
      csd(codecdata){
}

VideoDecoderBase::VideoDecoderBase()
    : bInputEos(false),
      bOutputEos(false),
      bAdaptiveMode(false),
      bSecureMode(false),
      bReceiveError(false),
      bCodecDataReceived(false),
      bCodecDataQueued(false),
      mLooper(new ALooper) {

    bOutputFmtChangedPending = false;
    bReleasingDecoder = false;

    nOutBufferUsage = (uint64_t)(GRALLOC_USAGE_PRIVATE_2);

    pCodecDataBuf = nullptr;
    nCodecDataLen = 0;

    mLooper->setName("VideoDecoderBase");
    mLooper->start(false, false, ANDROID_PRIORITY_VIDEO);
}

VideoDecoderBase::~VideoDecoderBase() {
    if (mLooper != NULL) {
        mLooper->unregisterHandler(id());
        mLooper->stop();
    }
}

status_t VideoDecoderBase::init(Client* client/*const std::shared_ptr<C2BlockPool> &pool*/) {
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    (void)mLooper->registerHandler(this);
    mClient = client;

    sp<AMessage> reply;
    (new AMessage(kWhatInit, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}

status_t VideoDecoderBase::start() {
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);
    sp<AMessage> reply;
    (new AMessage(kWhatStart, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}

status_t VideoDecoderBase::stop() {
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);
    sp<AMessage> reply;
    (new AMessage(kWhatStop, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}

status_t VideoDecoderBase::flush() {
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);
    sp<AMessage> reply;
    (new AMessage(kWhatFlush, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}

status_t VideoDecoderBase::destroy() {
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);
    sp<AMessage> reply;
    sp<AMessage> msg = new AMessage(kWhatDestroy, this);
    status_t err = msg->postAndAwaitResponse(&reply);
    if (err == OK && reply != NULL) {
        CHECK(reply->findInt32("err", &err));
    }
    return err;
}

status_t VideoDecoderBase::queueInput(
        uint8_t *pInBuf, uint32_t size, uint64_t timestamp, uint32_t flags, int fd, int id) {

    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    bool codecdata = ((flags & C2FrameData::FLAG_CODEC_CONFIG) != 0);

    if (codecdata) {
        // previous csd is kept because it is not sent to decoder yet.
        // if there is csd after flushing, clear previous csd
        if (!bCodecDataReceived && !bCodecDataQueued && nCodecDataLen > 0) {
            ALOGV("clear previous csd: nCodecDataLen %d", nCodecDataLen);
            nCodecDataLen = 0;
        }
        if (!pCodecDataBuf) {
            pCodecDataBuf = (uint8_t*)malloc(size);
        } else {
            pCodecDataBuf = (uint8_t*)realloc(pCodecDataBuf, nCodecDataLen + size);
        }

        if (!pCodecDataBuf) {
            ALOGE("malloc pCodecDataBuf falied, size=%d", size);
            return BAD_VALUE;
        }

        memcpy(pCodecDataBuf + nCodecDataLen, pInBuf, size);
        nCodecDataLen += size;
        bCodecDataReceived = true;
        return OK;
    }

    bInputEos = ((flags & C2FrameData::FLAG_END_OF_STREAM) != 0);

    {
        Mutexed<InputBufferQueue>::Locked queue(mInputQueue);
        queue->push_back(std::make_unique<IMXInputBuffer>(pInBuf, fd, id, size, timestamp, bInputEos, codecdata));
    }

    sp<AMessage> reply;
    (new AMessage(kWhatDecode, this))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    return err;
}
status_t VideoDecoderBase::queueOutput(int32_t blockId){
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    sp<AMessage> msg = new AMessage(kWhatQueueOutput, this);
    msg->setInt32("blockId", blockId);
    msg->post();

    return OK;
}

status_t VideoDecoderBase::setConfig(DecConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    switch (index) {
        case DEC_CONFIG_OUTPUT_FORMAT:
            memcpy(&mOutputFormat, pConfig, sizeof(VideoFormat));
            break;
        case DEC_CONFIG_INPUT_FORMAT:
            memcpy(&mInputFormat, pConfig, sizeof(VideoFormat));
            break;
        default:
            return DoSetConfig(index, pConfig);
    }
    return OK;
}

status_t VideoDecoderBase::getConfig(DecConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    switch (index) {
        case DEC_CONFIG_OUTPUT_FORMAT:
            memcpy(pConfig, &mOutputFormat, sizeof(VideoFormat));
            break;
        case DEC_CONFIG_INPUT_FORMAT:
            memcpy(pConfig, &mInputFormat, sizeof(VideoFormat));
            break;
        default:
            return DoGetConfig(index, pConfig);
    }
    return OK;
}

status_t VideoDecoderBase::setGraphicBlockPool(const std::shared_ptr<C2BlockPool> &pool) {
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    if (pool.get() == nullptr) {
        ALOGE("setGraphicBlockPool get nullptr ! \n");
        return BAD_VALUE;
    }

    mBlockPool = pool;
    return OK;
}

status_t VideoDecoderBase::onInit() {
    /* implement by sub class */
    return OK;
}

status_t VideoDecoderBase::onStart() {
    /* implement by sub class */
    return OK;
}

status_t VideoDecoderBase::onStop() {
    /* implement by sub class */
    return OK;
}

status_t VideoDecoderBase::onFlush() {
    /* implement by sub class */
    return OK;
}

status_t VideoDecoderBase::onDestroy() {
    /* implement by sub class */
    return OK;
}

// input == nullptr, drain output
status_t VideoDecoderBase::decodeInternal(std::unique_ptr<IMXInputBuffer> input) {
    /* implement by sub class */
    (void)input;
    return OK;
}

GraphicBlockInfo* VideoDecoderBase::getGraphicBlockById(int32_t blockId) {
    if (blockId < 0 || blockId >= static_cast<int32_t>(mGraphicBlocks.size())) {
        ALOGE("getGraphicBlockById failed: id=%d", blockId);
        return nullptr;
    }
    auto blockIter = std::find_if(mGraphicBlocks.begin(), mGraphicBlocks.end(),
                                  [blockId](const GraphicBlockInfo& gb) {
                                      return gb.mBlockId == blockId;
                                  });

    if (blockIter == mGraphicBlocks.end()) {
        ALOGV("%s line %d: failed: blockId=%d", __FUNCTION__, __LINE__, blockId);
        return nullptr;
    }
    return &(*blockIter);
}

GraphicBlockInfo* VideoDecoderBase::getGraphicBlockByPhysAddr(unsigned long physAddr) {
    if (physAddr == 0) {
        ALOGE("%s line %d: invalid physical address=%p", __FUNCTION__, __LINE__, (void*)physAddr);
        return nullptr;
    }
    auto blockIter = std::find_if(mGraphicBlocks.begin(), mGraphicBlocks.end(),
                                  [physAddr](const GraphicBlockInfo& gb) {
                                      return gb.mPhysAddr == physAddr;
                                  });

    if (blockIter == mGraphicBlocks.end()) {
        ALOGV("%s line %d: failed: physical address=%p", __FUNCTION__, __LINE__, (void*)physAddr);
        return nullptr;
    }
    return &(*blockIter);
}

GraphicBlockInfo* VideoDecoderBase::getFreeGraphicBlock() {
    auto blockIter = std::find_if(mGraphicBlocks.begin(), mGraphicBlocks.end(),
                                  [](const GraphicBlockInfo& gb) {
                                      return gb.mState == GraphicBlockInfo::State::OWNED_BY_COMPONENT;;
                                  });

    if (blockIter == mGraphicBlocks.end()) {
        ALOGV("%s line %d: failed: no free Graphic Block",  __FUNCTION__, __LINE__);
        return nullptr;
    }

    ALOGV("getFreeGraphicBlock blockId %d", blockIter->mBlockId);
    return &(*blockIter);
}

status_t VideoDecoderBase::removeGraphicBlockById(int32_t blockId) {
    if (blockId < 0 || blockId >= static_cast<int32_t>(mGraphicBlocks.size())) {
        ALOGE("getGraphicBlockById failed: id=%d", blockId);
        return BAD_INDEX;
    }
    auto blockIter = std::find_if(mGraphicBlocks.begin(), mGraphicBlocks.end(),
                                  [blockId](const GraphicBlockInfo& gb) {
                                      return gb.mBlockId == blockId;
                                  });

    if (blockIter == mGraphicBlocks.end()) {
        ALOGV("%s line %d: failed: blockId=%d", __FUNCTION__, __LINE__, blockId);
        return BAD_INDEX;
    }

    if ((*blockIter).mVirtAddr > 0 && (*blockIter).mCapacity > 0)
        munmap((void*)(*blockIter).mVirtAddr, (*blockIter).mCapacity);

    (*blockIter).mGraphicBlock.reset();
    mGraphicBlocks.erase(blockIter);
    return OK;
}

status_t VideoDecoderBase::outputFormatChanged() {

    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    bOutputFmtChangedPending = true;
    status_t err = onOutputFormatChanged();
    if (err == OK)
        bOutputFmtChangedPending = false;

    return err;
}

status_t VideoDecoderBase::fetchOutputBuffer() {
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    C2MemoryUsage usage(nOutBufferUsage);

    std::shared_ptr<C2GraphicBlock> outBlock;
    c2_status_t c2_err = mBlockPool->fetchGraphicBlock(mOutputFormat.width, mOutputFormat.height,
                                                    mOutputFormat.pixelFormat, usage, &outBlock);

    if (c2_err == C2_BLOCKING)
        return WOULD_BLOCK;
    else if (c2_err != C2_OK) {
        ALOGE("fetchGraphicBlock for Output failed with status %d", c2_err);
        return UNKNOWN_ERROR;
    }

    int32_t blockId;
    status_t err = appendOutputBuffer(outBlock, &blockId);
    if (err == NO_MEMORY) {
        // sleep 5ms to wait graphic buffer return from display
        usleep(5000);
        err = WOULD_BLOCK;
    }

    return err;
}

void VideoDecoderBase::returnOutputBufferToDecoder(int32_t blockId) {
    GraphicBlockInfo *gbInfo = getGraphicBlockById(blockId);
    if (gbInfo) {
        ALOGV("%s: blockId %d", __FUNCTION__, blockId);
        gbInfo->mState = GraphicBlockInfo::State::OWNED_BY_COMPONENT;
    } else {
        ALOGE("%s: invalid blockId %d", __FUNCTION__, blockId);
    }
}

void VideoDecoderBase::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatDecode: {
            std::unique_ptr<IMXInputBuffer> input;
            {
                Mutexed<InputBufferQueue>::Locked queue(mInputQueue);
                // there is always have at least one input
                input = std::move(queue->front());
                queue->pop_front(); // pop in NotifyInputBufferUsed ?
            }
            status_t err = decodeInternal(std::move(input));
            Reply(msg, &err);
            break;
        }
        case kWhatInit: {
            int32_t err = onInit();
            Reply(msg, &err);
            break;
        }
        case kWhatStart: {
            int32_t err = onStart();
            Reply(msg, &err);
            break;
        }
        case kWhatStop: {
            int32_t err = onStop();
            Reply(msg, &err);
            break;
        }
        case kWhatFlush: {
            ALOGV("kWhatFlush");
            Mutexed<InputBufferQueue>::Locked queue(mInputQueue);
            while (!queue->empty()) {
                queue->pop_front();
            }

            bCodecDataReceived = false;
            bReleasingDecoder = false;

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

            if (pCodecDataBuf) {
                free(pCodecDataBuf);
                pCodecDataBuf = nullptr;
                nCodecDataLen = 0;
            }

            bReleasingDecoder = true;

            int32_t err = onDestroy();
            freeOutputBuffers();
            Reply(msg, &err);
            break;
        }
        case kWhatQueueOutput:
            int32_t blockId;
            CHECK(msg->findInt32("blockId", &blockId));
            returnOutputBufferToDecoder(blockId);
            break;

        default: {
            ALOGW("Unrecognized msg: %d", msg->what());
            break;
        }
    }
}

status_t VideoDecoderBase::appendOutputBuffer(std::shared_ptr<C2GraphicBlock> block, int32_t* blockId) {
    fsl::Memory *prvHandle = (fsl::Memory*)block->handle();

    ALOGV("appendOutputBuffer handle %p, fd %d virt %p, phys %p, format 0x%x, size %d",
        prvHandle, prvHandle->fd, (void*)prvHandle->base, (void*)prvHandle->phys, prvHandle->format, prvHandle->size);

    GraphicBlockInfo* pInfo = getGraphicBlockByPhysAddr(prvHandle->phys);
    if (pInfo) {
        // previous output buffer returned to decoder
        ALOGV("previous output buffer returned to decoder, blockId %d", pInfo->mBlockId);
        pInfo->mDMABufFd = prvHandle->fd;
        pInfo->mGraphicBlock = std::move(block);
        pInfo->mState = GraphicBlockInfo::State::OWNED_BY_COMPONENT;
        *blockId = pInfo->mBlockId;
    } else {
        if (true == OutputBufferFull()) {
            return NO_MEMORY;
        }
        GraphicBlockInfo info;

        fsl::IonAllocator * pIonAllocator = fsl::IonAllocator::getInstance();
        int ret = pIonAllocator->getVaddrs(prvHandle->fd, prvHandle->size, (uint64_t&)info.mVirtAddr);
        if (ret != 0) {
            ALOGE("Ion get virtual address failed, fd %d", info.mDMABufFd);
            return BAD_VALUE;
        }

        info.mDMABufFd = prvHandle->fd;
        info.mCapacity = prvHandle->size;
        info.mPhysAddr = prvHandle->phys;
        info.mPixelFormat = prvHandle->format;
        info.mState = GraphicBlockInfo::State::OWNED_BY_COMPONENT;
        info.mBlockId = static_cast<int32_t>(mGraphicBlocks.size());
        info.mGraphicBlock = std::move(block);
        *blockId = info.mBlockId;
        ALOGI("fetch a new buffer, blockId %d phys %p virt %p", info.mBlockId, (void*)info.mPhysAddr, (void*)info.mVirtAddr);

        Mutex::Autolock autoLock(mGBLock);
        mGraphicBlocks.push_back(std::move(info));
    }

    return OK;
}

status_t VideoDecoderBase::importOutputBuffers(std::vector<GraphicBlockInfo> buffers)
{
    return OK;
}

status_t VideoDecoderBase::onOutputFormatChanged() {
    VDB_API_TRACE("%s, line %d", __FUNCTION__, __LINE__);

    ALOGI("New format(pixelfmt=0x%x, min buffers=%u, request buffers %d, w*h=%d x %d, crop=(%d %d %d %d), interlaced %d)",
          mOutputFormat.pixelFormat, mOutputFormat.minBufferNum, mOutputFormat.bufferNum,
          mOutputFormat.width, mOutputFormat.height,
          mOutputFormat.rect.left, mOutputFormat.rect.top,
          mOutputFormat.rect.right, mOutputFormat.rect.bottom, mOutputFormat.interlaced);

    status_t err;

    mClient->notifyVideoInfo(&mOutputFormat);

    for (auto& info : mGraphicBlocks) {
        if (info.mState == GraphicBlockInfo::State::OWNED_BY_VPU)
            info.mState = GraphicBlockInfo::State::OWNED_BY_COMPONENT;
    }

    for (const auto& info : mGraphicBlocks) {
        CHECK(info.mState != GraphicBlockInfo::State::OWNED_BY_VPU);
    }

    err = freeOutputBuffers();
    if (err) {
        NotifyError(err);
        return err;
    }

    err = allocateOutputBuffers();
    if (err) {
        NotifyError(err);
        return err;
    }

    importOutputBuffers(mGraphicBlocks);

    return OK;
}

void VideoDecoderBase::ClearPictureBuffer() {
    mClient->clearPictureBuffer();
}

void VideoDecoderBase::NotifyFlushDone () {
    mClient->notifyFlushDone();
}

void VideoDecoderBase::NotifyInputBufferUsed(int32_t input_id) {
    mClient->notifyInputBufferUsed(input_id);
}

void VideoDecoderBase::NotifySkipInputBuffer(int32_t input_id) {
    mClient->notifySkipInputBuffer(input_id);
}

void VideoDecoderBase::NotifyPictureReady(int32_t pictureId, uint64_t timestamp) {
    if (bReleasingDecoder)
        returnOutputBufferToDecoder(pictureId);
    else {
        Mutex::Autolock autoLock(mGBLock);
        mClient->notifyPictureReady(pictureId, timestamp);
    }
}

void VideoDecoderBase::NotifyEOS() {
    mClient->notifyEos();
}

void VideoDecoderBase::NotifyError(status_t err) {
    mClient->notifyError(err);
}

int VideoDecoderBase::getBlockPoolAllocatorId()
{
    if(mBlockPool != nullptr)
        return mBlockPool->getAllocatorId();

    return -1;
}
} // namespace android

/* end of file */
