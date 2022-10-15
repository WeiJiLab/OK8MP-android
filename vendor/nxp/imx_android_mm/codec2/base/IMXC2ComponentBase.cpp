/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "IMXC2ComponentBase"
#include <log/log.h>

#include <cutils/properties.h>
#include <media/stagefright/foundation/AMessage.h>
#include <inttypes.h>

#include <C2Config.h>
#include <C2Debug.h>
#include <C2PlatformSupport.h>
#include "IMXC2ComponentBase.h"

namespace android {

std::unique_ptr<C2Work> IMXC2ComponentBase::WorkQueue::pop_front() {
    std::unique_ptr<C2Work> work = std::move(mQueue.front().work);
    mQueue.pop_front();
    return work;
}

void IMXC2ComponentBase::WorkQueue::push_back(std::unique_ptr<C2Work> work) {
    mQueue.push_back({ std::move(work), NO_DRAIN });
}

bool IMXC2ComponentBase::WorkQueue::empty() const {
    return mQueue.empty();
}

void IMXC2ComponentBase::WorkQueue::clear() {
    mQueue.clear();
}

uint32_t IMXC2ComponentBase::WorkQueue::drainMode() const {
    return mQueue.front().drainMode;
}

void IMXC2ComponentBase::WorkQueue::markDrain(uint32_t drainMode) {
    mQueue.push_back({ nullptr, drainMode });
}

////////////////////////////////////////////////////////////////////////////////

IMXC2ComponentBase::WorkHandler::WorkHandler() : mRunning(false) {}

void IMXC2ComponentBase::WorkHandler::setComponent(
        const std::shared_ptr<IMXC2ComponentBase> &thiz) {
    mThiz = thiz;
}

static void Reply(const sp<AMessage> &msg, int32_t *err = nullptr) {
    sp<AReplyToken> replyId;
    CHECK(msg->senderAwaitsResponse(&replyId));
    sp<AMessage> reply = new AMessage;
    if (err) {
        reply->setInt32("err", *err);
    }
    reply->postReply(replyId);
}

void IMXC2ComponentBase::WorkHandler::onMessageReceived(const sp<AMessage> &msg) {
    std::shared_ptr<IMXC2ComponentBase> thiz = mThiz.lock();
    if (!thiz) {
        ALOGD("component not yet set; msg = %s", msg->debugString().c_str());
        sp<AReplyToken> replyId;
        if (msg->senderAwaitsResponse(&replyId)) {
            sp<AMessage> reply = new AMessage;
            reply->setInt32("err", C2_CORRUPTED);
            reply->postReply(replyId);
        }
        return;
    }

    switch (msg->what()) {
        case kWhatProcess: {
            if (mRunning) {
                if (thiz->processQueue()) {
                    (new AMessage(kWhatProcess, this))->post();
                }
            } else {
                ALOGV("Ignore process message as we're not running");
            }
            break;
        }
        case kWhatInit: {
            int32_t err = thiz->onInit();
            Reply(msg, &err);
            [[fallthrough]];
        }
        case kWhatStart: {
            mRunning = true;
            break;
        }
        case kWhatStop: {
            int32_t err = thiz->onStop();
            Reply(msg, &err);
            break;
        }
        case kWhatReset: {
            thiz->onReset();
            mRunning = false;
            Reply(msg);
            break;
        }
        case kWhatRelease: {
            thiz->onRelease();
            mRunning = false;
            Reply(msg);
            break;
        }
        default: {
            ALOGD("Unrecognized msg: %d", msg->what());
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

namespace {

struct DummyReadView : public C2ReadView {
    DummyReadView() : C2ReadView(C2_NO_INIT) {}
};

}  // namespace


IMXC2ComponentBase::IMXC2ComponentBase(
        const std::shared_ptr<C2ComponentInterface> &intf)
    : mDummyReadView(DummyReadView()),
      mIntf(intf),
      mLooper(new ALooper),
      mHandler(new WorkHandler) {

    nCurFrameIndex = -1;
    nUsedFrameIndex = -1;
    bRecieveOutputEos = false;
    mLooper->setName(intf->getName().c_str());
    (void)mLooper->registerHandler(mHandler);
    mLooper->start(false, false, ANDROID_PRIORITY_VIDEO);
}

IMXC2ComponentBase::~IMXC2ComponentBase() {
    mLooper->unregisterHandler(mHandler->id());
    (void)mLooper->stop();
}

c2_status_t IMXC2ComponentBase::setListener_vb(
        const std::shared_ptr<C2Component::Listener> &listener, c2_blocking_t mayBlock) {
    mHandler->setComponent(shared_from_this());

    Mutexed<ExecState>::Locked state(mExecState);
    if (state->mState == RUNNING) {
        if (listener) {
            return C2_BAD_STATE;
        } else if (!mayBlock) {
            return C2_BLOCKING;
        }
    }
    state->mListener = listener;
    // TODO: wait for listener change to have taken place before returning
    // (e.g. if there is an ongoing listener callback)
    return C2_OK;
}

c2_status_t IMXC2ComponentBase::queue_nb(std::list<std::unique_ptr<C2Work>> * const items) {
    {
        Mutexed<ExecState>::Locked state(mExecState);
        if (state->mState != RUNNING) {
            return C2_BAD_STATE;
        }
    }
    bool queueWasEmpty = false;
    {
        Mutexed<WorkQueue>::Locked queue(mWorkQueue);
        queueWasEmpty = queue->empty();
        while (!items->empty()) {
            queue->push_back(std::move(items->front()));
            items->pop_front();
        }
    }
    if (queueWasEmpty) {
        (new AMessage(WorkHandler::kWhatProcess, mHandler))->post();
    }
    return C2_OK;
}

c2_status_t IMXC2ComponentBase::announce_nb(const std::vector<C2WorkOutline> &items) {
    (void)items;
    return C2_OMITTED;
}

c2_status_t IMXC2ComponentBase::flush_sm(
        flush_mode_t flushMode, std::list<std::unique_ptr<C2Work>>* const flushedWork) {
    (void)flushMode;
    {
        Mutexed<ExecState>::Locked state(mExecState);
        if (state->mState != RUNNING) {
            return C2_BAD_STATE;
        }
    }
    {
        Mutexed<WorkQueue>::Locked queue(mWorkQueue);
        queue->incGeneration();
        // TODO: queue->splicedBy(flushedWork, flushedWork->end());
        while (!queue->empty()) {
            std::unique_ptr<C2Work> work = queue->pop_front();
            if (work) {
                flushedWork->push_back(std::move(work));
            }
        }
    }
    {
        Mutexed<PendingWork>::Locked pending(mPendingWork);
        while (!pending->empty()) {
            flushedWork->push_back(std::move(pending->front()));
            pending->pop_front();
        }
    }

    return C2_OK;
}

c2_status_t IMXC2ComponentBase::drain_nb(drain_mode_t drainMode) {
    if (drainMode == DRAIN_CHAIN) {
        return C2_OMITTED;
    }
    {
        Mutexed<ExecState>::Locked state(mExecState);
        if (state->mState != RUNNING) {
            return C2_BAD_STATE;
        }
    }
    bool queueWasEmpty = false;
    {
        Mutexed<WorkQueue>::Locked queue(mWorkQueue);
        queueWasEmpty = queue->empty();
        queue->markDrain(drainMode);
    }
    if (queueWasEmpty) {
        (new AMessage(WorkHandler::kWhatProcess, mHandler))->post();
    }

    return C2_OK;
}

c2_status_t IMXC2ComponentBase::start() {
    Mutexed<ExecState>::Locked state(mExecState);
    if (state->mState == RUNNING) {
        return C2_BAD_STATE;
    }
    bool needsInit = (state->mState == UNINITIALIZED);
    state.unlock();
    if (needsInit) {
        sp<AMessage> reply;
        (new AMessage(WorkHandler::kWhatInit, mHandler))->postAndAwaitResponse(&reply);
        int32_t err;
        CHECK(reply->findInt32("err", &err));
        if (err != C2_OK) {
            return (c2_status_t)err;
        }
    } else {
        (new AMessage(WorkHandler::kWhatStart, mHandler))->post();
    }
    state.lock();
    state->mState = RUNNING;
    return C2_OK;
}

c2_status_t IMXC2ComponentBase::stop() {
    ALOGV("stop");
    {
        Mutexed<ExecState>::Locked state(mExecState);
        if (state->mState != RUNNING) {
            return C2_BAD_STATE;
        }
        state->mState = STOPPED;
    }
    {
        Mutexed<WorkQueue>::Locked queue(mWorkQueue);
        queue->clear();
    }
    {
        Mutexed<PendingWork>::Locked pending(mPendingWork);
        pending->clear();
    }
    sp<AMessage> reply;
    (new AMessage(WorkHandler::kWhatStop, mHandler))->postAndAwaitResponse(&reply);
    int32_t err;
    CHECK(reply->findInt32("err", &err));
    if (err != C2_OK) {
        return (c2_status_t)err;
    }
    return C2_OK;
}

c2_status_t IMXC2ComponentBase::reset() {
    ALOGV("reset");
    {
        Mutexed<ExecState>::Locked state(mExecState);
        state->mState = UNINITIALIZED;
    }
    {
        Mutexed<WorkQueue>::Locked queue(mWorkQueue);
        queue->clear();
    }
    {
        Mutexed<PendingWork>::Locked pending(mPendingWork);
        pending->clear();
    }
    sp<AMessage> reply;
    (new AMessage(WorkHandler::kWhatReset, mHandler))->postAndAwaitResponse(&reply);
    return C2_OK;
}

c2_status_t IMXC2ComponentBase::release() {
    ALOGV("release");

    bool needCallStop = false;
    {
        Mutexed<ExecState>::Locked state(mExecState);
        if (state->mState == RUNNING) {
            state->mState = STOPPED;
            needCallStop = true;
        }
    }

    if (needCallStop) {
        {
            Mutexed<WorkQueue>::Locked queue(mWorkQueue);
            queue->clear();
        }
        {
            Mutexed<PendingWork>::Locked pending(mPendingWork);
            pending->clear();
        }
        sp<AMessage> reply;
        (new AMessage(WorkHandler::kWhatStop, mHandler))->postAndAwaitResponse(&reply);
    }

    sp<AMessage> reply;
    (new AMessage(WorkHandler::kWhatRelease, mHandler))->postAndAwaitResponse(&reply);
    return C2_OK;
}

std::shared_ptr<C2ComponentInterface> IMXC2ComponentBase::intf() {
    return mIntf;
}

namespace {

std::list<std::unique_ptr<C2Work>> vec(std::unique_ptr<C2Work> &work) {
    std::list<std::unique_ptr<C2Work>> ret;
    ret.push_back(std::move(work));
    return ret;
}

}  // namespace

c2_status_t IMXC2ComponentBase::finish(
        uint64_t timestamp, std::function<void(const std::unique_ptr<C2Work> &)> fillWork) {

    std::unique_ptr<C2Work> work;
    uint64_t frameIndex;
    {
        // find one work : 1. input is used; 2. input ts match output ts
        Mutexed<PendingWork>::Locked pending(mPendingWork);
        ALOGV("finish, pending size=%d, ts=%lld", (int)pending->size(), (long long)timestamp);
        auto workIter = std::find_if(pending->begin(), pending->end(),
                                 [timestamp](const std::unique_ptr<C2Work>& w) {
                                     return (w->input.ordinal.timestamp == timestamp);
                                 });

        if (workIter != pending->end()) {
            work = std::move(*workIter);
            workIter = pending->erase(workIter);
        } else if (pending->size() > 0) {
            workIter = pending->begin();
            for (auto iter = pending->begin(); iter != pending->end(); ++iter) {
                if ((*iter)->input.ordinal.timestamp < (*workIter)->input.ordinal.timestamp &&
                        ((*iter)->input.flags & C2FrameData::FLAG_END_OF_STREAM) == 0)
                    workIter = iter;
            }

            if (workIter != pending->end()) {
                work = std::move(*workIter);
                workIter = pending->erase(workIter);
                ALOGV("pop the min timestamp used input #%lld, ts %lld, rest pending %zu",
                    (long long)work->input.ordinal.frameIndex.peeku(),
                    (long long)work->input.ordinal.timestamp.peeku(),
                    pending->size());
            }
        }
    }

    if (work) {
        frameIndex = work->input.ordinal.frameIndex.peeku();
        ALOGV("returning pending work #%lld", (long long)frameIndex);

        fillWork(work);

        if (work->worklets.front()->output.flags & C2FrameData::FLAG_END_OF_STREAM)
            bRecieveOutputEos = true;

        Mutexed<ExecState>::Locked state(mExecState);
        std::shared_ptr<C2Component::Listener> listener = state->mListener;
        state.unlock();
        listener->onWorkDone_nb(shared_from_this(), vec(work));

        {
            //add expired time to avoid input buffer return immediately. the value should be less than 3 seconds
            #define EXPIRED_TIME (2000000L)
            std::unique_ptr<C2Work> unexpected;
            Mutexed<PendingWork>::Locked pending(mPendingWork);

            for (;;) {
                auto iter = std::find_if(pending->begin(), pending->end(),
                                     [timestamp, frameIndex](const std::unique_ptr<C2Work>& w) {
                                         return (w->input.ordinal.timestamp != (-1)
                                                    && w->input.ordinal.timestamp + EXPIRED_TIME < timestamp
                                                    && w->input.ordinal.frameIndex < frameIndex
                                                    && (w->input.flags & C2FrameData::FLAG_END_OF_STREAM) == 0);
                                     });

                if (iter != pending->end()) {
                    unexpected = std::move(*iter);
                    pending->erase(iter);
                }

                if (unexpected) {
                    ALOGD("unexpected pending work ts=%lld, frameIndex=%lld",
                        (long long)unexpected->input.ordinal.timestamp.peeku(),
                        (long long)unexpected->input.ordinal.frameIndex.peeku());
                    unexpected->result = C2_OK;
                    Mutexed<ExecState>::Locked state(mExecState);
                    std::shared_ptr<C2Component::Listener> listener = state->mListener;
                    state.unlock();
                    listener->onWorkDone_nb(shared_from_this(), vec(unexpected));
                    continue;
                } else
                    break;
            }
        }

        return C2_OK;
    } else {
        ALOGW("can't find related work (ts=%lld) in mPendingWork", (long long)timestamp);
        //fillWork(work); // notice that work is null in this case
        return C2_NOT_FOUND;
    }
}

c2_status_t IMXC2ComponentBase::finishWithException(bool eos, bool force) {
    // if exception is eos, then just return eos, otherwise decoder meet corrupted stream, need report it.
    std::unique_ptr<C2Work> work;
    Mutexed<PendingWork>::Locked pending(mPendingWork);

    ALOGV("finishWithException, pending size=%d, force %d", (int)pending->size(), force);

    if (pending->empty()) {

        if (eos && force) {
            ALOGI("no pending work when get eos");
            std::unique_ptr<C2Work> work(new C2Work);
            work->input.flags = C2FrameData::FLAG_END_OF_STREAM;
            work->input.ordinal.frameIndex = nCurFrameIndex;
            work->input.buffers.clear();

            work->worklets.emplace_back(new C2Worklet);
            work->worklets.front()->output.flags = C2FrameData::FLAG_END_OF_STREAM;
            work->worklets.front()->output.buffers.clear();
            work->workletsProcessed = 1u;
            work->result = C2_OK;

            Mutexed<ExecState>::Locked state(mExecState);
            std::shared_ptr<C2Component::Listener> listener = state->mListener;
            state.unlock();
            listener->onWorkDone_nb(shared_from_this(), vec(work));
            return C2_OK;
        }

        ALOGD("finishWithException(eos=%d): can't find c2work", eos);
        Mutexed<ExecState>::Locked state(mExecState);
        std::shared_ptr<C2Component::Listener> listener = state->mListener;
        state.unlock();
        listener->onError_nb(shared_from_this(), C2_NOT_FOUND);
        return C2_NOT_FOUND;
    }

    while (!pending->empty()) {
        auto workIter = pending->begin();
        work = std::move(*workIter);
        pending->erase(workIter);
        work->worklets.front()->output.flags = work->input.flags;
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->workletsProcessed = 1u;
        work->result = eos ? C2_OK : C2_CORRUPTED;

        ALOGV("finishWithException(eos=%d): frameIndex %d, result %d, flags 0x%x",
            eos, (int)work->input.ordinal.frameIndex.peeku(), work->result, work->input.flags);

        Mutexed<ExecState>::Locked state(mExecState);
        std::shared_ptr<C2Component::Listener> listener = state->mListener;
        state.unlock();
        listener->onWorkDone_nb(shared_from_this(), vec(work));
    }

    return C2_OK;
}

C2Work* IMXC2ComponentBase::getPendingWorkByFrameIndex(uint64_t frameIndex) {
    Mutexed<PendingWork>::Locked pending(mPendingWork);
    auto workIter = std::find_if(pending->begin(), pending->end(),
                                 [frameIndex](const std::unique_ptr<C2Work>& w) {
                                     return w->input.ordinal.frameIndex == frameIndex;
                                 });

    if (workIter == pending->end()) {
        ALOGV("Can't find pending work by bitstream ID: %llu", (unsigned long long)frameIndex);
        return nullptr;
    }
    return workIter->get();
}

c2_status_t IMXC2ComponentBase::skipOnePendingWork(uint64_t frameIndex) {
    std::unique_ptr<C2Work> work;
    Mutexed<PendingWork>::Locked pending(mPendingWork);
    auto workIter = pending->begin();

    if (frameIndex != (uint64_t)(-1)) {
        workIter = std::find_if(pending->begin(), pending->end(),
                                 [frameIndex](const std::unique_ptr<C2Work>& w) {
                                     return (w->input.ordinal.frameIndex == frameIndex);
                                 });
    }

    if (workIter != pending->end()) {
        work = std::move(*workIter);
        workIter = pending->erase(workIter);
        ALOGV("skip pending work #%lld", (long long)work->input.ordinal.frameIndex.peeku());
        work->worklets.front()->output.flags = work->input.flags;
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->workletsProcessed = 1u;
        work->result = C2_OK;

        Mutexed<ExecState>::Locked state(mExecState);
        std::shared_ptr<C2Component::Listener> listener = state->mListener;
        state.unlock();
        listener->onWorkDone_nb(shared_from_this(), vec(work));
        return C2_OK;
    }

    ALOGV("skip pending work but pending queue is empty");

    return C2_BAD_VALUE;
}

c2_status_t IMXC2ComponentBase::initOutputBlockPool() {
    c2_status_t err = C2_OK;
    // TODO: don't use query_vb
    C2StreamBufferTypeSetting::output outputFormat(0u);
    std::vector<std::unique_ptr<C2Param>> params;
    err = intf()->query_vb(
            { &outputFormat },
            { C2PortBlockPoolsTuning::output::PARAM_TYPE },
            C2_DONT_BLOCK,
            &params);
    if (err != C2_OK && err != C2_BAD_INDEX) {
        ALOGD("query err = %d", err);
        return err;
    }
    C2BlockPool::local_id_t poolId =
        outputFormat.value == C2BufferData::GRAPHIC
                ? C2BlockPool::BASIC_GRAPHIC
                : C2BlockPool::BASIC_LINEAR;
    if (params.size()) {
        C2PortBlockPoolsTuning::output *outputPools =
            C2PortBlockPoolsTuning::output::From(params[0].get());
        if (outputPools && outputPools->flexCount() >= 1) {
            poolId = outputPools->m.values[0];
        }
    }

    err = GetCodec2BlockPool(poolId, shared_from_this(), &mOutputBlockPool);
    if (err == C2_OK && mOutputBlockPool->getLocalId() == C2BlockPool::BASIC_GRAPHIC) {
        // can't C2BasicGraphicBlockPool because it doesn't have a pool manager
        err = CreateCodec2BlockPool(C2PlatformAllocatorStore::GRALLOC, shared_from_this(), &mOutputBlockPool);
        if (err) {
            ALOGE("CreateCodec2BlockPool GRALLOC failed, err %d", err);
            return err;
        } else {
            // Configure output block pool ID as parameter C2PortBlockPoolsTuning::output to component.
            std::unique_ptr<C2PortBlockPoolsTuning::output> poolIdsTuning =
                C2PortBlockPoolsTuning::output::AllocUnique({ mOutputBlockPool->getLocalId() });
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            err = intf()->config_vb({ poolIdsTuning.get() }, C2_MAY_BLOCK, &failures);
            ALOGD("Configured output block pool ids %lld", (long long)poolIdsTuning->m.values[0]);
        }
    }

    ALOGD("Using output block pool with poolID %llu => got %llu - %d",
            (unsigned long long)poolId,
            (unsigned long long)(
                    mOutputBlockPool ? mOutputBlockPool->getLocalId() : 111000111),
            err);

    return C2_OK;

}

bool IMXC2ComponentBase::processQueue() {
    std::unique_ptr<C2Work> work;
    uint64_t generation;
    int32_t drainMode;
    bool isFlushPending = false;
    bool hasQueuedWork = false;

    {
        Mutexed<WorkQueue>::Locked queue(mWorkQueue);
        if (queue->empty()) {
            return false;
        }

        generation = queue->generation();
        drainMode = queue->drainMode();
        isFlushPending = queue->popPendingFlush();
        work = queue->pop_front();
        hasQueuedWork = !queue->empty();
    }
    if (isFlushPending) {
        ALOGV("processing pending flush");
        c2_status_t err = onFlush_sm();
        if (err != C2_OK) {
            ALOGD("flush err: %d", err);
            // TODO: error
        }
    }

    if (!mOutputBlockPool) {
        c2_status_t err = initOutputBlockPool();
        if (err != C2_OK) {
            ALOGE("GetCodec2BlockPool failed, err %d\n", err);
            Mutexed<ExecState>::Locked state(mExecState);
            std::shared_ptr<C2Component::Listener> listener = state->mListener;
            state.unlock();
            listener->onError_nb(shared_from_this(), err);
            return hasQueuedWork;
        }
    }

    if (!work) {
        ALOGV("work=null, call drainInternal, drainMode=%d", drainMode);
        c2_status_t err = drainInternal(drainMode);
        if (err != C2_OK) {
            Mutexed<ExecState>::Locked state(mExecState);
            std::shared_ptr<C2Component::Listener> listener = state->mListener;
            state.unlock();
            listener->onError_nb(shared_from_this(), err);
        }
        return hasQueuedWork;
    }

    {
        std::vector<C2Param *> updates;
        for (const std::unique_ptr<C2Param> &param: work->input.configUpdate) {
            if (param) {
                updates.emplace_back(param.get());
            }
        }
        if (!updates.empty()) {
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            c2_status_t err = intf()->config_vb(updates, C2_MAY_BLOCK, &failures);
            ALOGD("applied %zu configUpdates => %s (%d)", updates.size(), asString(err), err);
        }
    }

    if (!work->input.buffers.empty() && !work->input.buffers[0]) {
        ALOGD("Encountered null input buffer. Clearing the input buffer");
        work->input.buffers.clear();
    }

    nCurFrameIndex = work->input.ordinal.frameIndex.peeku();

    ALOGV("start processing frame #%lld ts %lld" ,
        (long long)nCurFrameIndex, (long long)work->input.ordinal.timestamp.peeku());
    processWork(work);
    ALOGV("processed frame #%" PRIu64, nCurFrameIndex);
    {
        bool componentStopped = false;
        {
            Mutexed<ExecState>::Locked state(mExecState);
            if (state->mState == STOPPED) {
                componentStopped = true;
                ALOGV("component is stopped, this work become unexpected");
            }
        }
        Mutexed<WorkQueue>::Locked queue(mWorkQueue);
        if (queue->generation() != generation || componentStopped) {
            ALOGD("work form old generation: was %" PRIu64 " now %" PRIu64,
                    queue->generation(), generation);
            work->result = C2_NOT_FOUND;
            queue.unlock();
            {
                Mutexed<ExecState>::Locked state(mExecState);
                std::shared_ptr<C2Component::Listener> listener = state->mListener;
                state.unlock();
                listener->onWorkDone_nb(shared_from_this(), vec(work));
            }
            queue.lock();
            return hasQueuedWork;
        }
    }
    if (work->workletsProcessed != 0u) {
        Mutexed<ExecState>::Locked state(mExecState);
        ALOGV("returning this work");
        std::shared_ptr<C2Component::Listener> listener = state->mListener;
        state.unlock();
        listener->onWorkDone_nb(shared_from_this(), vec(work));
    } else {
        ALOGV("queue pending work, ts=%" PRIu64 , work->input.ordinal.timestamp.peeku());
        std::unique_ptr<C2Work> unexpected;
        {
            Mutexed<PendingWork>::Locked pending(mPendingWork);
            uint64_t frameIndex = work->input.ordinal.frameIndex.peeku();

            auto iter = std::find_if(pending->begin(), pending->end(),
                                 [frameIndex](const std::unique_ptr<C2Work>& w) {
                                     return w->input.ordinal.frameIndex == frameIndex;
                                 });

            if (iter != pending->end()) {
                unexpected = std::move(*iter);
                pending->erase(iter);
            }
            (void)pending->emplace_back(std::move(work));
        }
        if (unexpected) {
            ALOGD("unexpected pending work");
            unexpected->result = C2_CORRUPTED;
            Mutexed<ExecState>::Locked state(mExecState);
            std::shared_ptr<C2Component::Listener> listener = state->mListener;
            state.unlock();
            listener->onWorkDone_nb(shared_from_this(), vec(unexpected));
        }
    }

    return hasQueuedWork;
}

std::shared_ptr<C2Buffer> IMXC2ComponentBase::createLinearBuffer(
        const std::shared_ptr<C2LinearBlock> &block) {
    return createLinearBuffer(block, block->offset(), block->size());
}

std::shared_ptr<C2Buffer> IMXC2ComponentBase::createLinearBuffer(
        const std::shared_ptr<C2LinearBlock> &block, size_t offset, size_t size) {
    return C2Buffer::CreateLinearBuffer(block->share(offset, size, ::C2Fence()));
}

std::shared_ptr<C2Buffer> IMXC2ComponentBase::createGraphicBuffer(
        const std::shared_ptr<C2GraphicBlock> &block) {
    return createGraphicBuffer(block, C2Rect(block->width(), block->height()));
}

std::shared_ptr<C2Buffer> IMXC2ComponentBase::createGraphicBuffer(
        const std::shared_ptr<C2GraphicBlock> &block, const C2Rect &crop) {
    return C2Buffer::CreateGraphicBuffer(block->share(crop, ::C2Fence()));
}

} // namespace android
