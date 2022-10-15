/**
 *  Copyright 2019,2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "UniaDecodr"
#include <log/log.h>
#include "UniaDecoder.h"
#include "AudioFrameParser.h"
#include <C2Config.h>
#include <dlfcn.h>

#define MAX_PROFILE_ERROR_COUNT 1500 //about 1 second decoding time, 30 seconds' audio data length
#define TICKS_PER_SECOND 1000000
#define IS_ERROR(ret) ret == ACODEC_ERROR_STREAM \
                  || ret == ACODEC_PARA_ERROR \
                  || ret == ACODEC_INSUFFICIENT_MEM \
                  || ret == ACODEC_ERR_UNKNOWN \
                  || ret == ACODEC_PROFILE_NOT_SUPPORT \
                  || ret == ACODEC_INIT_ERR
#define LOG_RET(ret, FMT, ...) {if(IS_ERROR(ret))LOGE(FMT, ##__VA_ARGS__); \
                                else LOGV(FMT, ##__VA_ARGS__);}

namespace android {

static std::unordered_map<UA_ParaType, const char *> gUniaParam2StringMap = {
    {UNIA_SAMPLERATE,           "UNIA_SAMPLERATE"},
    {UNIA_CHANNEL,              "UNIA_CHANNEL"},
    {UNIA_FRAMED,               "UNIA_FRAMED"},
    {UNIA_DEPTH,                "UNIA_DEPTH"},
    {UNIA_CODEC_DATA,           "UNIA_CODEC_DATA"},
    {UNIA_BITRATE,              "UNIA_BITRATE"},
    {UNIA_DOWNMIX_STEREO,       "UNIA_DOWNMIX_STEREO"},
    {UNIA_STREAM_TYPE,          "UNIA_STREAM_TYPE"},
    {UNIA_CHAN_MAP_TABLE,       "UNIA_CHAN_MAP_TABLE"},
    {UNIA_WMA_BlOCKALIGN,       "UNIA_WMA_BlOCKALIGN"},
    {UNIA_WMA_VERSION,          "UNIA_WMA_VERSION"},
    {UNIA_RA_FLAVOR_INDEX,      "UNIA_RA_FLAVOR_INDEX"},
    {UNIA_RA_FRAME_BITS,        "UNIA_RA_FRAME_BITS"},
    {UNIA_MP3_DEC_CRC_CHECK,    "UNIA_MP3_DEC_CRC_CHECK"},
    {UNIA_MP3_DEC_MCH_ENABLE,   "UNIA_MP3_DEC_MCH_ENABLE"},
    {UNIA_OUTPUT_PCM_FORMAT,    "UNIA_OUTPUT_PCM_FORMAT"},
    {UNIA_CONSUMED_LENGTH,      "UNIA_CONSUMED_LENGTH"},
};

/*****************************************************************************
 * Function:    appLocalMalloc
 *
 * Description: Implements the local malloc
 *
 * Return:      Value of the address.
 *
 ****************************************************************************/

static void* appLocalMalloc (uint32 TotalSize)
{

    void *PtrMalloc = nullptr;

    if(0 == TotalSize)
        LOGW("\nWarning: ZERO size IN LOCAL MALLOC");

    PtrMalloc = malloc(TotalSize);

    if (PtrMalloc == nullptr) {

        LOGE("\nError: MEMORY FAILURE IN LOCAL MALLOC");
    }
    return (PtrMalloc);
}


/*****************************************************************************
 * Function:    appLocalFree
 *
 * Description: Implements to Free the memory.
 *
 * Return:      Void
 *
 ****************************************************************************/
static void appLocalFree (void *MemoryBlock)
{
    if(MemoryBlock != nullptr)
        free(MemoryBlock);
}

static void *appLocalCalloc(uint32 TotalNumber, uint32 TotalSize)
{
    void *PtrCalloc = nullptr;

    if((0 == TotalSize)||(0==TotalNumber))
        LOGW("\nWarning: ZERO size IN LOCAL CALLOC");

    PtrCalloc = malloc(TotalNumber*TotalSize);

    if (PtrCalloc == nullptr) {

        LOGE("\nError: MEMORY FAILURE IN LOCAL CALLOC");
                return nullptr;
    }
    memset(PtrCalloc, 0, TotalSize*TotalNumber);
    return (PtrCalloc);
}

/*****************************************************************************
 * Function:    appLocalReAlloc
 *
 * Description: Implements to Free the memory.
 *
 * Return:      Void
 *
 ****************************************************************************/
