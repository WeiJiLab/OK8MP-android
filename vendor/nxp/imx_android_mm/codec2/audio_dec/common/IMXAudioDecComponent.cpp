/**
 *  Copyright 2019,2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "AudDecComp"
#include "IMXAudioDecComponent.h"
#include <C2Buffer.h>
#include <sys/stat.h>
#include <stdio.h>
#include <numeric>
#include <cutils/properties.h>

#define MAX(a,b) ((a)>=(b)?(a):(b))

namespace android {

IMXAudioDecComponent::IMXAudioDecComponent(const std::shared_ptr<C2ComponentInterface> &intf)
: IMXC2ComponentBase(intf),
    bConvertEnable(true),
    ePlayMode(DEC_FILE_MODE),
    pConvertBuffer(nullptr),
    bFirstOutput(true),
    fpOutput(nullptr),
    frameIndex(0),
    bInitFlag(false)
{
    logLevel = property_get_int32("vendor.media.log.level", 0);
    LOGV("entry %p", this);
}

IMXAudioDecComponent::~IMXAudioDecComponent()
{
    LOGV("entry");
    if(fpOutput != nullptr)
        fclose(fpOutput);
}

c2_status_t IMXAudioDecComponent::onInit()
{
    LOGV("entry");
    if(bInitFlag) // avoid init twice
        return C2_OK;

    RINGBUFFER_ERRORTYPE BufferRet = RINGBUFFER_SUCCESS;
    BufferRet = AudioRingBuffer.BufferCreate(nPushModeInputLen, nRingBufferScale);
    if (BufferRet != RINGBUFFER_SUCCESS)
    {
    	LOGE("Create ring buffer fail.\n");
    	return C2_NO_MEMORY;
    }
    LOGD("Ring buffer nPushModeInputLen %d\n", nPushModeInputLen);

    AUDIO_TS_MANAGER_ERRORTYPE Ret = AUDIO_TS_MANAGER_SUCCESS;
    Ret = TS_Manager.Create();
    if (Ret != AUDIO_TS_MANAGER_SUCCESS)
    {
    	LOGE("Create audio ts manager fail.\n");
    	return C2_NO_MEMORY;
    }

    bReceivedEOS = false;
    bCodecInit = false;
    bDecoderEOS = false;
    bDecoderInitFail = false;
    bFirstInBuffer = true;
    bFirstOutput = true;
    nRequiredSize = 0;
    nOutputBitPerSample = 16;

    nConvertBufferSize = 200000;
    bCheckFrameHeader = false;
    mTimestampDelta = 0;
    mDecodeEachInput = false;
    if(bConvertEnable){
        pConvertBuffer = (uint8_t *)malloc(nConvertBufferSize);
    }

    if(C2_OK != doInit()){
        LOGE("doInit fail!");
        return C2_NO_MEMORY;
    }

    bInitFlag = true;
    return C2_OK;
}

c2_status_t IMXAudioDecComponent::onStop()
{
    LOGV("entry");
    onFlush_sm();
    return C2_OK;
}

void IMXAudioDecComponent::onReset()
{
    LOGV("entry");

    (void) onStop();
}

void IMXAudioDecComponent::onRelease()
{
    LOGV("entry");
    c2_status_t ret = C2_OK;

    ret = unInit();
    if (ret != C2_OK)
    {
    	LOGE("Audio decoder de-init fail.\n");
    	return;
    }

    LOGD("Audio decoder instance de-init.\n");
    AudioRingBuffer.BufferFree();
    TS_Manager.Free();

    if(pConvertBuffer != nullptr){
        free(pConvertBuffer);
        pConvertBuffer = nullptr;
    }

    bInitFlag = false;
    return;
}

c2_status_t IMXAudioDecComponent::onFlush_sm()
{
    LOGV("entry");

    bReceivedEOS = false;
    bDecoderEOS = false;
    bFirstOutput = true;
    bCheckFrameHeader = false;
    mDecodeEachInput = false;
    mTimestampDelta = 0;
    AudioRingBuffer.BufferReset();
    TS_Manager.Reset();

    while (!mFrameLenQueue.empty()) {
        mFrameLenQueue.pop();
    }

    doReset();
    LOGD("Clear ring buffer.\n");
    return C2_OK;
}

   
void IMXAudioDecComponent::processWork(const std::unique_ptr<C2Work> &work)
{
    LOGV("====================");
    c2_status_t ret = C2_OK;
    uint32_t threshold = 0; // lower limit of data length when starting decoding

    work->workletsProcessed = 0u;
    work->result = C2_OK;
    work->worklets.front()->output.configUpdate.clear();
    //if (mSignalledError) {
    //    return;
    //}

    _processInputFlag(work);
    if(work->workletsProcessed == 1u){
        LOGV("processWork done after _processInputFlag");
        return;
    }

    _processInputData(work);

    //uint64_t frameIndex = work->input.ordinal.frameIndex.peeku();

    if (bCheckFrameHeader)
    {
    	ret = checkFrameHeader();
    	if (ret != C2_OK)
    	{
    		LOGE("CheckFrameHeader fail.\n");
    	}
    }

    //only 2 frames in audio ringbuffer for DEC_STREAM_MODE
    if(ePlayMode == DEC_FILE_MODE){
        threshold = nPushModeInputLen;
    }else if(nRequiredSize > 0){
        threshold = nRequiredSize;
    }else{
        threshold = nRequiredSize = AudioRingBuffer.AudioDataLen()+DEFAULT_REQUIRED_SIZE;
    }

    if (mDecodeEachInput)
        threshold = 1;

    LOGV("playMode %s, nRequiredSize %d, nPushModeInputLen %d, result threshold %d",
        (ePlayMode == DEC_FILE_MODE)?"file":"stream", nRequiredSize, nPushModeInputLen, threshold);

    if ((AudioRingBuffer.AudioDataLen() < threshold && bReceivedEOS == false)
        || bDecoderEOS == true)
    {
        LOGV("RingBuffer data is not enough or DecoderEOS true, skip decoding.\n");
        work->worklets.front()->output.flags = work->input.flags;
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->worklets.front()->output.buffers.clear();
        work->workletsProcessed = 1u;
        return;
    }

    // this seems just for aac, need to move into UniaDecoder or AacDecodeUtil
    if (!bCodecInit)
    {
        LOGV("call CodecInit");
    	bCodecInit = true;
    	ret = codecInit();
    	if (ret != C2_OK)
    	{
    		LOGE("CodecInit fail.\n");
    		bDecoderInitFail = true;
    	}
    }

    ret = _processOutput(work);
    if(C2_OK != ret){
        LOGE("_processOutput fail ret %d", ret);
    }

    work->worklets.front()->output.flags = work->input.flags;
    work->workletsProcessed = 1u;

    LOGV("return input work: flags 0x%x, Processed %d", work->worklets.front()->output.flags, work->workletsProcessed);

    //work->worklets.front()->output.ordinal = work->input.ordinal;
    /*
   for (const auto &p : work->worklets.front()->output.configUpdate) {
        if (!p) {
            continue;
        }
        LOGV("param size is %zu", p->size());
    }  
    */
    
}