static void * appLocalReAlloc (void *MemoryBlock, uint32 TotalSize)
{
    void *PtrMalloc = nullptr;

    if(0 == TotalSize)
        LOGW("\nWarning: ZERO size IN LOCAL REALLOC");

    PtrMalloc = (void *)realloc(MemoryBlock, TotalSize);
    if (PtrMalloc == nullptr) {
        LOGE("\nError: MEMORY FAILURE IN LOCAL REALLOC");
    }

    return PtrMalloc;
}
UniaDecoder::UniaDecoder(const std::shared_ptr<C2ComponentInterface> &intf, AudioDecodeUtil *pDecodeUtil)
    : IMXAudioDecComponent(intf),
      mUtil(pDecodeUtil)
{
    LOGV("entry %p", this);
    nPushModeInputLen = mUtil->getPushModeInputLen();
    nRingBufferScale = RING_BUFFER_SCALE;

    //these values must be reset by subclass
    errorCount = 0;
    profileErrorCount = 0;

    accumulatedOutputLen = 0;
    mWrapper = nullptr;
    mWrapperHandle = nullptr;
    mLibHandle = nullptr;
    codecConfig.buf = nullptr;

    mDecodeEachInput = 0;
}

UniaDecoder::~UniaDecoder()
{
    LOGV("entry");
}

c2_status_t UniaDecoder::doInit()
{
    LOGV("entry");
    AUDIOFORMAT format = FORMAT_UNKNOW;
    bool hwBased = false;

    // from InitComponent
    c2_status_t ret = C2_OK;
    LOGD("UniaDecoder::doInit");

    memOps.Calloc = appLocalCalloc;
    memOps.Malloc = appLocalMalloc;
    memOps.Free= appLocalFree;
    memOps.ReAlloc= appLocalReAlloc;

    errorCount = 0;
    inputFrameCount = 0;
    consumeFrameCount= 0;
    profileErrorCount = 0;
    mWrapper = nullptr;
    mLibHandle = nullptr;

    if(nPushModeInputLen == 0){
        LOGE("audio decoder param not inited!");
        return C2_BAD_VALUE;
    }
    PcmMode.nChannels = 2;
    PcmMode.nBitPerSample = 16;
    PcmMode.nSamplingRate = 44100;

    ret = _createWrapperInterface();
    if(ret != C2_OK){
        LOGE("_createWrapperInterface fail");
        return ret;
    }

    // from AudioFilterInstanceInit
    codecConfig.buf = nullptr;
    codecConfig.size = 0;
    errorCount = 0;
    profileErrorCount = 0;
    mWrapperHandle = nullptr;
    inputBufferCount = 0;
    framedInputBufferCount = 0;

    do{
        LOGI("SetupWrapper %s\n", mWrapper->GetVersionInfo());

        ret = mUtil->getDecoderProp(&format, &hwBased);

        if (ret == C2_OK && mWrapper->CreateDecoderPlus != nullptr && hwBased)
            mWrapperHandle = mWrapper->CreateDecoderPlus(&memOps, format);
        else
            mWrapperHandle = mWrapper->CreateDecoder(&memOps);

        if(ret == C2_OMITTED)
            ret = C2_OK;

        if(mWrapperHandle == nullptr){
            LOGE("create decoder fail");
            ret = C2_OMITTED;
            break;
        }

        _initWrapperWithParam();
    }while(0);
    LOGD("doInit success");
    return ret;
}

c2_status_t UniaDecoder::handleEOS(uint8_t **ppBuffer, uint32_t* length)
{
    LOGV("entry");
    return mUtil->handleEOS(ppBuffer, length);
}

c2_status_t UniaDecoder::handleBOS(uint32_t* offset, uint32_t length)
{
    LOGV("entry");
    uint64_t off1, off2;
    c2_status_t err = C2_OK;

    off1 = *offset;
    err = mUtil->handleBOS(offset, length);
    off2 = *offset;

    if (off2 > off1) {
        // need adjust timestamp because handleBOS cut samples from this buffer
        uint64_t bitrate = PcmMode.nChannels*PcmMode.nSamplingRate*nOutputBitPerSample;
        LOGV("bitrate %lld handled length %lld", (long long)bitrate, (long long)(off2-off1));
        mTimestampDelta += (int64_t)((off2-off1)*8*TICKS_PER_SECOND + bitrate - 1)/bitrate;
    }
    return err;
}

AUDIO_DECODE_RETURN_TYPE UniaDecoder::doDecode(const std::unique_ptr<C2Work> &work,
                                                    std::list<uint8_t *> &outputBuffers, std::list<uint32_t> &outputSizes)
{
    LOGV("entry");
    AUDIO_DECODE_RETURN_TYPE ret;

    // MA-17241 aac dsp decoder needs calling DecodeFrame() again to output valid data sometimes
    int32_t retry=0;
    const int32_t maxRetryCount = 1;

    int32_t loopCount = 0;
    const int32_t maxLoopCount = 500;

    do{
        uint32_t nTmpOutBufferLen = 0;
        uint8_t * pTmpOutBuffer = nullptr;
        ret = _doDecodeInternal(work, &pTmpOutBuffer, &nTmpOutBufferLen);
        if(pTmpOutBuffer != nullptr){
            //_calcCheckSum(pTmpOutBuffer, nTmpOutBufferLen);
            outputBuffers.push_back(pTmpOutBuffer);
            outputSizes.push_back(nTmpOutBufferLen);
            LOGV("push %d to outputSizes", nTmpOutBufferLen);
        }
        if(ret == AUDIO_DECODE_NEEDMORE
            && AudioRingBuffer.AudioDataLen() > 0
            && retry++ < maxRetryCount){
            LOGV("retry for NEEDMORE");
            continue;
        }

        if(ret == AUDIO_DECODE_FATAL_ERROR || ret == AUDIO_DECODE_EOS)
            break;

        if(AudioRingBuffer.AudioDataLen() < nPushModeInputLen && !bReceivedEOS)
            break;

        // to avoid dead loop while core decoder returns InputOffset=0 ,
        // no chance to decrease ringbuffer data length to break from while loop
        // AAC_LC_44kHz_128kbps_1_SSR.aac, cts bug_25928803.mp4
        if(loopCount++ > maxLoopCount){
            LOGE("core decoder fails to return valid InputOffset after %d retries", maxLoopCount);
            return AUDIO_DECODE_FATAL_ERROR;
        }
    }while(true);

    return ret;
}