c2_status_t IMXAudioDecComponent::_processInputFlag(const std::unique_ptr<C2Work> &work)
{
    //LOGV("entry");
    c2_status_t ret = C2_OK;
    C2ReadView view = mDummyReadView;
    size_t size = 0u;
    if (!work->input.buffers.empty()) {
        view = work->input.buffers[0]->data().linearBlocks().front().map().get();
        size = view.capacity();
    }

    bool eos = (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0;
    bool codecConfig = (work->input.flags & C2FrameData::FLAG_CODEC_CONFIG) != 0;
    uint64_t timestamp = work->input.ordinal.timestamp.peeku();
    uint64_t frameIndex = work->input.ordinal.frameIndex.peeku();
    LOGV("ts %lld , size %zu, eos %d, frameIndex %lld", (long long)timestamp,size, eos, (long long)frameIndex);

    if (codecConfig && size > 0u) 
    {
        LOGV("audio codec config data received.\n");
        
        if(C2_OK != checkCodecConfig(view.data(), size))
            LOGE("checkCodecConfig fail!");
        work->worklets.front()->output.flags = work->input.flags;
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->worklets.front()->output.buffers.clear();
        work->workletsProcessed = 1u;

        return C2_OK;
    }

    // vts sends last input with index 0, this leads to reset actions wrongly
    // add conditions of eos and data len to avoid it.
    if(frameIndex == 0l && !(eos && (AudioRingBuffer.AudioDataLen() > 0)))
    {
        LOGV("first frame received.\n");
        AudioRingBuffer.BufferReset();
        TS_Manager.Reset();
        doReset();
        bReceivedEOS = false;
        bDecoderEOS = false;
        bCheckFrameHeader = true;
    }

    if(eos)
    {
    	bReceivedEOS = true;
        LOGV("EOS received.\n");
    }

    if (bFirstInBuffer && eos)
    {
    	LOGV("bFirstInBuffer && eos. call getParamDirectly\n");
        getParamDirectly();
    }

    if (bFirstInBuffer)
        bFirstInBuffer = false;

    TS_Manager.TS_Add(timestamp, size);

    if (size > 0)
        mFrameLenQueue.push(size);

    return C2_OK;
}

c2_status_t IMXAudioDecComponent::_processInputData(const std::unique_ptr<C2Work> &work)
{
    //LOGV("entry");
    c2_status_t ret = C2_OK;
    uint32_t nActualLen = 0;
    uint8_t * pBuffer = nullptr;
    C2ReadView view = mDummyReadView;
    size_t size = 0;

    if(ePlayMode == DEC_STREAM_MODE && nRequiredSize > 0 && AudioRingBuffer.AudioDataLen() > nRequiredSize){
        //LOGV("don't handle this input becauseof stream mode");
        //need modify for codec2
        //return ret;
    }

    if (!work->input.buffers.empty()) {
        view = work->input.buffers[0]->data().linearBlocks().front().map().get();
        size = view.capacity();
        pBuffer = const_cast<uint8_t *>(view.data());
    }

    /** Process audio data */
    if(size > 0 && pBuffer != nullptr){
        (void)parseInputFrame(pBuffer, size);
        AudioRingBuffer.BufferAdd(pBuffer, size,  &nActualLen);
        LOGV("add input %zu to ringBuffer, %d added, total %d",  size, nActualLen, AudioRingBuffer.AudioDataLen());
        /*
        LOGV("%2x %2x %2x %2x %2x, %2x %2x %2x %2x %2x ",
            pBuffer[0],pBuffer[1],pBuffer[2],pBuffer[3],pBuffer[4],
            pBuffer[size-5],pBuffer[size-4],pBuffer[size-3],pBuffer[size-2],pBuffer[size-1]
            ); */
    }
    else{
        // no data in this work, don't keep it in mPendingWork
        work->workletsProcessed = 1u;
    }

    //if(nActualLen > 0)
    //    _fileDump(&fpOutput, pBuffer, nActualLen);

    if (nActualLen < size)
    {
        LOGW("ringbuffer full! added %d, expect %zu, total %d", nActualLen, size, AudioRingBuffer.AudioDataLen());
        uint32_t left = size - nActualLen;
        uint32_t newLen = MAX(AudioRingBuffer.AudioDataLen() * 2, size+1024);
        if(newLen > 10*1024*1024 /*max buffer size*/){
            LOGE("ringbuffer size reaches upper limit!");
            return C2_OK;
        }
        AudioRingBuffer.Resize(newLen);
        AudioRingBuffer.BufferAdd(pBuffer + nActualLen, left, &nActualLen);
        if (nActualLen < left){
            LOGE("Can't resize ringbuffer to receive all data!");
        }
    }

    return C2_OK;

}

c2_status_t IMXAudioDecComponent::_processOutput(const std::unique_ptr<C2Work> &work)
{
    LOGV("entry");
    int64_t nTimeStamp = 0l;
    uint8_t * pOutBuffer = nullptr;
    uint32_t nOutBufferLen = 0;
    uint32_t nOutBufferOffset = 0;
    c2_status_t ret = C2_OK;
    std::list<uint8_t *> outputBuffers;
    std::list<uint32_t> outputSizes;
    AUDIO_DECODE_RETURN_TYPE DecodeRet;
    bool eos;

    if (bFirstOutput) {
        TS_Manager.Consumed(0);
        TS_Manager.TS_SetIncrease(0);
    }
    TS_Manager.TS_Get(&nTimeStamp);

    if (mTimestampDelta > 0 && mTimestampDelta < nTimeStamp)
        nTimeStamp -= mTimestampDelta;
    LOGV("tsm output ts %lld mTimestampDelta %lld", (long long)nTimeStamp, (long long)mTimestampDelta);

    if (bDecoderInitFail)
    {
    	AudioRingBuffer.BufferReset();
    	TS_Manager.Reset();
    	bReceivedEOS = true;
        DecodeRet = AUDIO_DECODE_EOS;
        LOGW("InitFail, set bReceivedEOS and skip decoding");
    }
    else
    {
        DecodeRet = doDecode(work, outputBuffers, outputSizes);
        if(DecodeRet != AUDIO_DECODE_SUCCESS){
            LOGV("doDecode return %s", _decodeRetToString(DecodeRet));
        }

    	if (DecodeRet == AUDIO_DECODE_FAILURE)
    	{
            LOGW("Decode frame fail!.\n");
            return C2_OK;
    	}else if(DecodeRet == AUDIO_DECODE_FATAL_ERROR){
            LOGE("Decode fatal error!, set EOS.\n");
            AudioRingBuffer.BufferReset();
            TS_Manager.Reset();
            bReceivedEOS = true;
            DecodeRet = AUDIO_DECODE_EOS;
        }
    }

    if (DecodeRet == AUDIO_DECODE_EOS && bReceivedEOS == true && AudioRingBuffer.AudioDataLen() == 0)
    {
        //if buffer is both the first and the last one, and is empty, send out EOS event directly.
        if((bFirstOutput && work->worklets.front()->output.buffers.size() == 0)) {
            LOGD("AudioDecComp send EOS at first output\n");
            uint32_t flags = work->worklets.front()->output.flags;
            flags |= C2FrameData::FLAG_END_OF_STREAM;
            work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
            bDecoderEOS = true;
        }
    }

    // when getting first output, don't checkFrameHeader anymore
    if (bCheckFrameHeader)
    {
        bCheckFrameHeader = false;
    }

    //if(DecodeRet == AUDIO_DECODE_NEEDMORE){
    //    return C2_OK;
    //}

    //
    //malloc pOutBuffer, copy output data from list outputBuffers
    //
    if(!outputBuffers.empty() && !outputSizes.empty()){
        nOutBufferLen = std::accumulate(outputSizes.begin(), outputSizes.end(), 0);
        pOutBuffer = (uint8_t *)malloc(nOutBufferLen);
        if(!pOutBuffer){
            LOGE("malloc output buffer fail! size %d", nOutBufferLen);
            nOutBufferLen = 0;
        }
        else{
            uint32_t index = 0;
            while(!outputSizes.empty() && !outputBuffers.empty()){
                uint8_t * p = *outputBuffers.begin();
                outputBuffers.erase(outputBuffers.begin());
                uint32_t size = *outputSizes.begin();
                outputSizes.erase(outputSizes.begin());
                if(p != nullptr && size > 0){
                    memcpy(pOutBuffer+index, p, size);
                    index += size;
                    free(p);
                }
            }
        }

    }

    if(pOutBuffer == nullptr || nOutBufferLen == 0){
        LOGV("No valid output for this decoding");
        return C2_OK;
    }

    eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);

    if(bFirstOutput) {
        if(C2_OK == handleBOS(&nOutBufferOffset, nOutBufferLen))
            bFirstOutput = false;
    }

    if (eos) {
        handleEOS(&pOutBuffer, &nOutBufferLen);
    }

    if(nOutBufferLen <= nOutBufferOffset){
        LOGV("No valid output for this decoding because of nOutBufferOffset %d", nOutBufferOffset);
        return C2_OK;
    }

    //
    // Convert to 16 bits if necessary, result is in pData/dataLen
    //
    uint32_t dataLen = nOutBufferLen - nOutBufferOffset;
    uint8_t * pData = pOutBuffer + nOutBufferOffset;
    LOGV("nOutBufferLen %d nOutBufferOffset %d", nOutBufferLen, nOutBufferOffset);

    //LOGV("bConvertEnable %d, pConvertBuffer %p",  bConvertEnable, pConvertBuffer);
    if(bConvertEnable && pConvertBuffer != nullptr){
        uint32_t outSize = 0;
        if(PcmMode.nBitPerSample != 16){
            nOutputBitPerSample = PcmMode.nBitPerSample;
            PcmMode.nBitPerSample = 16;
        }
        //LOGV("nOutputBitPerSample %d",  nOutputBitPerSample);
        if(nOutputBitPerSample != 16){
            // check convert buffer size
            if(nOutputBitPerSample == 8 && nConvertBufferSize < dataLen * 2){
                LOGV("enlarge pConvertBuffer %d=>%d", nConvertBufferSize, dataLen * 2);
                nConvertBufferSize = dataLen * 2;
                pConvertBuffer = (uint8_t *)realloc(pConvertBuffer, nConvertBufferSize);
            }
            if(nOutputBitPerSample == 24 && nConvertBufferSize < dataLen * 2 / 3){
                LOGV("enlarge pConvertBuffer %d=>%d", nConvertBufferSize, dataLen * 2 / 3);
                nConvertBufferSize = dataLen * 2 / 3;
                pConvertBuffer = (uint8_t *)realloc(pConvertBuffer, nConvertBufferSize);
            }
            // do converting...
            if(pConvertBuffer != nullptr && C2_OK == _convertData(pConvertBuffer,&outSize,pData,dataLen)){
                LOGV("convert ok");
                pData = pConvertBuffer;
                dataLen = outSize;
            }
        }
    }

#if 1

    std::shared_ptr<C2LinearBlock> block;
    C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };

    do{
        LOGV("fetchLinearBlock... len %d, usage %d ",  dataLen, (int32_t)usage.expected);
        c2_status_t err = mOutputBlockPool->fetchLinearBlock(dataLen, usage, &block);
        if (err != C2_OK){
            LOGE("fetchLinearBlock fail!");
            work->result = C2_NO_MEMORY;
            ret = C2_NO_MEMORY;
            break;
        }
        //LOGI("map to WriteView");
        C2WriteView wView = block->map().get();
        if (wView.error() != C2_OK){
            LOGE("block->map() fail!");
            work->result = wView.error();
            ret = work->result;
            break;
        }
        uint8_t *pBuffer = reinterpret_cast<uint8_t *> (wView.data());
        memcpy(pBuffer, pData, dataLen);

        //_fileDump(&fpOutput, pData, dataLen);
    }while(0);
    
    free(pOutBuffer);

    if(C2_OK == ret){
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.buffers.push_back(createLinearBuffer(block, 0, dataLen));
    }
    work->worklets.front()->output.flags = work->input.flags;
    work->worklets.front()->output.ordinal.frameIndex = work->input.ordinal.frameIndex.peeku();

    // for imxextractor
    work->worklets.front()->output.ordinal.timestamp = nTimeStamp;

    work->workletsProcessed = 1u;

    LOGV("output size %d, ts %lld, frameIndex %lld,configUpdate %zu, flags 0x%x",
        dataLen, (long long)work->worklets.front()->output.ordinal.timestamp.peeku(),
        (long long)work->worklets.front()->output.ordinal.frameIndex.peeku(),
        work->worklets.front()->output.configUpdate.size(), work->worklets.front()->output.flags);

    //_fileDump(&fpOutput, work);
    //_compareData(work, pData, dataLen);

#else
    //_fileDump(&fpOutput, pData, dataLen);

    nTimeStamp = work->input.ordinal.timestamp.peeku();
    LOGV("use ts from input %lld ms", (long long)nTimeStamp/1000);

    // keep input work in ComponentBase, use finish() to let it return work
    _drainOutput(work, pData, dataLen, nTimeStamp, false);

#endif

    return ret;
}

void IMXAudioDecComponent::_fileDump(FILE** ppFp, uint8_t * pBits, uint32_t nSize)
{
    static const char * fileName = "/data/audioComp.bit";
    if(nSize==0)
        return;

    if(*ppFp==nullptr)
    {
        // dump to file only when this file doesn't exist
        // avoid dumping file everytime playing
        // remove this file before you want to dump data
        struct stat sb;
        if (stat(fileName, &sb) != -1)
            return;

        *ppFp=fopen(fileName,"wb");
        if(*ppFp==nullptr)
        {
            LOGE("Can't open %s", fileName);
            return;
        }
        LOGI("Open %s OK", fileName);
    }

    fwrite(pBits,1,nSize,*ppFp);
    fflush(*ppFp);
    return;
}

void IMXAudioDecComponent::_fileDump(FILE** ppFp, const std::unique_ptr<C2Work> &work)
{
    static const char * fileName = "/data/audioComp.bit";

    if(*ppFp==nullptr)
    {
        // dump to file only when this file doesn't exist
        // avoid dumping file everytime playing
        // remove this file before you want to dump data
        struct stat sb;
        if (stat(fileName, &sb) != -1)
            return;

        *ppFp=fopen(fileName,"wb");
        if(*ppFp==nullptr)
        {
            LOGE("Can't open %s", fileName);
            return;
        }
        LOGI("Open %s OK", fileName);
    }

    std::vector<std::shared_ptr<C2Buffer>> &buffers = work->worklets.front()->output.buffers;
    if(buffers.size() < 1){
        LOGD("no buffer in this work");
        return;
    }

    if(buffers.size() != 1){
        LOGD("contain more than one output buffer %zu!", buffers.size());
    }
    std::shared_ptr<C2Buffer> buf = buffers[0];
    C2ReadView view = buf->data().linearBlocks()[0].map().get();
    if (view.error() != C2_OK) {
        LOGD("Error while mapping: %d", view.error());
        return;
    }

    fwrite(view.data(),1,view.capacity(),*ppFp);
    fflush(*ppFp);
    return;
}