AUDIO_DECODE_RETURN_TYPE UniaDecoder::_doDecodeInternal(
    const std::unique_ptr<C2Work> &work, uint8_t **ppOutputBuffer, uint32_t *pOutputSize)
{
    LOGV("entry");
    AUDIO_DECODE_RETURN_TYPE ret = AUDIO_DECODE_SUCCESS;
    int32_t decoderRet = ACODEC_SUCCESS;
    uint32_t nActualOutLen = 0;
    //uint32_t InputOffsetBegin = 0;
    uint32_t consumeLen = 0;
    uint32_t InputLen = 0;;
    uint32_t InputOffset = 0;
    uint8_t * pInputBuffer = nullptr;
    *ppOutputBuffer = nullptr;
    *pOutputSize = 0;
    UniaDecFrameInfo FrameInfo;
    memset(&FrameInfo, 0, sizeof(UniaDecFrameInfo));

    AudioRingBuffer.BufferGet(&pInputBuffer, nPushModeInputLen, &InputLen);
    LOGV("nPushModeInputLen %d,InputLen %d,isFrameInput %d",
              nPushModeInputLen, InputLen, mUtil->isFrameInput());

    if(ePlayMode == DEC_STREAM_MODE && C2_OK == mUtil->parseFrame(pInputBuffer,InputLen,&FrameInfo)){
        if(FrameInfo.bGotOneFrame){
            LOGV("got one frame,size=%d,next=%d,header=%d"
                ,FrameInfo.nFrameSize,FrameInfo.nNextSize,FrameInfo.nHeaderSize);
            uint32_t nActualInLen = 0;
            InputLen = FrameInfo.nFrameSize;
            InputOffset = 0;
            AudioRingBuffer.BufferGet(&pInputBuffer, InputLen, &nActualInLen);

            if(FrameInfo.nNextSize > 0)
                nRequiredSize = FrameInfo.nNextSize+DEFAULT_REQUIRED_SIZE;
            else
                nRequiredSize = 0;

            if(nActualInLen < InputLen){
                LOGE("Can't get a full frame(%d) from ringbuffer, nActualLen=%d",InputLen, nActualInLen);
                return ret;
            }
        }else{
            nRequiredSize = FrameInfo.nFrameSize+DEFAULT_REQUIRED_SIZE;
            LOGI("can't get one frame,size=%d",FrameInfo.nFrameSize);
            return ret;
        }
    } else if (mUtil->isFrameInput() && InputLen != 0) {
        uint32_t nFrameLen = 0;

        if (mFrameLenQueue.size() > 0)
            nFrameLen = mFrameLenQueue.front();

        if (nFrameLen == 0)
            nFrameLen = nPushModeInputLen;;

        AudioRingBuffer.BufferGet(&pInputBuffer, nFrameLen, &InputLen);
    }

    //struct timeval tv, tv1;
    //gettimeofday (&tv, nullptr);
    //InputOffsetBegin = InputOffset;

    uint32_t outputBufferSize = mUtil->getOutBufferLen();
    uint8_t * pOutputBuffer = (uint8_t *)malloc(outputBufferSize);
    if(!pOutputBuffer){
        LOGE("malloc output buffer fail! size %d", outputBufferSize);
        return AUDIO_DECODE_FAILURE;
    }

    LOGV("now decoding...  InputLen %d, outBufSize %d",InputLen, outputBufferSize);

    if(pInputBuffer && InputLen > 0){
        decoderRet = mWrapper->DecodeFrame(mWrapperHandle,pInputBuffer,InputLen,
            &InputOffset,&pOutputBuffer,&nActualOutLen);
        if(decoderRet == ACODEC_ERR_UNKNOWN && InputOffset == 0)
            InputOffset = InputLen; // skip this block of data, avoid dead loop in doDecode
        AudioRingBuffer.BufferConsumed(InputOffset);
        if (InputOffset > 0) {
            uint32_t tempSize = 0;
            while (mFrameLenQueue.size() > 0) {
                tempSize += mFrameLenQueue.front();
                if (InputOffset >= tempSize)
                    mFrameLenQueue.pop();
                if (InputOffset <= tempSize)
                    break;
            }
        }
        LOGV("ringbuffer consumed InputOffset %d, left %d",InputOffset, AudioRingBuffer.AudioDataLen());
        inputFrameCount += InputOffset;
    }else{
        decoderRet = mWrapper->DecodeFrame(mWrapperHandle,nullptr,0,&InputOffset,&pOutputBuffer,&nActualOutLen);
    }
    mWrapper->GetParameter(mWrapperHandle,UNIA_CONSUMED_LENGTH,(UniACodecParameter*)&consumeLen);

    LOG_RET(decoderRet, "wrapperRet \"%s\", InputOffset %d, consumeLen %d, nActualOutLen %d",
                    _wrapperRet2string(decoderRet),InputOffset,consumeLen, nActualOutLen);
    //gettimeofday (&tv1, nullptr);
    //LOGD("AudioFilterFrame decode cost: %d\n", (tv1.tv_sec-tv.tv_sec)*1000+(tv1.tv_usec-tv.tv_usec)/1000);

    if(decoderRet == ACODEC_SUCCESS || decoderRet == ACODEC_CAPIBILITY_CHANGE){
        /*
        int32_t channelCount = 0;
        int32_t sampleRate = 0;
        if(mUtil->getParameter(UNIA_CHANNEL, &channelCount) != C2_OK
            || mUtil->getParameter(UNIA_SAMPLERATE, &sampleRate) != C2_OK)
            LOGE("Can't get channelCount and sampleRate!");
            */
        if(decoderRet == ACODEC_CAPIBILITY_CHANGE)
                _updateParamFromWrapper(&work->worklets.front()->output.configUpdate);

        TS_PerFrame = (int64_t)nActualOutLen*8*TICKS_PER_SECOND/PcmMode.nChannels \
                /PcmMode.nSamplingRate/nOutputBitPerSample;//PcmMode.nBitPerSample;

        LOGV("TSM Consumed consumeLen %d",consumeLen);
        TS_Manager.Consumed(consumeLen);
        TS_Manager.TS_SetIncrease(TS_PerFrame);

        // finetune ts with total output len to match with vts
        accumulatedOutputLen += nActualOutLen;
        uint64_t totalTS = (int64_t)accumulatedOutputLen*8*TICKS_PER_SECOND/PcmMode.nChannels \
                /PcmMode.nSamplingRate/nOutputBitPerSample;//PcmMode.nBitPerSample;
        int64_t current;
        TS_Manager.TS_Get(&current);
        if(totalTS == (current + 1))
            TS_Manager.TS_SetIncrease(1);

        consumeFrameCount += consumeLen;

        if(nActualOutLen == 0){
            ret = AUDIO_DECODE_NEEDMORE;
        }

        #if 0
        FILE * pfile;
        pfile = fopen("/data/data/pcm","ab");
        if(pfile){
            fwrite(pOutputBuffer,1,nActualOutLen,pfile);
            fclose(pfile);
        }
        #endif
        if(nActualOutLen != 0){
            *ppOutputBuffer = pOutputBuffer;
            *pOutputSize = nActualOutLen;
            profileErrorCount = 0;
        }
    }else if(decoderRet == ACODEC_END_OF_STREAM){
        ret = AUDIO_DECODE_EOS;
    }else if(decoderRet == ACODEC_NOT_ENOUGH_DATA){
        ret = AUDIO_DECODE_NEEDMORE;
    }else if((decoderRet == ACODEC_ERROR_STREAM || decoderRet == ACODEC_ERR_UNKNOWN) && pInputBuffer){
        mWrapper->ResetDecoder(mWrapperHandle);
        TS_Manager.Consumed(consumeLen);
        consumeFrameCount += InputOffset;
        ret = AUDIO_DECODE_FAILURE;
        errorCount ++;
    }else if((decoderRet == ACODEC_ERROR_STREAM || decoderRet == ACODEC_ERR_UNKNOWN) && !pInputBuffer){
        LOGD("SET TO EOS");
        ret = AUDIO_DECODE_EOS;
    }else if(decoderRet == ACODEC_PROFILE_NOT_SUPPORT){
        ret = AUDIO_DECODE_EOS;
        profileErrorCount++;
    }else if(decoderRet == ACODEC_INIT_ERR){
        ret = AUDIO_DECODE_FATAL_ERROR;
    }else if(ACODEC_PARA_ERROR == decoderRet){
        if(C2_OK != mUtil->checkParameter()){
            mWrapper->ResetDecoder(mWrapperHandle);
            ret = AUDIO_DECODE_FATAL_ERROR;
            LOGE("ACODEC_PARA_ERROR check failed");
        }
    }else{
        ret = AUDIO_DECODE_FAILURE;
        errorCount ++;
    }

    //for test usage
    if(errorCount > 500){
        LOGD("Unia Decoder error count > 500 ***!!!");
        errorCount = 0;
    }

    //check if count of profile error reaches the limited.
    if(profileErrorCount > MAX_PROFILE_ERROR_COUNT){
        ret = AUDIO_DECODE_FATAL_ERROR;
        profileErrorCount = 0;
        LOGE("return AUDIO_DECODE_FATAL_ERROR instead of ACODEC_PROFILE_NOT_SUPPORT");
    }

    // outputbuffer doesn't contain valid data, free it here
    if(*ppOutputBuffer == nullptr)
        free(pOutputBuffer);

    return ret;
}

// check whether each input buffer contains one frame or not
c2_status_t UniaDecoder::parseInputFrame(uint8_t * pBuf, size_t size)
{
    c2_status_t ret = C2_OK;
    uint32_t nOffset = 0;
    LOGV("entry");

    if(pBuf == NULL || 0 == size)
        return ret;

    if(++inputBufferCount > 5)
        return ret;

    ret = mUtil->checkFrameHeader(pBuf, size, &nOffset);

    if(ret == C2_OK && mUtil->isFrameInput())
        framedInputBufferCount ++;
    LOGV("checkFrameHeader ret=%d,size=%zu,offset=%u,input=%d,framed=%d",ret , size, nOffset, inputBufferCount, framedInputBufferCount);

    if(inputBufferCount == 5 && framedInputBufferCount == inputBufferCount){
        mDecodeEachInput = true;
        LOGI("set mDecodeEachInput");
    }

    return C2_OK;
}

c2_status_t UniaDecoder::checkFrameHeader()
{
    LOGV("entry");
    uint8_t *pBuffer;
    uint32_t nActualLen = 0;
    uint32_t nOffset = 0;
    c2_status_t ret = C2_OK;
    int32_t value;

    size_t pushModeLen = mUtil->getFrameHdrBufLen();
    if(pushModeLen <= 0)
        return ret;

    AudioRingBuffer.BufferGet(&pBuffer, pushModeLen, &nActualLen);
    LOGV("Get stream length: %d, pushModeLen %zu\n", nActualLen, pushModeLen);

    if (nActualLen < pushModeLen && ePlayMode == DEC_FILE_MODE){
        LOGV("no enough data to check frame header");
        return C2_OK;
    }

    ret = mUtil->checkFrameHeader(pBuffer, nActualLen, &nOffset);
    if(nOffset > 0){
        AudioRingBuffer.BufferConsumed(nOffset);
        TS_Manager.Consumed(nOffset);
    }

    // raw aac format && low latency mode
    mDecodeEachInput = mUtil->isFrameInput() && mUtil->getFrameHdrBufLen()==1;
    LOGI("mDecodeEachInput %d", mDecodeEachInput);

    return ret;
}

c2_status_t UniaDecoder::codecInit()
{
    LOGV("entry");
    c2_status_t ret = C2_OK;
    int32_t value = 0;
    //set stream type here for aac type after got from frame header
    if(C2_OK == mUtil->getParameter(UNIA_STREAM_TYPE,&value)){
        LOGV("mWrapper SetParameter UNIA_STREAM_TYPE %d",value);
        mWrapper->SetParameter(mWrapperHandle,UNIA_STREAM_TYPE,(UniACodecParameter*)&value);
    }
    return ret;
}


void UniaDecoder::doReset()
{
    LOGV("entry");
    inputBufferCount = 0;
    framedInputBufferCount = 0;

    if(mWrapper == nullptr || mWrapperHandle == nullptr){
        LOGE("performReset invalid argument");
        return;
    }

    int32_t ret = mWrapper->ResetDecoder(mWrapperHandle);
    if(ret != ACODEC_SUCCESS){
        LOGE("wrapper ResetDecoder fail! ret=%d",ret);
    }
    return;
}