void IMXAudioDecComponent::_compareData(const std::unique_ptr<C2Work> &work, uint8_t * pBits, uint32_t nSize)
{
    std::vector<std::shared_ptr<C2Buffer>> &buffers = work->worklets.front()->output.buffers;
    if(buffers.size() < 1){
        LOGD("no buffer in this work");
        return;
    }

    if(buffers.size() != 1){
        LOGD("contain more than one output buffer %zu!", buffers.size());
    }
    std::shared_ptr<C2Buffer> buf = buffers[0];
    C2ReadView view = buf->data().linearBlocks()[0].map().get();
    if (view.error() != C2_OK) {
        LOGD("Error while mapping: %d", view.error());
        return;
    }

    for(int i=0; i<nSize; i++){
        if(*(view.data()+i) != *(pBits+i))
            LOGI("mismatch index %d, write %02x, read %02x" ,i, *(pBits+i), *(view.data()+i));
    }
}

c2_status_t IMXAudioDecComponent::_convertData(uint8_t * pOut, uint32_t *nOutSize, uint8_t *pIn, uint32_t nInSize)
{
    LOGV("entry");
    if(nullptr == pOut || nullptr == pIn)
        return C2_BAD_VALUE;

    if(nOutputBitPerSample == 8) {
        // Convert to U16
        int32_t i,Len;
        uint16_t *pDst =(uint16_t *)pOut;
        int8_t *pSrc =(int8_t *)pIn;
        Len = nInSize;
        for(i=0;i<Len;i++) {
            *pDst++ = (uint16_t)(*pSrc++) << 8;
        }
        nInSize *= 2;
    } else if(nOutputBitPerSample == 24) {
        int32_t i,j,Len;
        uint8_t *pDst =(uint8_t *)pOut;
        uint8_t *pSrc =(uint8_t *)pIn;
        Len = nInSize / (nOutputBitPerSample>>3) / PcmMode.nChannels;
        for(i=0;i<Len;i++) {
            for(j=0;j<(int32_t)PcmMode.nChannels;j++) {
                pDst[0] = pSrc[1];
                pDst[1] = pSrc[2];
                pDst+=2;
                pSrc+=3;
            }
        }
        nInSize = Len * (16>>3) * PcmMode.nChannels;
    }

    *nOutSize = nInSize;
    return C2_OK;
}