c2_status_t UniaDecoder::checkCodecConfig(const uint8 * data, size_t size)
{
    LOGV("entry, size %zu", size);
    if(mWrapper == nullptr || mWrapperHandle == nullptr)
        return C2_BAD_VALUE;

    /*
    const uint8 * pBuffer = data;
    LOGV("codec config %2x %2x %2x %2x %2x, %2x %2x %2x %2x %2x ",
        pBuffer[0],pBuffer[1],pBuffer[2],pBuffer[3],pBuffer[4],
        pBuffer[size-5],pBuffer[size-4],pBuffer[size-3],pBuffer[size-2],pBuffer[size-1]
        );
    */
    if (codecConfig.buf != nullptr) {
        codecConfig.buf = (char *)realloc(codecConfig.buf, \
            codecConfig.size + size);
        if (codecConfig.buf == nullptr)
        {
            LOGE("Can't get memory.\n");
            return C2_NO_MEMORY;
        }
        memcpy(codecConfig.buf + codecConfig.size, data, size);
        codecConfig.size += size;

    } else {
         codecConfig.buf = (char *)malloc(size);
        if (codecConfig.buf == nullptr)
        {
            LOGE("Can't get memory.\n");
            return C2_NO_MEMORY;
        }
        memcpy(codecConfig.buf, data , size);
        codecConfig.size = size;
    }

    LOGV("mWrapper SetParameter UNIA_CODEC_DATA len %d", codecConfig.size);
    int32_t ret = mWrapper->SetParameter(mWrapperHandle,UNIA_CODEC_DATA,(UniACodecParameter*)&codecConfig);
    if(ret != ACODEC_SUCCESS){
        LOGE("UniaDec::performCheckCodecConfig fail size %zu,ret=%d",size, ret);
        return C2_CORRUPTED;
    }

    return C2_OK;
}

c2_status_t UniaDecoder::unInit()
{
    LOGV("entry");
    if(codecConfig.buf != nullptr){
        free(codecConfig.buf);
        codecConfig.buf = nullptr;
    }

    if(mWrapper != nullptr && mWrapperHandle != nullptr){
        mWrapper->DeleteDecoder(mWrapperHandle);
        mWrapperHandle = nullptr;
    }

    if (mLibHandle){
        dlclose(mLibHandle);
        mLibHandle = nullptr;
    }

    if(mWrapper != nullptr){
        free(mWrapper);
        mWrapper = nullptr;
    }
    LOGV("UniaDec::DeInitComponent inputFrameCount=%d,consumeFrameCount=%d",inputFrameCount,consumeFrameCount);

    inputBufferCount = 0;
    framedInputBufferCount = 0;
    return C2_OK;
}

c2_status_t UniaDecoder::getParamDirectly()
{
    LOGV("entry");
    c2_status_t ret = C2_OK;
    int32_t decoderRet = ACODEC_SUCCESS;
    UniAcodecOutputPCMFormat outputValue;

    decoderRet = mWrapper->GetParameter(mWrapperHandle,UNIA_OUTPUT_PCM_FORMAT,(UniACodecParameter*)&outputValue);
    if(ACODEC_SUCCESS != decoderRet || outputValue.samplerate == 0 || PcmMode.nSamplingRate == 0) {
        return C2_BAD_VALUE;
    }

    PcmMode.nSamplingRate = outputValue.samplerate;
    PcmMode.nChannels = outputValue.channels;

    return ret;

}

c2_status_t UniaDecoder::_initWrapperWithParam()
{
    int32_t value;

    for ( uint32_t i = UNIA_SAMPLERATE; i < UNIA_STREAM_TYPE; i++){
        if(i == UNIA_CODEC_DATA)
            continue;

        value = 0;
        if(C2_OK == mUtil->getParameter((UA_ParaType)i,&value)){
            LOGI("mWrapper SetParameter %s %d",_uniaParam2string((UA_ParaType)i), value);
            mWrapper->SetParameter(mWrapperHandle,(UA_ParaType)i,(UniACodecParameter*)&value);
        }
    }

    //set output format for channel layout
    memset(&channelTable,0,sizeof(CHAN_TABLE));
    if(C2_OK == mUtil->getParameter(UNIA_CHAN_MAP_TABLE,(int32_t*)&channelTable)){
        LOGI("mWrapper SetParameter UNIA_CHAN_MAP_TABLE");
        mWrapper->SetParameter(mWrapperHandle,UNIA_CHAN_MAP_TABLE,(UniACodecParameter*)&channelTable);
    }

    value = 0;
    if(C2_OK == mUtil->getParameter(UNIA_WMA_BlOCKALIGN,&value)){
        LOGI("mWrapper SetParameter UNIA_WMA_BlOCKALIGN %d", value);
        mWrapper->SetParameter(mWrapperHandle,UNIA_WMA_BlOCKALIGN,(UniACodecParameter*)&value);
    }

    value = 0;
    if(C2_OK == mUtil->getParameter(UNIA_WMA_VERSION,&value)){
        LOGI("mWrapper SetParameter UNIA_WMA_VERSION %d", value);
        mWrapper->SetParameter(mWrapperHandle,UNIA_WMA_VERSION,(UniACodecParameter*)&value);
    }

    value = 0;
    if(C2_OK == mUtil->getParameter(UNIA_RA_FRAME_BITS,&value)){
        LOGI("mWrapper SetParameter UNIA_RA_FRAME_BITS %d", value);
        mWrapper->SetParameter(mWrapperHandle,UNIA_RA_FRAME_BITS,(UniACodecParameter*)&value);
    }

    return C2_OK;
}

void UniaDecoder::_updateParamFromWrapper(std::vector<std::unique_ptr<C2Param>> *configUpdate)
{
    LOGV("entry");
    UniAcodecOutputPCMFormat outputValue;
    int32_t original = 0;
    int32_t neu = 0;

    if(ACODEC_SUCCESS == mWrapper->GetParameter(mWrapperHandle,UNIA_OUTPUT_PCM_FORMAT,(UniACodecParameter*)&outputValue)){
        if(outputValue.samplerate == 0 || outputValue.channels == 0 || outputValue.depth == 0){
            LOGE("bad value: sample rate=%d,channel=%d,depth=%d",
                outputValue.samplerate,outputValue.channels,outputValue.depth);
            return;
        }

        LOGI("PCM FORMAT sample rate %d=>%d, channels %d=>%d, bitdepth %d=>%d",
            PcmMode.nSamplingRate, outputValue.samplerate,
            PcmMode.nChannels, outputValue.channels,
            PcmMode.nBitPerSample, outputValue.depth);
        PcmMode.nSamplingRate = outputValue.samplerate;
        PcmMode.nChannels = outputValue.channels;
        nOutputBitPerSample = PcmMode.nBitPerSample = outputValue.depth;
        //PcmMode.bInterleaved = (bool)outputValue.interleave;

        //Will codec2 use channel mapping ?
        /*
        if(PcmMode.nChannels > 2){
            MapOutputLayoutChannel(&outputValue);
        }

        LOGD("OUTPUT CHANGED channel=%d,layout=%d,%d,%d,%d,%d,%d,%d,%d",PcmMode.nChannels,
            PcmMode.eChannelMapping[0],PcmMode.eChannelMapping[1],PcmMode.eChannelMapping[2],
            PcmMode.eChannelMapping[3],PcmMode.eChannelMapping[4],PcmMode.eChannelMapping[5],
            PcmMode.eChannelMapping[6],PcmMode.eChannelMapping[7]);
        */
    }

    neu = original = 0;
    if(ACODEC_SUCCESS == mWrapper->GetParameter(mWrapperHandle,UNIA_SAMPLERATE,(UniACodecParameter*)&neu)
        && C2_OK == mUtil->getParameter(UNIA_SAMPLERATE, &original)){
        if(neu != original){
            LOGI("update samplereate %d=>%d", original, neu);
            if(C2_OK == mUtil->setParameter(UNIA_SAMPLERATE,neu)){
                C2StreamSampleRateInfo::output sampleRateInfo(0u, neu);
                configUpdate->push_back(C2Param::Copy(sampleRateInfo));
            }
        }
    }

    neu = original = 0;
    if(ACODEC_SUCCESS == mWrapper->GetParameter(mWrapperHandle,UNIA_CHANNEL,(UniACodecParameter*)&neu)
        && C2_OK == mUtil->getParameter(UNIA_CHANNEL, &original)){
        if(neu != original){
            LOGI("update channel num %d=>%d", original, neu);
            if(C2_OK == mUtil->setParameter(UNIA_CHANNEL, neu)){
                C2StreamChannelCountInfo::output channelCountInfo(0u, neu);
                configUpdate->push_back(C2Param::Copy(channelCountInfo));
            }
        }
    }

    neu = original = 0;
    if(ACODEC_SUCCESS == mWrapper->GetParameter(mWrapperHandle,UNIA_BITRATE,(UniACodecParameter*)&neu)
        && C2_OK == mUtil->getParameter(UNIA_BITRATE, &original)){
        if(neu != original && neu != 0){
            LOGI("update bitrate %d=>%d", original, neu);
            if(C2_OK == mUtil->setParameter(UNIA_BITRATE,neu)){
                C2StreamBitrateInfo::input bitrate(0u, neu);
                configUpdate->push_back(C2Param::Copy(bitrate));
            }
        }
    }

    neu = original = 0;
    if(ACODEC_SUCCESS == mWrapper->GetParameter(mWrapperHandle,UNIA_DEPTH,(UniACodecParameter*)&neu)
        && C2_OK == mUtil->getParameter(UNIA_DEPTH, &original)){
        if(neu > 0 && neu != original){
            LOGI("update bitdepth %d=>%d", original, neu);
            mUtil->setParameter(UNIA_DEPTH,neu);
        }
        // todo : send to configUpdate
    }

    return;
}