void IMXAudioDecComponent::_drainOutput(
        const std::unique_ptr<C2Work> &currInputWork,
        uint8_t * pData,
        uint32_t dataLen,
        int64_t nTimeStamp,
        bool eos) {
    LOGV("entry");
    std::shared_ptr<C2LinearBlock> block;

    std::function<void(const std::unique_ptr<C2Work>&)> fillWork =
        [&block, pData, dataLen, nTimeStamp, this, &currInputWork](const std::unique_ptr<C2Work> &work){

            if(work == nullptr){
                LOGW("fillWork() got null work");
                return;
            }

            C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
            c2_status_t err = mOutputBlockPool->fetchLinearBlock(
                    dataLen, usage, &block);
            if (err != C2_OK) {
                LOGE("fillWork() failed to fetch a linear block (%d)", err);
                return;
            }
            C2WriteView wView = block->map().get();
            uint8_t *pBuffer = reinterpret_cast<uint8_t *> (wView.data());
            memcpy(pBuffer, pData, dataLen);

            #define DUMP_OUTPUT
            #ifdef DUMP_OUTPUT
                //_fileDump(&fpOutput, pData, dataLen);
            #endif

            work->result = C2_OK;
            C2FrameData &output = work->worklets.front()->output;
            output.flags = work->input.flags; // shall be currInputWork flags?
            output.buffers.clear();
            output.buffers.push_back(createLinearBuffer(block, 0, dataLen));
            //output.ordinal = work->input.ordinal;
            output.ordinal.timestamp = nTimeStamp;
            output.ordinal.frameIndex = ++frameIndex;
            std::vector<std::unique_ptr<C2Param>> *configUpdate = &currInputWork->worklets.front()->output.configUpdate;
            if (!configUpdate->empty()){
                LOGI("fillWork() copy config to output, size %zu", configUpdate->size());
                std::vector<std::unique_ptr<C2Param>> * outputConfig = &work->worklets.front()->output.configUpdate;
                outputConfig->clear();
                for (auto it = configUpdate->begin() ; it != configUpdate->end(); ++it)
                    outputConfig->push_back(std::move(*it));
            }
            work->workletsProcessed = 1u;
            LOGI("fillWork() out timestamp %lld, size %u, frameIndex %d, flags %x", 
                (long long)nTimeStamp/1000, block ? block->capacity() : 0, frameIndex, output.flags);
        };

        finish(nTimeStamp, fillWork);

}


const char * IMXAudioDecComponent::_decodeRetToString(AUDIO_DECODE_RETURN_TYPE decodeRet)
{
    switch(decodeRet){
        case AUDIO_DECODE_EOS:
            return "AUDIO_DECODE_EOS";
        case AUDIO_DECODE_FAILURE:
            return "AUDIO_DECODE_FAILURE";
        case AUDIO_DECODE_NEEDMORE:
            return "AUDIO_DECODE_NEEDMORE";
        case AUDIO_DECODE_FATAL_ERROR:
            return "AUDIO_DECODE_FATAL_ERROR";
        default:
            return "invalid";
    };
}

c2_status_t IMXAudioDecComponent::drainInternal(uint32_t drainMode)
{
    LOGI("entry");
    return C2_OK;
}

}