c2_status_t UniaDecoder::_createWrapperInterface()
{
    int32_t ret = C2_OK;

    do{
        const char * wrapperLibName;
        const char * optionalWrapperLibName;

        mUtil->getLibName(&wrapperLibName, &optionalWrapperLibName);

        tUniACodecQueryInterface  myQueryInterface;

        if(wrapperLibName == nullptr){
            ret = C2_BAD_VALUE;
            break;
        }
        if(optionalWrapperLibName != nullptr){
            mLibHandle = dlopen(optionalWrapperLibName, RTLD_NOW);
            if(mLibHandle != nullptr)
                LOGI("optionalWrapperLibName: %s\n", optionalWrapperLibName);
        }

        if(mLibHandle == nullptr){
            mLibHandle = dlopen(wrapperLibName, RTLD_NOW);
            if(mLibHandle != nullptr)
                LOGI("wrapperLibName: %s\n", wrapperLibName);
        }

        if(mLibHandle == nullptr){
            LOGE("Fail to open Decoder library\n");
            ret = C2_CORRUPTED;
            break;
        }

        mWrapper = (UniaDecInterface *)malloc(sizeof(UniaDecInterface));
        if(mWrapper == nullptr){
            ret = C2_NO_MEMORY;
            break;
        }
        memset(mWrapper,0 ,sizeof(UniaDecInterface));

        myQueryInterface = (tUniACodecQueryInterface)dlsym(mLibHandle, (char *)"UniACodecQueryInterface");
        if(!myQueryInterface)
        {
            LOGE("Fail to query decoder interface\n");
            ret = C2_OMITTED;
            break;
        }

        ret = myQueryInterface(ACODEC_API_GET_VERSION_INFO, (void **)&mWrapper->GetVersionInfo);

        ret |= myQueryInterface(ACODEC_API_CREATE_CODEC, (void **)&mWrapper->CreateDecoder);
        ret |= myQueryInterface(ACODEC_API_DELETE_CODEC, (void **)&mWrapper->DeleteDecoder);
        ret |= myQueryInterface(ACODEC_API_RESET_CODEC, (void **)&mWrapper->ResetDecoder);
        ret |= myQueryInterface(ACODEC_API_SET_PARAMETER, (void **)&mWrapper->SetParameter);
        ret |= myQueryInterface(ACODEC_API_GET_PARAMETER, (void **)&mWrapper->GetParameter);
        ret |= myQueryInterface(ACODEC_API_DEC_FRAME, (void **)&mWrapper->DecodeFrame);
        ret |= myQueryInterface(ACODEC_API_GET_LAST_ERROR, (void **)&mWrapper->GetLastError);

        if(ret != C2_OK){
            LOGE("Fail to query decoder API\n");
            ret = C2_OMITTED;
            break;
        }

        ret = myQueryInterface(ACODEC_API_CREATE_CODEC_PLUS, (void **)&mWrapper->CreateDecoderPlus);
        if (ret != C2_OK){
            LOGD("ACODEC_API_CREATE_CODEC_PLUS is not implemented\n");
            ret = C2_OK;
        }

    }while(0);

    if(ret != C2_OK){
        LOGE("UniaDecoder::CreateDecoderInterface ret=%d",ret);
        if (mLibHandle)
            dlclose(mLibHandle);
        mLibHandle = nullptr;
        free(mWrapper);
        mWrapper = nullptr;
    }

    return (c2_status_t)ret;
}

const char * UniaDecoder::_wrapperRet2string(uint32_t uniaDecodeRet){
    switch(uniaDecodeRet){
        case ACODEC_SUCCESS:             return "success";
        case ACODEC_ERROR_STREAM:        return "error stream";
        case ACODEC_PARA_ERROR:          return "para error";
        case ACODEC_INSUFFICIENT_MEM:    return "insufficient mem";
        case ACODEC_ERR_UNKNOWN:         return "error unknown";
        case ACODEC_PROFILE_NOT_SUPPORT: return "profile not support";
        case ACODEC_INIT_ERR:            return "init error";
        case ACODEC_NO_OUTPUT:           return "no output";
        case ACODEC_NOT_ENOUGH_DATA:     return "not enough data";
        case ACODEC_CAPIBILITY_CHANGE:   return "capability change";
        case ACODEC_END_OF_STREAM:       return "eos";
        default:                         return "unknown";
    }
}

const char * UniaDecoder::_uniaParam2string(UA_ParaType param){
    std::unordered_map<UA_ParaType, const char *>::const_iterator got = gUniaParam2StringMap.find (param);

    if(got == gUniaParam2StringMap.end())
        return "unknown";
    else
        return got->second;
}

void UniaDecoder::_calcCheckSum(uint8_t *pBuffer, uint32_t size)
{
    uint32_t sum = 0;
    for(uint32_t i=0; i<size; i++)
        sum += pBuffer[i];
    LOGI("checksum is %d", sum);
}

}

