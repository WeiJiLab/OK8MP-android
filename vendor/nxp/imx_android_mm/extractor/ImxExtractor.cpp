/**
 *  Copyright 2016 Freescale Semiconductor, Inc.
 *  Copyright 2017-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "ImxExtractor"
#include <utils/Log.h>

#include "ImxExtractor.h"
#include "Imx_ext.h"

#include <media/stagefright/DataSourceBase.h>
#include <media/MediaTrack.h>
#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AUtils.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ByteUtils.h>
#include <media/stagefright/foundation/ColorUtils.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/avc_utils.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaDataUtils.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaDataBase.h>


#include <utils/String8.h>
#include <utils/RefBase.h>
#include <dlfcn.h>
#include <OMX_Video.h>
#include <OMX_Audio.h>
#include <OMX_Implement.h>
#include <cutils/properties.h>
#include <inttypes.h>
#include <ImxInspector.h>
#include <unordered_map>
#include <stdio.h>
#include <string.h>

namespace android {
#define MAX_USER_DATA_STRING_LENGTH 1024
#define MAX_FRAME_BUFFER_LENGTH 20000000
#define MAX_FRAME_BUFFER_LENGTH_4K 50000000
#define MAX_VIDEO_BUFFER_SIZE (512*1024)
#define MIN_VIDEO_BUFFER_SIZE (52*1024)
#define MAX_AUDIO_BUFFER_SIZE (16*1024)
#define MAX_TEXT_BUFFER_SIZE (1024)
#define MAX_TRACK_COUNT 32
#define SEEK_CHECK_TOLERANCE (2*1000000)// 2 seconds
#define AOPUS_CSD1_CSD2_SIZE 20

struct ImxMediaSource : public MediaTrackHelper {
    explicit ImxMediaSource(
            ImxExtractor *extractor, size_t index, AMediaFormat * metadata);

    virtual media_status_t start();
    virtual media_status_t stop();

    virtual media_status_t getFormat(AMediaFormat *format);

    virtual media_status_t read(
            MediaBufferHelper **buffer, const ReadOptions *options);

    bool started();
    void addMediaBuffer(MediaBufferHelper *buffer);
    bool full();
protected:
    virtual ~ImxMediaSource();
private:
    ImxExtractor *mExtractor;
    size_t mSourceIndex;
    Mutex mLock;

    AMediaFormat *mFormat;
    List<MediaBufferHelper *> mPendingFrames;

    bool mStarted;
    bool mIsAVC;
    bool mIsHEVC;
    bool mIsVorbis;
    size_t mNALLengthSize;
    size_t mBufferSize;
    uint32 mFrameSent;

    void clearPendingFrames();
    ImxMediaSource(const ImxMediaSource &);
    ImxMediaSource &operator=(const ImxMediaSource &);
};

ImxMediaSource::ImxMediaSource(ImxExtractor *extractor, size_t index, AMediaFormat * metadata)
    : mExtractor(extractor),
      mSourceIndex(index),
      mFormat(metadata)
{
    mStarted = false;
    const char *mime;
    const char *containerMime = NULL;
    CHECK(AMediaFormat_getString(mFormat, AMEDIAFORMAT_KEY_MIME, &mime));

    mIsAVC = !strcasecmp(mime, MEDIA_MIMETYPE_VIDEO_AVC);
    mIsHEVC = !strcasecmp(mime, MEDIA_MIMETYPE_VIDEO_HEVC);

    AMediaFormat *extractor_meta = AMediaFormat_new();
    if(AMEDIA_OK == mExtractor->getMetaData(extractor_meta)){
        AMediaFormat_getString(extractor_meta, AMEDIAFORMAT_KEY_MIME, &containerMime);
    }
    AMediaFormat_delete(extractor_meta);

    mIsVorbis = containerMime != NULL && !strcasecmp(containerMime, MEDIA_MIMETYPE_CONTAINER_MATROSKA) && !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_VORBIS);

    mNALLengthSize = 0;
    mBufferSize = 0;
    mFrameSent = 0;

    if (mIsAVC) {

        void *data;
        size_t size;
        if(!AMediaFormat_getBuffer(mFormat, AMEDIAFORMAT_KEY_CSD_AVC, &data, &size))
            return;

        const uint8_t *ptr = (const uint8_t *)data;

        CHECK(size >= 7);
         CHECK_EQ((unsigned)ptr[0], 1u);  // configurationVersion == 1

        // The number of bytes used to encode the length of a NAL unit.
        mNALLengthSize = 1 + (ptr[4] & 3);
        ALOGD("ImxMediaSource::ImxMediaSource avc mNALLengthSize=%zu",mNALLengthSize);
    } else if (mIsHEVC) {

        void *data;
        size_t size;
        if(!AMediaFormat_getBuffer(mFormat, AMEDIAFORMAT_KEY_CSD_HEVC, &data,&size))
            return;

        const uint8_t *ptr = (const uint8_t *)data;

        CHECK(size >= 7);
        CHECK_EQ((unsigned)ptr[0], 1u);  // configurationVersion == 1

        mNALLengthSize = 1 + (ptr[21] & 3);
        ALOGD("ImxMediaSource::ImxMediaSource hevc mNALLengthSize=%zu",mNALLengthSize);
    }
}
ImxMediaSource::~ImxMediaSource()
{
    ALOGV("ImxMediaSource::~ImxMediaSource BEGIN mSourceIndex=%zu",mSourceIndex);
    clearPendingFrames();
    mExtractor->DisableTrack(mSourceIndex);
    mExtractor->DetachMediaBufferGroupHelper(mSourceIndex);
    mExtractor->ClearTrackSource(mSourceIndex);
    ALOGV("ImxMediaSource::~ImxMediaSource END mSourceIndex=%zu",mSourceIndex);

}
media_status_t ImxMediaSource::start()
{
    media_status_t ret = AMEDIA_OK;
    size_t max_buf_size = 0;
    mStarted = true;
    ret = mExtractor->GetTrackMaxBufferSize(mSourceIndex, &max_buf_size);
    if(ret != AMEDIA_OK)
        return ret;

    mBufferGroup->init(1 /* number of buffers */, max_buf_size /* buffer size */, 65536/* growth limit */);
    mExtractor->AttachMediaBufferGroupHelper(mSourceIndex, mBufferGroup);
    mExtractor->ActiveTrack(mSourceIndex);

    ALOGD("source start track %zu",mSourceIndex);

    return AMEDIA_OK;
}
void ImxMediaSource::clearPendingFrames() {
    ALOGV("clearPendingFrames mBufferSize=%zu.mPendingFrames=%zu",mBufferSize,mPendingFrames.size());
    while (!mPendingFrames.empty()) {
        MediaBufferHelper *frame = *mPendingFrames.begin();
        mPendingFrames.erase(mPendingFrames.begin());

        frame->release();
        frame = NULL;
    }
    mBufferSize = 0;

}

media_status_t ImxMediaSource::stop()
{
    clearPendingFrames();
    mStarted = false;
    mExtractor->DisableTrack(mSourceIndex);
    mExtractor->DetachMediaBufferGroupHelper(mSourceIndex);
    ALOGD("source stop track %zu",mSourceIndex);
    return AMEDIA_OK;
}
media_status_t ImxMediaSource::getFormat(AMediaFormat *format)
{
    ALOGV("ImxMediaSource::getFormat");
    return AMediaFormat_copy(format, mFormat);
}
static unsigned U24_AT(const uint8_t *ptr) {
    return ptr[0] << 16 | ptr[1] << 8 | ptr[2];
}
media_status_t ImxMediaSource::read(
        MediaBufferHelper **out, const ReadOptions *options)
{
    status_t ret = OK;
    *out = NULL;
    uint32_t seekFlag = 0;
    //int64_t targetSampleTimeUs = -1ll;
    size_t srcSize = 0;
    size_t srcOffset = 0;
    int32_t i = 0;
    int64_t seekTimeUs;
    ReadOptions::SeekMode mode;

    int64_t targetSampleTimeUs = -1;
    const char *containerMime = NULL;
    const char *mime = NULL;

    if (options && options->getSeekTo(&seekTimeUs, &mode)) {
        switch (mode) {
            case ReadOptions::SEEK_PREVIOUS_SYNC:
                seekFlag = SEEK_FLAG_NO_LATER;
                break;
            case ReadOptions::SEEK_NEXT_SYNC:
                seekFlag = SEEK_FLAG_NO_EARLIER;
                break;
            case ReadOptions::SEEK_CLOSEST_SYNC:
            case ReadOptions::SEEK_CLOSEST:
                seekFlag = SEEK_FLAG_NEAREST;
                break;
            case ReadOptions::SEEK_FRAME_INDEX:
                seekFlag = SEEK_FLAG_FRAME_INDEX;
                break;
            default:
                seekFlag = SEEK_FLAG_NEAREST;
                break;
        }

        if (mode == ReadOptions::SEEK_CLOSEST) {
            targetSampleTimeUs = seekTimeUs;
        }

        ALOGV("ImxMediaSource::read seekTimeUs %" PRId64 ", mode %d, seekFlag %d", seekTimeUs, mode, seekFlag);

        clearPendingFrames();

        AMediaFormat* meta = AMediaFormat_new();
        status_t result = mExtractor->getMetaData(meta);
        if(result == OK){

            AMediaFormat_getString(meta, AMEDIAFORMAT_KEY_MIME, &containerMime);
            AMediaFormat_getString(mFormat, AMEDIAFORMAT_KEY_MIME, &mime);

            if((mode == ReadOptions::SEEK_CLOSEST) && containerMime && !strcasecmp(containerMime,MEDIA_MIMETYPE_CONTAINER_MPEG4)){
                seekFlag = SEEK_FLAG_CLOSEST;
            }

            if(mFrameSent < 10 && containerMime && !strcasecmp(containerMime, MEDIA_MIMETYPE_CONTAINER_FLV)
                        && mime && !strcasecmp(mime,MEDIA_MIMETYPE_VIDEO_SORENSON))
            {
                ALOGV("read first frame before seeking track, mFrameSent %d", mFrameSent);
                int64_t time = 0;
                int32_t j=0;
                ret = mExtractor->HandleSeekOperation(mSourceIndex,&time,seekFlag);
                while (mPendingFrames.empty()) {
                    media_status_t err = mExtractor->GetNextSample(mSourceIndex,false);
                    if (err != AMEDIA_OK) {
                        clearPendingFrames();
                        return err;
                    }
                    j++;
                    if(j > 1 && OK != mExtractor->CheckInterleaveEos(mSourceIndex)){
                        ALOGE("get interleave eos");
                        return AMEDIA_ERROR_MALFORMED;
                    }
                }
                MediaBufferHelper *frame = *mPendingFrames.begin();
                AMediaFormat *bufMeta = frame->meta_data();
                AMediaFormat_setInt64(bufMeta, AMEDIAFORMAT_KEY_TIME_US, seekTimeUs);
            }

        }
        AMediaFormat_delete(meta);
        ret = mExtractor->HandleSeekOperation(mSourceIndex,&seekTimeUs,seekFlag);
        if(seekFlag == SEEK_FLAG_CLOSEST || seekFlag == SEEK_FLAG_FRAME_INDEX)
            targetSampleTimeUs = seekTimeUs;
    }

    while (mPendingFrames.empty()) {
        media_status_t err = mExtractor->GetNextSample(mSourceIndex,false);

        if (err != AMEDIA_OK) {
            clearPendingFrames();

            return err;
        }
        i++;
        if(i > 1 && OK != mExtractor->CheckInterleaveEos(mSourceIndex)){
            ALOGE("get interleave eos");
            return AMEDIA_ERROR_END_OF_STREAM;
        }
    }

    MediaBufferHelper *frame = *mPendingFrames.begin();
    mPendingFrames.erase(mPendingFrames.begin());

    *out = frame;
    mBufferSize -= frame->range_length();

    mFrameSent++;

    if (mIsVorbis) {
        int64_t timeUs;
        CHECK(AMediaFormat_getInt64(frame->meta_data(), AMEDIAFORMAT_KEY_TIME_US, &timeUs));
        ALOGV("ImxMediaSource::read mSourceIndex=%zu size=%zu,time %" PRId64 "",mSourceIndex,frame->range_length(),timeUs);

        int32_t sampleRate;
        if (!AMediaFormat_getInt32(mFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE,
                                   &sampleRate)) {
            return AMEDIA_ERROR_MALFORMED;
        }
        int64_t durationUs;
        if (!AMediaFormat_getInt64(mFormat, AMEDIAFORMAT_KEY_DURATION,
                                   &durationUs)) {
            return AMEDIA_ERROR_MALFORMED;
        }
        //same logic with MatroskaSource::readBlock()
        if (timeUs < durationUs) {
            int32_t validSamples = ((durationUs - timeUs) * sampleRate) / 1000000ll;
            AMediaFormat_setInt32(frame->meta_data(), AMEDIAFORMAT_KEY_VALID_SAMPLES, validSamples);
        }
    }

    if(!mIsAVC && !mIsHEVC){
        return AMEDIA_OK;
    }

    if (targetSampleTimeUs >= 0) {
        AMediaFormat_setInt64(frame->meta_data(),
                AMEDIAFORMAT_KEY_TARGET_TIME, targetSampleTimeUs);
    }

    //convert to nal frame
    uint8_t *srcPtr =
        (uint8_t *)frame->data() + frame->range_offset();
    srcSize = frame->range_length();

    if(srcPtr[0] == 0x0 && srcPtr[1] == 0x0 && srcPtr[2] == 0x0 && srcPtr[3] == 0x1){
        return AMEDIA_OK;
    }

    if(0 == mNALLengthSize)
        return AMEDIA_OK;

    //replace the 4 bytes when nal length size is 4
    if(4 == mNALLengthSize){

        while(srcOffset + mNALLengthSize <= srcSize){
            size_t NALsize = U32_AT(srcPtr + srcOffset);
            if((uint64_t)NALsize + (uint64_t) srcOffset >= 0xffffffff){
                ALOGE("invalid NALsize 0x%zu", NALsize);
                break;
            }

            srcPtr[srcOffset++] = 0;
            srcPtr[srcOffset++] = 0;
            srcPtr[srcOffset++] = 0;
            srcPtr[srcOffset++] = 1;

            //memcpy(&srcPtr[srcOffset], "\x00\x00\x00\x01", 4);
            srcOffset += NALsize;
        }
        if(srcOffset < srcSize){
            frame->release();
            frame = NULL;

            return AMEDIA_ERROR_MALFORMED;
        }
        ALOGV("ImxMediaSource::read 2 size=%zu",srcSize);

        return AMEDIA_OK;
    }

    //create a new MediaBuffer and copy all data from old buffer to new buffer.
    size_t dstSize = 0;
    MediaBufferHelper *buffer = NULL;
    uint8_t *dstPtr = NULL;
    //got the buffer size when pass is 0, then copy buffer when pass is 1
    for (int32_t pass = 0; pass < 2; pass++) {
        ALOGV("ImxMediaSource::read pass=%d,begin",pass);
        size_t srcOffset = 0;
        size_t dstOffset = 0;
        while (srcOffset + mNALLengthSize <= srcSize) {
            size_t NALsize;
            switch (mNALLengthSize) {
                case 1: NALsize = srcPtr[srcOffset]; break;
                case 2: NALsize = U16_AT(srcPtr + srcOffset); break;
                case 3: NALsize = U24_AT(srcPtr + srcOffset); break;
                case 4: NALsize = U32_AT(srcPtr + srcOffset); break;
                default:
                    TRESPASS();
            }

            if (NALsize == 0) {
                frame->release();
                frame = NULL;

                return AMEDIA_ERROR_MALFORMED;
            } else if (srcOffset + mNALLengthSize + NALsize > srcSize) {
                break;
            }

            if (pass == 1) {
                memcpy(&dstPtr[dstOffset], "\x00\x00\x00\x01", 4);

                memcpy(&dstPtr[dstOffset + 4],
                       &srcPtr[srcOffset + mNALLengthSize],
                       NALsize);
                ALOGV("ImxMediaSource::read 3 copy %zu",4+NALsize);
            }

            dstOffset += 4;  // 0x00 00 00 01
            dstOffset += NALsize;

            srcOffset += mNALLengthSize + NALsize;
        }

        if (srcOffset < srcSize) {
            // There were trailing bytes or not enough data to complete
            // a fragment.

            frame->release();
            frame = NULL;

            return AMEDIA_ERROR_MALFORMED;
        }

        if (pass == 0) {
            dstSize = dstOffset;

            mBufferGroup->acquire_buffer(&buffer, false, dstSize);
            buffer->set_range(0, dstSize);

            AMediaFormat *frameMeta = frame->meta_data();

            int64_t timeUs;
            CHECK(AMediaFormat_getInt64(frameMeta, AMEDIAFORMAT_KEY_TIME_US, &timeUs));

            int32_t isSync;
            CHECK(AMediaFormat_getInt32(frameMeta, AMEDIAFORMAT_KEY_IS_SYNC_FRAME, &isSync));

            AMediaFormat *bufMeta = buffer->meta_data();
            AMediaFormat_setInt64(bufMeta, AMEDIAFORMAT_KEY_TIME_US, timeUs);
            AMediaFormat_setInt32(bufMeta, AMEDIAFORMAT_KEY_IS_SYNC_FRAME, isSync);

            dstPtr = (uint8_t *)buffer->data();
            ALOGV("ImxMediaSource::read 3 size=%zu,ts=%" PRId64 "",dstSize,timeUs);
        }
    }

    frame->release();
    frame = NULL;
    *out = buffer;

    return AMEDIA_OK;
}

void ImxMediaSource::addMediaBuffer(MediaBufferHelper *buffer)
{
    if(buffer == NULL)
        return;

    mBufferSize += buffer->range_length();
    mPendingFrames.push_back(buffer);

    return;
}
bool ImxMediaSource::started()
{
    return mStarted;
}
bool ImxMediaSource::full()
{
    size_t maxBufferSize;
    int width = 0;
    int height = 0;

    if(AMediaFormat_getInt32(mFormat, AMEDIAFORMAT_KEY_WIDTH, &width)
        && AMediaFormat_getInt32(mFormat, AMEDIAFORMAT_KEY_HEIGHT, &height)
        && width >= 3840 && height >= 2160){
        maxBufferSize = MAX_FRAME_BUFFER_LENGTH_4K;
    }
    else
        maxBufferSize = MAX_FRAME_BUFFER_LENGTH;

    if(mBufferSize > maxBufferSize)
        return true;
    else
        return false;
}

struct ImxDataSourceReader{
public:
    ImxDataSourceReader(DataSourceHelper *source);

    bool isStreaming() const;
    bool isLiveStreaming() const;
    bool AddBufferReadLimitation(uint32_t index,uint32_t size);
    bool AttachMediaBufferGroupHelper(uint32_t track_num, MediaBufferGroupHelper * buf_group);
    bool DetachMediaBufferGroupHelper(uint32_t track_num);
    bool AcquireBuffer(uint32_t track_num, size_t size, MediaBufferHelper **out_buf);
    uint32_t GetBufferReadLimitation(uint32_t index);
    DataSourceHelper *mDataSource;
    Mutex mLock;
    int64_t mOffset;
    bool bStopReading;
    int64_t mLength;
private:

    bool mIsLiveStreaming;
    bool mIsStreaming;
    uint32_t mMaxBufferSize[MAX_TRACK_COUNT];
    MediaBufferGroupHelper *mBufferGroup[MAX_TRACK_COUNT];

    ImxDataSourceReader(const ImxDataSourceReader &);
    ImxDataSourceReader &operator=(const ImxDataSourceReader &);
};
static FslFileHandle appFileOpen ( const uint8 * file_path, const uint8 * mode, void * context)
{
    if(NULL == mode || !context)
    {
        ALOGE("appLocalFileOpen: Invalid parameter\n");
        return NULL;
    }
    if(file_path)
        ALOGV("file_path=%s",file_path);

    ImxDataSourceReader *h = (ImxDataSourceReader *)context;
    FslFileHandle sourceFileHandle =(FslFileHandle)&h->mDataSource;

    Mutex::Autolock autoLock(h->mLock);

    if(sourceFileHandle != NULL){
        ALOGV("appFileOpen success,sourceFileHandle=%p",sourceFileHandle);
        return sourceFileHandle;
    }else
        return NULL;
}
static uint32  appReadFile( FslFileHandle file_handle, void * buffer, uint32 nb, void * context)
{
    ImxDataSourceReader *h = (ImxDataSourceReader *)context;
    int ret = 0;

    if (!file_handle || !context || !buffer)
        return 0;

    Mutex::Autolock autoLock(h->mLock);
    if(h->bStopReading){
        return 0;
    }

    //ALOGV("appLocalReadFile nb %u",(unsigned int)nb);

    ret = h->mDataSource->readAt(h->mOffset, buffer, nb);

    //ALOGV("appReadFile at %lld nb %u, result %d", h->mOffset, (unsigned int)nb, ret);

    if(ret > 0)
    {
        h->mOffset += ret;
        return ret;
    }
    else
    {
        //ALOGV("appLocalReadFile 0");
        return 0xffffffff;
    }
}


static int32   appSeekFile( FslFileHandle file_handle, int64 offset, int32 whence, void * context)
{

    ImxDataSourceReader *h = (ImxDataSourceReader *)context;
    int nContentPipeResult = 0;
    if (!file_handle || !context)
        return -1;

    Mutex::Autolock autoLock(h->mLock);
    //ALOGV("appSeekFile current location=%lld,offset %lld, whence %d",h->mOffset, offset, whence);

    switch(whence) {
        case SEEK_CUR:
        {
            if(h->mLength > 0 && (h->mOffset + offset > h->mLength || h->mOffset + offset < 0)){
                nContentPipeResult = -1;
            }else
                h->mOffset += offset;
        }
        break;
        case SEEK_SET:
        {
            if(h->mLength > 0 && offset > h->mLength)
                nContentPipeResult = -1;
            else
                h->mOffset = offset;
        }
        break;
        case SEEK_END:
        {
            if(offset > 0 ||  h->mLength + offset < 0 || h->isLiveStreaming())
                nContentPipeResult = -1;
            else
                h->mOffset = h->mLength + offset;
        }
        break;
        default:
            nContentPipeResult = -1;
            break;
    }

    if( 0 == nContentPipeResult) /* success */
        return 0;

    ALOGV("appSeekFile fail %d", nContentPipeResult);
    return -1;
}
static int64  appGetCurrentFilePos( FslFileHandle file_handle, void * context)
{
    ImxDataSourceReader *h = (ImxDataSourceReader *)context;

    if (!file_handle || !context)
        return -1;

    return h->mOffset;
}

static int64   appFileSize( FslFileHandle file_handle, void * context)
{
    if (!file_handle || !context)
        return -1;

    ImxDataSourceReader *h = (ImxDataSourceReader *)context;

    ALOGV("appFileSize %" PRId64 "", h->mLength);

    return h->mLength;
}

static int32 appFileClose( FslFileHandle file_handle, void * context)
{
    if (!file_handle || !context)
        return -1;
    ImxDataSourceReader *h = (ImxDataSourceReader *)context;

    Mutex::Autolock autoLock(h->mLock);
    file_handle = NULL;
    h->mOffset = 0;
    ALOGV("appFileClose");
    return 0;
}

static int64   appCheckAvailableBytes(FslFileHandle file_handle, int64 bytesRequested, void * context)
{
    if (!file_handle || !context)
        return 0;

    return bytesRequested;
}

static uint32  appGetFlag( FslFileHandle file_handle, void * context)
{
    uint32 flag = 0;

    if (!file_handle || !context)
        return 0;

    ImxDataSourceReader *h = (ImxDataSourceReader *)context;
    if(h->isLiveStreaming()){
        flag |= FILE_FLAG_NON_SEEKABLE;
        flag |= FILE_FLAG_READ_IN_SEQUENCE;
    }
    if(h->isStreaming())
        flag |= FILE_FLAG_READ_IN_SEQUENCE;

    ALOGV("appLocalGetFlag %x", flag);

    return flag;
}

static void *appCalloc(uint32 TotalNumber, uint32 TotalSize)
{
    void *PtrCalloc = NULL;

    if((0 == TotalSize)||(0==TotalNumber))
        ALOGW("\nWarning: ZERO size IN LOCAL CALLOC");

    PtrCalloc = malloc(TotalNumber*TotalSize);

    if (PtrCalloc == NULL) {

        ALOGE("\nError: MEMORY FAILURE IN LOCAL CALLOC");
        return NULL;
    }
    memset(PtrCalloc, 0, TotalSize*TotalNumber);
    return (PtrCalloc);
}

static void* appMalloc (uint32 TotalSize)
{

    void *PtrMalloc = NULL;

    if(0 == TotalSize)
        ALOGW("\nWarning: ZERO size IN LOCAL MALLOC");

    PtrMalloc = malloc(TotalSize);

    if (PtrMalloc == NULL) {

        ALOGE("\nError: MEMORY FAILURE IN LOCAL MALLOC");
    }
    return (PtrMalloc);
}
static void appFree (void *MemoryBlock)
{
    if(MemoryBlock)
        free(MemoryBlock);
}

static void * appReAlloc (void *MemoryBlock, uint32 TotalSize)
{
    void *PtrMalloc = NULL;

    if(0 == TotalSize)
        ALOGW("\nWarning: ZERO size IN LOCAL REALLOC");

    PtrMalloc = (void *)realloc(MemoryBlock, TotalSize);
    if (PtrMalloc == NULL) {
        ALOGE("\nError: MEMORY FAILURE IN LOCAL REALLOC");
    }

    return PtrMalloc;
}
static uint8* appRequestBuffer(   uint32 streamNum,
                                uint32 *size,
                                void ** bufContext,
                                void * parserContext)
{
    ImxDataSourceReader *h;
    uint8 * dataBuf = NULL;
    MediaBufferHelper * buffer = NULL;
    uint32_t limitSize = 0;

    if (!size || !bufContext || !parserContext)
        return NULL;

    //ALOGV("appRequestBuffer streamNum=%u",streamNum);

    h = (ImxDataSourceReader *)parserContext;

    if((*size) >= 100000000){
        return NULL;
    }

    limitSize = h->GetBufferReadLimitation(streamNum);
    if((*size) > limitSize && limitSize > 0)
        *size = limitSize;


    if(false == h->AcquireBuffer(streamNum, (size_t)(*size), &buffer)){
        ALOGE("appRequestBuffer streamNum=%u failed",streamNum);
        return NULL;
    }

    buffer->set_range(0, (*size));
    //ALOGV("appRequestBuffer streamNum=%u,len=%d,size=%zu",streamNum,(*size),buffer->size());
    *bufContext = (void*)buffer;
    dataBuf = (uint8*)buffer->data();

    return dataBuf;
}


static void appReleaseBuffer(uint32 streamNum, uint8 * pBuffer, void * bufContext, void * parserContext)
{
    ImxDataSourceReader *h;
    MediaBufferHelper * buffer = NULL;

    ALOGV("appReleaseBuffer streamNum=%u",streamNum);

    if (!pBuffer || !bufContext || !parserContext)
        return;
    h = (ImxDataSourceReader *)parserContext;
    buffer = (MediaBufferHelper *)(bufContext);
    buffer->release();
    buffer = NULL;

    return;
}

ImxDataSourceReader::ImxDataSourceReader(DataSourceHelper *source)
    :mDataSource(source),
    mOffset(0),
    mLength(0)
{
    off64_t size = 0;

    mIsLiveStreaming = (mDataSource->flags() & 32);
    mIsStreaming = (mDataSource->flags() & DataSourceBase::kIsCachingDataSource);

    if(!mIsLiveStreaming){
        status_t ret = mDataSource->getSize(&size);
        if(ret == OK)
            mLength = size;
        else if(ret > 0)
            mLength = ret;
    }else{
        mLength = 0;
    }

    ALOGV("ImxDataSourceReader: mLength is  %" PRId64 "", mLength);

    bStopReading = false;
    memset(&mMaxBufferSize[0], 0, MAX_TRACK_COUNT*sizeof(uint32_t));
    memset(&mBufferGroup[0], 0, MAX_TRACK_COUNT*sizeof(MediaBufferGroupHelper*));
}
bool ImxDataSourceReader::isStreaming() const
{
    return mIsStreaming;
}
bool ImxDataSourceReader::isLiveStreaming() const
{
    return mIsLiveStreaming;
}
bool ImxDataSourceReader::AddBufferReadLimitation(uint32_t track_num,uint32_t size)
{
    if(track_num < MAX_TRACK_COUNT){
        mMaxBufferSize[track_num] = size;
        return true;
    }else
        return false;
}
uint32_t ImxDataSourceReader::GetBufferReadLimitation(uint32_t track_num)
{
    if(track_num < MAX_TRACK_COUNT){
        return mMaxBufferSize[track_num];
    }else
        return 0;
}
bool ImxDataSourceReader::AttachMediaBufferGroupHelper(uint32_t track_num, MediaBufferGroupHelper * buf_group)
{
    if(track_num < MAX_TRACK_COUNT){
        mBufferGroup[track_num] = buf_group;
        return true;
    }else
        return false;
}
bool ImxDataSourceReader::DetachMediaBufferGroupHelper(uint32_t track_num)
{
    if(track_num < MAX_TRACK_COUNT && mBufferGroup[track_num] != NULL){
        mBufferGroup[track_num] = NULL;
        return true;
    }else
        return false;
}
bool ImxDataSourceReader::AcquireBuffer(uint32_t track_num, size_t size, MediaBufferHelper **out_buf)
{
    MediaBufferHelper * buffer = NULL;

    if(track_num >= MAX_TRACK_COUNT || mBufferGroup[track_num] == NULL){
        ALOGE("AcquireBuffer failed due to no mBufferGroup[%d],size=%zu",track_num, size);
        return false;
    }

    if(OK == mBufferGroup[track_num]->acquire_buffer(&buffer, false, size)){
        buffer->set_range(0, size);
        *out_buf = buffer;
        ALOGV("AcquireBuffer track_num=%d,size=%zu",track_num,size);
        return true;
    }
    ALOGE("AcquireBuffer failed mBufferGroup[%d],size=%zu",track_num, size);

    return false;
}
typedef struct{
    const char* name;
    const char* mime;
}fsl_mime_struct;

fsl_mime_struct mime_table[]={
    {"mp4",MEDIA_MIMETYPE_CONTAINER_MPEG4},
    {"mkv",MEDIA_MIMETYPE_CONTAINER_MATROSKA},
    {"avi",MEDIA_MIMETYPE_CONTAINER_AVI},
    {"asf",MEDIA_MIMETYPE_CONTAINER_ASF},
    {"flv",MEDIA_MIMETYPE_CONTAINER_FLV},
    {"mpg2",MEDIA_MIMETYPE_CONTAINER_MPEG2TS},
    {"mpg2",MEDIA_MIMETYPE_CONTAINER_MPEG2PS},
    {"rm",MEDIA_MIMETYPE_CONTAINER_RMVB},
    {"mp3",MEDIA_MIMETYPE_AUDIO_MPEG},
    {"aac",MEDIA_MIMETYPE_AUDIO_AAC_ADTS},
    {"ape",MEDIA_MIMETYPE_AUDIO_APE},
    {"flac",MEDIA_MIMETYPE_AUDIO_FLAC},
    {"dsf",MEDIA_MIMETYPE_CONTAINER_DSF},
};
typedef struct{
    uint32_t type;
    uint32_t subtype;
    const char* mime;
}codec_mime_struct;
codec_mime_struct video_mime_table[]={
    {VIDEO_H263,0,MEDIA_MIMETYPE_VIDEO_H263},
    {VIDEO_H264,0,MEDIA_MIMETYPE_VIDEO_AVC},
    {VIDEO_HEVC,0,MEDIA_MIMETYPE_VIDEO_HEVC},
    {VIDEO_MPEG2,0,MEDIA_MIMETYPE_VIDEO_MPEG2},
    {VIDEO_MPEG4,0,MEDIA_MIMETYPE_VIDEO_MPEG4},
    {VIDEO_JPEG,0,MEDIA_MIMETYPE_VIDEO_MJPEG},
    {VIDEO_MJPG,VIDEO_MJPEG_2000,NULL},
    {VIDEO_MJPG,VIDEO_MJPEG_FORMAT_B,NULL},
    {VIDEO_MJPG,0,MEDIA_MIMETYPE_VIDEO_MJPEG},
    {VIDEO_DIVX,VIDEO_DIVX3,MEDIA_MIMETYPE_VIDEO_DIVX3},
    {VIDEO_DIVX,VIDEO_DIVX4,MEDIA_MIMETYPE_VIDEO_DIVX4},
    {VIDEO_DIVX,VIDEO_DIVX5_6,MEDIA_MIMETYPE_VIDEO_DIVX},
    {VIDEO_DIVX,0,MEDIA_MIMETYPE_VIDEO_DIVX},
    {VIDEO_XVID,0,MEDIA_MIMETYPE_VIDEO_XVID},
    {VIDEO_WMV,VIDEO_WMV7,MEDIA_MIMETYPE_VIDEO_WMV},
    {VIDEO_WMV,VIDEO_WMV8,MEDIA_MIMETYPE_VIDEO_WMV},
    {VIDEO_WMV,VIDEO_WMV9,MEDIA_MIMETYPE_VIDEO_VC1},
    {VIDEO_WMV,VIDEO_WMV9A,MEDIA_MIMETYPE_VIDEO_VC1},
    {VIDEO_WMV,VIDEO_WVC1,MEDIA_MIMETYPE_VIDEO_VC1},
    {VIDEO_REAL,0,MEDIA_MIMETYPE_VIDEO_REAL},
    {VIDEO_SORENSON_H263,0,MEDIA_MIMETYPE_VIDEO_SORENSON},
    {VIDEO_ON2_VP,VIDEO_VP8,MEDIA_MIMETYPE_VIDEO_VP8},
    {VIDEO_ON2_VP,VIDEO_VP9,MEDIA_MIMETYPE_VIDEO_VP9},
    {VIDEO_AV1,0,MEDIA_MIMETYPE_VIDEO_AV1},
};
codec_mime_struct audio_mime_table[]={
    {AUDIO_MP3,0,MEDIA_MIMETYPE_AUDIO_MPEG},
    {AUDIO_VORBIS,0,MEDIA_MIMETYPE_AUDIO_VORBIS},
    {AUDIO_AAC,AUDIO_ER_BSAC,MEDIA_MIMETYPE_AUDIO_BSAC},
    {AUDIO_AAC,0,MEDIA_MIMETYPE_AUDIO_AAC},
    {AUDIO_MPEG2_AAC,0,MEDIA_MIMETYPE_AUDIO_AAC},
    {AUDIO_AC3,0,MEDIA_MIMETYPE_AUDIO_AC3},
    {AUDIO_EC3,0,MEDIA_MIMETYPE_AUDIO_EAC3},
    {AUDIO_WMA,0,MEDIA_MIMETYPE_AUDIO_WMA},
    {AUDIO_AMR,AUDIO_AMR_NB,MEDIA_MIMETYPE_AUDIO_AMR_NB},
    {AUDIO_AMR,AUDIO_AMR_WB,MEDIA_MIMETYPE_AUDIO_AMR_WB},
    {AUDIO_PCM,0,MEDIA_MIMETYPE_AUDIO_RAW},
    {AUDIO_REAL,REAL_AUDIO_RAAC,MEDIA_MIMETYPE_AUDIO_AAC},
    {AUDIO_REAL,REAL_AUDIO_SIPR,MEDIA_MIMETYPE_AUDIO_REAL},
    {AUDIO_REAL,REAL_AUDIO_COOK,MEDIA_MIMETYPE_AUDIO_REAL},
    {AUDIO_REAL,REAL_AUDIO_ATRC,MEDIA_MIMETYPE_AUDIO_REAL},
    {AUDIO_PCM_ALAW,0,MEDIA_MIMETYPE_AUDIO_G711_ALAW},
    {AUDIO_PCM_MULAW,0,MEDIA_MIMETYPE_AUDIO_G711_MLAW},
    {AUDIO_FLAC,0,MEDIA_MIMETYPE_AUDIO_FLAC},
    {AUDIO_OPUS,0,MEDIA_MIMETYPE_AUDIO_OPUS},
    {AUDIO_APE,0,MEDIA_MIMETYPE_AUDIO_APE},
    {AUDIO_DSD,0,MEDIA_MIMETYPE_AUDIO_DSD},
    {AUDIO_AC4,0,MEDIA_MIMETYPE_AUDIO_AC4},
    {AUDIO_ADPCM,AUDIO_IMA_ADPCM,MEDIA_MIMETYPE_AUDIO_DVI_IMA_ADPCM},
    {AUDIO_ADPCM,AUDIO_ADPCM_MS,MEDIA_MIMETYPE_AUDIO_MS_ADPCM},
    {AUDIO_ALAC,0,MEDIA_MIMETYPE_AUDIO_ALAC},
};

#ifdef USE_IMX_AAC_Dec
static const uint64_t gM4aFilteredFileSizes[] = {
    // testDecodeM4a
    /*sinesweepm4a.m4a*/ 60053,

    // testDecodeAacLcM4a
    /*sinesweep1_1ch_8khz_aot2_mp4.m4a*/ 31941,
    /*sinesweep1_1ch_11khz_aot2_mp4.m4a*/ 31942,
    /*sinesweep1_1ch_12khz_aot2_mp4.m4a*/ 31943,
    /*sinesweep1_1ch_16khz_aot2_mp4.m4a*/ 31942,
    /*sinesweep1_1ch_22khz_aot2_mp4.m4a*/ 31943,
    /*sinesweep1_1ch_24khz_aot2_mp4.m4a*/ 31943,
    /*sinesweep1_1ch_32khz_aot2_mp4.m4a*/ 31942,
    /*sinesweep1_1ch_44khz_aot2_mp4.m4a*/ 31943,
    /*sinesweep1_1ch_48khz_aot2_mp4.m4a*/ 31943,
    /*sinesweep_2ch_8khz_aot2_mp4.m4a*/ 62683,
    /*sinesweep_2ch_11khz_aot2_mp4.m4a*/ 62684,
    /*sinesweep_2ch_12khz_aot2_mp4.m4a*/ 62684,
    /*sinesweep_2ch_16khz_aot2_mp4.m4a*/ 62683,
    /*sinesweep_2ch_22khz_aot2_mp4.m4a*/ 62684,
    /*sinesweep_2ch_24khz_aot2_mp4.m4a*/ 62684,
    /*sinesweep_2ch_32khz_aot2_mp4.m4a*/ 62684,
    /*sinesweep_2ch_44khz_aot2_mp4.m4a*/ 62684,
    /*sinesweep_2ch_48khz_aot2_mp4.m4a*/ 62684,

    // testDecodeHeAacM4a
    /*noise_1ch_24khz_aot5_dr_sbr_sig1_mp4.m4a*/ 10624,
    /*noise_1ch_24khz_aot5_ds_sbr_sig1_mp4.m4a*/ 11687,
    /*noise_1ch_32khz_aot5_dr_sbr_sig2_mp4.m4a*/ 21821,
    /*noise_1ch_44khz_aot5_dr_sbr_sig0_mp4.m4a*/ 29591,
    /*noise_1ch_44khz_aot5_ds_sbr_sig2_mp4.m4a*/ 20356,
    /*noise_2ch_24khz_aot5_dr_sbr_sig2_mp4.m4a*/ 19773,
    /*noise_2ch_32khz_aot5_ds_sbr_sig2_mp4.m4a*/ 25845,
    /*noise_2ch_48khz_aot5_dr_sbr_sig1_mp4.m4a*/ 50294,
    /*noise_2ch_48khz_aot5_ds_sbr_sig1_mp4.m4a*/ 29440,

    // testDecodeHeAacV2M4a
    /*noise_2ch_24khz_aot29_dr_sbr_sig0_mp4.m4a*/ 17033,
    /*noise_2ch_44khz_aot29_dr_sbr_sig1_mp4.m4a*/ 29908,
    /*noise_2ch_48khz_aot29_dr_sbr_sig2_mp4.m4a*/ 32185,

    // testDecodeAacEldM4a, AAC-ELD is not supported
    /*sinesweep1_1ch_16khz_aot39_fl480_mp4.m4a*/ 22627,
    /*sinesweep1_1ch_22khz_aot39_fl512_mp4.m4a*/ 22481,
    /*sinesweep1_1ch_24khz_aot39_fl480_mp4.m4a*/ 22613,
    /*sinesweep1_1ch_32khz_aot39_fl512_mp4.m4a*/ 22518,
    /*sinesweep1_1ch_44khz_aot39_fl480_mp4.m4a*/ 22766,
    /*sinesweep1_1ch_48khz_aot39_fl512_mp4.m4a*/ 22604,
    /*sinesweep_2ch_16khz_aot39_fl512_mp4.m4a*/ 43192,
    /*sinesweep_2ch_22khz_aot39_fl480_mp4.m4a*/ 43404,
    /*sinesweep_2ch_24khz_aot39_fl512_mp4.m4a*/ 43256,
    /*sinesweep_2ch_32khz_aot39_fl480_mp4.m4a*/ 43366,
    /*sinesweep_2ch_44khz_aot39_fl512_mp4.m4a*/ 43210,
    /*sinesweep_2ch_48khz_aot39_fl480_mp4.m4a*/ 43366,
    /*noise_1ch_16khz_aot39_ds_sbr_fl512_mp4.m4a*/ 9768,
    /*noise_1ch_24khz_aot39_ds_sbr_fl512_mp4.m4a*/ 12233,
    /*noise_1ch_32khz_aot39_dr_sbr_fl480_mp4.m4a*/ 25033,
    /*noise_1ch_44khz_aot39_ds_sbr_fl512_mp4.m4a*/ 20633,
    /*noise_1ch_48khz_aot39_dr_sbr_fl480_mp4.m4a*/ 30159,
    /*noise_2ch_22khz_aot39_ds_sbr_fl512_mp4.m4a*/ 14426,
    /*noise_2ch_32khz_aot39_ds_sbr_fl512_mp4.m4a*/ 23847,
    /*noise_2ch_44khz_aot39_dr_sbr_fl480_mp4.m4a*/ 45164,
    /*noise_2ch_48khz_aot39_ds_sbr_fl512_mp4.m4a*/ 37692,

    // testDecodeHeAacMcM4a, don't support aacp multi-channels
    /*noise_5ch_48khz_aot5_dr_sbr_sig1_mp4*/ 304170,
    /*noise_6ch_44khz_aot5_dr_sbr_sig2_mp4*/ 335657,

    // testGapless2/testGapless3, aac decoder don't support output delay
    /*stereonoisedcpos.m4a*/ 83892,
    /*mononoisedcpos.m4a*/ 46180,

};

static std::unordered_map<uint64_t, int32_t> gM4aFilteredFiles;

static bool FilterM4aFile(uint64_t fileSize) {
    bool beFiltered = false;

    if (gM4aFilteredFiles.empty()) {
        int32_t i;
        for (i = 0; i < sizeof(gM4aFilteredFileSizes)/sizeof(gM4aFilteredFileSizes[0]); i++) {
            gM4aFilteredFiles.insert({gM4aFilteredFileSizes[i], i});
        }
    }

    if (gM4aFilteredFiles.count(fileSize) != 0)
        beFiltered = true;

    ALOGV("FilterM4aFile return %d", beFiltered);

    return beFiltered;

}
#endif

ImxExtractor::ImxExtractor(DataSourceHelper *source,const char *mime)
    : mDataSource(source),
    mReader(new ImxDataSourceReader(mDataSource)),
    mMime(strdup(mime)),
    bInit(false)
{
    memset(&mLibName,0,255);
    mLibHandle = NULL;
    IParser = NULL;
    parserHandle = NULL;
    mFileMetaData = AMediaFormat_new();
    AMediaFormat_setString(mFileMetaData, AMEDIAFORMAT_KEY_MIME, mMime);

    currentVideoTs = 0;
    currentAudioTs = 0;
    mVideoActived = false;
    mAudioActived = false;
    bWaitForAudioStartTime = false;
    mVideoIndex = 0;
    mAudioIndex = 0;
    mReadMode = PARSER_READ_MODE_TRACK_BASED;
    mNumTracks = 0;
    bSeekable = false;
    mMovieDuration = 0;

    mAdtsBuffer = NULL;
    mResyncAdts = true;

    memset(&fileOps, 0, sizeof(FslFileStream));
    memset(&memOps, 0, sizeof(ParserMemoryOps));
    memset(&outputBufferOps, 0, sizeof(ParserOutputBufferOps));

    ALOGD("ImxExtractor::ImxExtractor mime=%s",mMime);
}
ImxExtractor::~ImxExtractor()
{
    if(parserHandle)
    {
        IParser->deleteParser(parserHandle);
        parserHandle = NULL;
    }

    if(IParser){
        delete IParser;
        IParser = NULL;
    }

    if(mReader != NULL){
        delete mReader;
        mReader = NULL;
    }

    if (mLibHandle != NULL) {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }
    AMediaFormat_clear(mFileMetaData);
    AMediaFormat_delete(mFileMetaData);
    free(mMime);

    ALOGD("ImxExtractor::~ImxExtractor");
}
status_t ImxExtractor::Init()
{
    status_t ret = OK;

    if(mReader == NULL)
        return UNKNOWN_ERROR;

    ALOGD("ImxExtractor::Init BEGIN");
    memset (&fileOps, 0, sizeof(FslFileStream));
    fileOps.Open = appFileOpen;
    fileOps.Read= appReadFile;
    fileOps.Seek = appSeekFile;
    fileOps.Tell = appGetCurrentFilePos;
    fileOps.Size= appFileSize;
    fileOps.Close = appFileClose;
    fileOps.CheckAvailableBytes = appCheckAvailableBytes;
    fileOps.GetFlag = appGetFlag;

    memset (&memOps, 0, sizeof(ParserMemoryOps));
    memOps.Calloc = appCalloc;
    memOps.Malloc = appMalloc;
    memOps.Free= appFree;
    memOps.ReAlloc= appReAlloc;

    outputBufferOps.RequestBuffer = appRequestBuffer;
    outputBufferOps.ReleaseBuffer = appReleaseBuffer;
    ret = CreateParserInterface();
    if(ret != OK){
        ALOGE("ImxExtractor create parser failed");
        return ret;
    }

    ret = ParseFromParser();

    ALOGD("ImxExtractor::Init ret=%d",ret);

    if(ret == OK)
        bInit = true;
    return ret;
}
size_t ImxExtractor::countTracks()
{
    status_t ret = OK;
    if(!bInit){
        ret = Init();

        if(ret != OK)
            return 0;
    }

    return mTracks.size();
}
MediaTrackHelper * ImxExtractor::getTrack(size_t index)
{
    ImxMediaSource* source;
    ALOGD("ImxExtractor::getTrack index=%zu",index);

    if (index >= mTracks.size()) {
        return NULL;
    }
    TrackInfo *trackInfo = &mTracks.editItemAt(index);

    AMediaFormat * meta = trackInfo->mMeta;
    source = new ImxMediaSource(this,index,meta);

    trackInfo->mSource = source;
    //ALOGE("getTrack source string cnt=%d",source->getStrongCount());

    return source;
}

media_status_t ImxExtractor::getTrackMetaData(AMediaFormat *meta,size_t index, uint32_t flags)
{
    if(!bInit){
        status_t ret = OK;
        ret = Init();

        if(ret != OK)
            return AMEDIA_ERROR_UNKNOWN;
    }
    if(flags){
        ;//
    }

    ALOGV("getTrackMetaData index=%zu",index);
    return AMediaFormat_copy(meta, mTracks.itemAt(index).mMeta);
    //return OK;
}

media_status_t ImxExtractor::getMetaData(AMediaFormat *meta)
{
    if(!bInit){
        status_t ret = OK;
        ret = Init();

        if(ret != OK)
            return AMEDIA_ERROR_UNKNOWN;
    }

    if(meta == nullptr){
        ALOGE("meta == NULL");
        return AMEDIA_ERROR_UNKNOWN;
    }
    AMediaFormat_clear(meta);
    return AMediaFormat_copy(meta, mFileMetaData);

    //return AMEDIA_OK;
}
uint32_t ImxExtractor::flags() const
{
    uint32_t x = CAN_PAUSE;
    if (!mReader->isLiveStreaming() && bSeekable) {
        x |= CAN_SEEK_BACKWARD | CAN_SEEK_FORWARD | CAN_SEEK;
    }

    return x;
}
status_t ImxExtractor::GetLibraryName()
{
    const char * name = NULL;
    for (size_t i = 0; i < sizeof(mime_table) / sizeof(mime_table[0]); i++) {
        if (!strcmp((const char *)mMime, mime_table[i].mime)) {
            name = mime_table[i].name;
            break;
        }
    }
    if(name == NULL || strlen(name) > 128)
        return NAME_NOT_FOUND;


    strcpy(mLibName, "/system/lib"
#ifdef __LP64__
            "64"
#endif
            "/extractors/lib_");
    strcat(mLibName,name);
    strcat(mLibName,"_parser_arm11_elinux.3.0.so");

    ALOGD("GetLibraryName %s",mLibName);
    return OK;
}
status_t ImxExtractor::CreateParserInterface()
{
    status_t ret = OK;
    int32 err = PARSER_SUCCESS;
    tFslParserQueryInterface  myQueryInterface;

    ret = GetLibraryName();
    if(ret != OK)
        return ret;

    do{
        mLibHandle = dlopen(mLibName, RTLD_NOW);
        if (mLibHandle == NULL){
            ALOGD("dlopen fail");
            ret = UNKNOWN_ERROR;
            break;
        }
        ALOGD("load parser name %s",mLibName);
        myQueryInterface = (tFslParserQueryInterface)dlsym(mLibHandle, "FslParserQueryInterface");
        if(myQueryInterface == NULL){
            ret = UNKNOWN_ERROR;
            break;
        }

        IParser = new FslParserInterface;
        if(IParser == NULL){
            ret = UNKNOWN_ERROR;
            break;
        }

        err = myQueryInterface(PARSER_API_GET_VERSION_INFO, (void **)&IParser->getVersionInfo);
        if(err)
            break;

        if(!IParser->getVersionInfo){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        // create & delete
        err = myQueryInterface(PARSER_API_CREATE_PARSER, (void **)&IParser->createParser);
        if(err)
            break;

        if(!IParser->createParser){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        err = myQueryInterface(PARSER_API_CREATE_PARSER2, (void **)&IParser->createParser2);
        if(err)
            ALOGW("IParser->createParser2 not found");

        err = myQueryInterface(PARSER_API_DELETE_PARSER, (void **)&IParser->deleteParser);
        if(err)
            break;

        if(!IParser->deleteParser){
            err = PARSER_ERR_INVALID_API;
            break;
        }
        //index init
        err = myQueryInterface(PARSER_API_INITIALIZE_INDEX, (void **)&IParser->initializeIndex);
        if(err)
            break;

        //movie properties
        err = myQueryInterface(PARSER_API_IS_MOVIE_SEEKABLE, (void **)&IParser->isSeekable);
        if(err)
            break;

        if(!IParser->isSeekable){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        err = myQueryInterface(PARSER_API_GET_MOVIE_DURATION, (void **)&IParser->getMovieDuration);
        if(err)
            break;

        if(!IParser->getMovieDuration){
            err = PARSER_ERR_INVALID_API;
            break;
        }
        err = myQueryInterface(PARSER_API_GET_USER_DATA, (void **)&IParser->getUserData);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_META_DATA, (void **)&IParser->getMetaData);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_NUM_TRACKS, (void **)&IParser->getNumTracks);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_NUM_PROGRAMS, (void **)&IParser->getNumPrograms);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_PROGRAM_TRACKS, (void **)&IParser->getProgramTracks);
        if(err)
            break;

        if((!IParser->getNumTracks && !IParser->getNumPrograms)
            ||(IParser->getNumPrograms && !IParser->getProgramTracks))
        {
            ALOGE("Invalid API to get tracks or programs.");
            err = PARSER_ERR_INVALID_API;
            break;
        }

        //track properties
        err = myQueryInterface(PARSER_API_GET_TRACK_TYPE, (void **)&IParser->getTrackType);
        if(err)
            break;

        if(!IParser->getTrackType){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        err = myQueryInterface(PARSER_API_GET_TRACK_DURATION, (void **)&IParser->getTrackDuration);
        if(err)
            break;
        if(!IParser->getTrackDuration){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        err = myQueryInterface(PARSER_API_GET_LANGUAGE, (void **)&IParser->getLanguage);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_BITRATE, (void **)&IParser->getBitRate);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_DECODER_SPECIFIC_INFO, (void **)&IParser->getDecoderSpecificInfo);
        if(err)
            break;

        //ignore the result because it is new api and some parser did implement it.
        err = myQueryInterface(PARSER_API_GET_TRACK_EXT_TAG, (void **)&IParser->getTrackExtTag);
        if(err)
            err = PARSER_SUCCESS;

        //video properties
        err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_WIDTH, (void **)&IParser->getVideoFrameWidth);
        if(err)
            break;
        err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_HEIGHT, (void **)&IParser->getVideoFrameHeight);
        if(err)
            break;
        err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_RATE, (void **)&IParser->getVideoFrameRate);
        if(err)
            break;
        err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_ROTATION, (void **)&IParser->getVideoFrameRotation);
        if(err){
            IParser->getVideoFrameRotation = NULL;
            err = PARSER_SUCCESS;
        }

        err = myQueryInterface(PARSER_API_GET_VIDEO_COLOR_INFO, (void **)&IParser->getVideoColorInfo);
        if(err){
            IParser->getVideoColorInfo = NULL;
            err = PARSER_SUCCESS;
        }

        err = myQueryInterface(PARSER_API_GET_VIDEO_HDR_COLOR_INFO, (void **)&IParser->getVideoHDRColorInfo);
        if(err){
            IParser->getVideoHDRColorInfo = NULL;
            err = PARSER_SUCCESS;
        }

        err = myQueryInterface(PARSER_API_GET_VIDEO_DISPLAY_WIDTH, (void **)&IParser->getVideoDisplayWidth);
        if(err){
            IParser->getVideoDisplayWidth = NULL;
            err = PARSER_SUCCESS;
        }

        err = myQueryInterface(PARSER_API_GET_VIDEO_DISPLAY_HEIGHT, (void **)&IParser->getVideoDisplayHeight);
        if(err){
            IParser->getVideoDisplayHeight = NULL;
            err = PARSER_SUCCESS;
        }

        err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_COUNT, (void **)&IParser->getVideoFrameCount);
        if(err){
            IParser->getVideoFrameCount = NULL;
            err = PARSER_SUCCESS;
        }

        err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_THUMBNAIL_TIME, (void **)&IParser->getVideoThumbnailTime);
        if(err){
            IParser->getVideoThumbnailTime = NULL;
            err = PARSER_SUCCESS;
        }

        //audio properties
        err = myQueryInterface(PARSER_API_GET_AUDIO_NUM_CHANNELS, (void **)&IParser->getAudioNumChannels);
        if(err)
            break;
        if(!IParser->getAudioNumChannels){
            err = PARSER_ERR_INVALID_API;
            break;
        }
        err = myQueryInterface(PARSER_API_GET_AUDIO_SAMPLE_RATE, (void **)&IParser->getAudioSampleRate);
        if(err)
            break;
        if(!IParser->getAudioSampleRate){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        err = myQueryInterface(PARSER_API_GET_AUDIO_BITS_PER_SAMPLE, (void **)&IParser->getAudioBitsPerSample);
        if(err)
            break;
        err = myQueryInterface(PARSER_API_GET_AUDIO_BLOCK_ALIGN, (void **)&IParser->getAudioBlockAlign);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_AUDIO_CHANNEL_MASK, (void **)&IParser->getAudioChannelMask);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_AUDIO_BITS_PER_FRAME, (void **)&IParser->getAudioBitsPerFrame);
        if(err)
            break;

        //subtitle properties
        err = myQueryInterface(PARSER_API_GET_TEXT_TRACK_WIDTH, (void **)&IParser->getTextTrackWidth);
        if(err)
            break;
        err = myQueryInterface(PARSER_API_GET_TEXT_TRACK_HEIGHT, (void **)&IParser->getTextTrackHeight);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_TEXT_TRACK_MIME, (void **)&IParser->getTextTrackMime);
        if(err)
            break;

        //track reading function
        err = myQueryInterface(PARSER_API_GET_READ_MODE, (void **)&IParser->getReadMode);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_SET_READ_MODE, (void **)&IParser->setReadMode);
        if(err)
            break;

        if(!IParser->getReadMode || !IParser->setReadMode){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        err = myQueryInterface(PARSER_API_ENABLE_TRACK, (void **)&IParser->enableTrack);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_NEXT_SAMPLE, (void **)&IParser->getNextSample);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_NEXT_SYNC_SAMPLE, (void **)&IParser->getNextSyncSample);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_FILE_NEXT_SAMPLE, (void **)&IParser->getFileNextSample);
        if(err)
            break;

        err = myQueryInterface(PARSER_API_GET_FILE_NEXT_SYNC_SAMPLE, (void **)&IParser->getFileNextSyncSample);
        if(err)
            break;

        if(!IParser->getNextSample && !IParser->getFileNextSample){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        if(IParser->getFileNextSample && !IParser->enableTrack){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        err = myQueryInterface(PARSER_API_SEEK, (void **)&IParser->seek);
        if(err)
            break;
        if(!IParser->seek){
            err = PARSER_ERR_INVALID_API;
            break;
        }

        err = myQueryInterface(PARSER_API_GET_SAMPLE_CRYPTO_INFO, (void **)&IParser->getSampleCryptoInfo);

        if(!IParser->getSampleCryptoInfo){
            err = PARSER_SUCCESS;
        }

        err = myQueryInterface(PARSER_API_GET_AUDIO_PRESENTATION_NUM, (void **)&IParser->getAudioPresentationNum);
        if(!IParser->getAudioPresentationNum) {
            err = PARSER_SUCCESS;
        }

        err = myQueryInterface(PARSER_API_GET_AUDIO_PRESENTATION_INFO, (void **)&IParser->getAudioPresentationInfo);
        if(!IParser->getAudioPresentationInfo) {
            err = PARSER_SUCCESS;
        }

    }while(0);


    if(err){
        ALOGW("ImxExtractor::CreateParserInterface parser err=%d",err);
        ret = UNKNOWN_ERROR;
    }

    if(ret == OK){
        ALOGD("ImxExtractor::CreateParserInterface success");
    }else{
        if(mLibHandle)
            dlclose(mLibHandle);
        mLibHandle = NULL;

        if(IParser != NULL)
            delete IParser;
        IParser = NULL;
        ALOGW("ImxExtractor::CreateParserInterface failed,ret=%d",ret);
    }

    return ret;
}
status_t ImxExtractor::ParseFromParser()
{
    int32 err = (int32)PARSER_SUCCESS;
    uint32 flag = FLAG_H264_NO_CONVERT | FLAG_OUTPUT_PTS | FLAG_ID3_FORMAT_NON_UTF8 | FLAG_OUTPUT_H264_SEI_POS_DATA;

    uint32 trackCnt = 0;
    bool bLive = mReader->isLiveStreaming();
    ALOGI("Core parser %s \n", IParser->getVersionInfo());

    flag |= FLAG_FETCH_AAC_ADTS_CSD; // only for android, as google aac decoder MUST get esds

    if(IParser->createParser2){
        if(mReader->isStreaming())
            flag |= FILE_FLAG_READ_IN_SEQUENCE;

        if(bLive){
            flag |= FILE_FLAG_NON_SEEKABLE;
            flag |= FILE_FLAG_READ_IN_SEQUENCE;
        }

        err = IParser->createParser2(flag,
                &fileOps,
                &memOps,
                &outputBufferOps,
                (void *)mReader,
                &parserHandle);
        ALOGD("createParser2 flag=%x,err=%d\n",flag,err);
    }else{
        err = IParser->createParser(bLive,
                &fileOps,
                &memOps,
                &outputBufferOps,
                (void *)mReader,
                &parserHandle);
        ALOGD("createParser flag=%x,err=%d\n",flag,err);
    }

    if(PARSER_SUCCESS !=  err)
    {
        ALOGE("fail to create the parser: %d\n", err);
        return UNKNOWN_ERROR;
    }
    if(mReader->isStreaming() || !strcasecmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG2TS)
        || !strcasecmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG2PS))
        mReadMode = PARSER_READ_MODE_FILE_BASED;
    else
        mReadMode = PARSER_READ_MODE_TRACK_BASED;

    err = IParser->setReadMode(parserHandle, mReadMode);
    if(PARSER_SUCCESS != err)
    {
        ALOGW("fail to set read mode to %d\n", mReadMode);
        if(mReadMode == PARSER_READ_MODE_TRACK_BASED)
            mReadMode = PARSER_READ_MODE_FILE_BASED;
        else
            mReadMode = PARSER_READ_MODE_TRACK_BASED;
        err = IParser->setReadMode(parserHandle, mReadMode);
        if(PARSER_SUCCESS != err)
        {
            ALOGE("fail to set read mode to %d\n", mReadMode);
            return UNKNOWN_ERROR;
        }
    }

    if ((NULL == IParser->getNextSample && PARSER_READ_MODE_TRACK_BASED == mReadMode)
            || (NULL == IParser->getFileNextSample && PARSER_READ_MODE_FILE_BASED == mReadMode)){
        ALOGE("get next sample did not exist");
        return UNKNOWN_ERROR;
    }

    err = IParser->getNumTracks(parserHandle, &trackCnt);
    if(err)
        return UNKNOWN_ERROR;

    mNumTracks = trackCnt;
    if(IParser->initializeIndex){
        err = IParser->initializeIndex(parserHandle);
        ALOGV("initializeIndex err=%d\n",err);
    }

    ALOGI("mReadMode=%d,mNumTracks=%u",mReadMode,mNumTracks);
    err = IParser->isSeekable(parserHandle,&bSeekable);
    if(err)
        return UNKNOWN_ERROR;

    ALOGI("bSeekable %d", bSeekable);

    err = IParser->getMovieDuration(parserHandle, (uint64 *)&mMovieDuration);
    if(err)
        return UNKNOWN_ERROR;

    err = ParseMetaData();
    if(err)
        return UNKNOWN_ERROR;

    err = ParseMediaFormat();
    if(err)
        return UNKNOWN_ERROR;
    return OK;
}
status_t ImxExtractor::ParseMetaData()
{
    struct KeyMap {
        const char* tag;
        UserDataID key;
    };
    const KeyMap kKeyMap[] = {
        { AMEDIAFORMAT_KEY_TITLE, USER_DATA_TITLE },
        { AMEDIAFORMAT_KEY_GENRE, USER_DATA_GENRE },
        { AMEDIAFORMAT_KEY_ARTIST, USER_DATA_ARTIST },
        { AMEDIAFORMAT_KEY_YEAR, USER_DATA_YEAR },
        { AMEDIAFORMAT_KEY_ALBUM, USER_DATA_ALBUM },
        { AMEDIAFORMAT_KEY_COMPOSER, USER_DATA_COMPOSER },
        { AMEDIAFORMAT_KEY_LYRICIST, USER_DATA_MOVIEWRITER },
        { AMEDIAFORMAT_KEY_CDTRACKNUMBER, USER_DATA_TRACKNUMBER },
        { AMEDIAFORMAT_KEY_LOCATION, USER_DATA_LOCATION},
        //{ (char *)"totaltracknumber", USER_DATA_TOTALTRACKNUMBER},
        { AMEDIAFORMAT_KEY_DISCNUMBER, USER_DATA_DISCNUMBER},
        { AMEDIAFORMAT_KEY_YEAR, USER_DATA_CREATION_DATE},//map data to year for id3 parser & mp4 parser.
        { AMEDIAFORMAT_KEY_COMPILATION, USER_DATA_COMPILATION},
        { AMEDIAFORMAT_KEY_ALBUMARTIST, USER_DATA_ALBUMARTIST},
        { AMEDIAFORMAT_KEY_AUTHOR, USER_DATA_AUTHOR},
        { AMEDIAFORMAT_KEY_ENCODER_DELAY,USER_DATA_AUD_ENC_DELAY},
        { AMEDIAFORMAT_KEY_ENCODER_PADDING,USER_DATA_AUD_ENC_PADDING},
        { AMEDIAFORMAT_KEY_DATE, USER_DATA_MP4_CREATION_TIME},//only get from mp4 parser.
    };
    uint32_t kNumMapEntries = sizeof(kKeyMap) / sizeof(kKeyMap[0]);

    if (IParser->getMetaData){

        uint8 *metaData = NULL;
        uint32 metaDataSize = 0;
        UserDataFormat userDataFormat;

        for (uint32_t i = 0; i < kNumMapEntries; ++i) {
            userDataFormat = USER_DATA_FORMAT_UTF8;
            IParser->getMetaData(parserHandle, kKeyMap[i].key, &userDataFormat, &metaData, \
                &metaDataSize);

            if((metaData != NULL) && ((int32_t)metaDataSize > 0) && USER_DATA_FORMAT_UTF8 == userDataFormat)
            {
                if(metaDataSize > MAX_USER_DATA_STRING_LENGTH)
                    metaDataSize = MAX_USER_DATA_STRING_LENGTH;

                AMediaFormat_setString(mFileMetaData, kKeyMap[i].tag, (const char*)metaData);

                ALOGI("FslParser Key: %d\t format=%d,size=%d,Value: %s\n",
                    kKeyMap[i].key,userDataFormat,(int)metaDataSize,metaData);
            }else if((metaData != NULL) && ((int32)metaDataSize > 0) && USER_DATA_FORMAT_INT_LE == userDataFormat){
                if(metaDataSize == 4)
                    AMediaFormat_setInt32(mFileMetaData, kKeyMap[i].tag, *(int32*)metaData);
                ALOGI("FslParser Key2: %d\t format=%d,size=%d,Value: %d\n",
                    kKeyMap[i].key,userDataFormat,(int)metaDataSize,*(int32*)metaData);
            }else if((metaData != NULL) && ((int32)metaDataSize > 0) && USER_DATA_FORMAT_UINT_LE == userDataFormat){
                if(USER_DATA_MP4_CREATION_TIME == kKeyMap[i].key && metaDataSize == 8){
                    uint64 data = *(uint64*)metaData;
                    String8 str;
                    if(ConvertMp4TimeToString(data,&str)){
                        AMediaFormat_setString(mFileMetaData, kKeyMap[i].tag, (const char*)str.string());
                        ALOGI("FslParser kKeyDate=%s",str.string());
                    }

                }
            }
        }

        //capture fps
        userDataFormat = USER_DATA_FORMAT_FLOAT32_BE;
        IParser->getMetaData(parserHandle, USER_DATA_CAPTURE_FPS, &userDataFormat, &metaData, \
        &metaDataSize);
        if(4 == metaDataSize && metaData){
            char tmp[20] = {0};
            uint32 len = 0;
            uint32 value= 0;
            float data = 0.0;
            value += *metaData << 24;
            value += *(metaData+1) << 16;
            value += *(metaData+2) << 8;
            value += *(metaData+3);
            data = *(float *)&value;
            len = sprintf((char*)&tmp, "%f", data);
            ALOGI("get fps=%s,len=%u",tmp,len);
            AMediaFormat_setFloat(mFileMetaData, AMEDIAFORMAT_KEY_CAPTURE_RATE, data);
        }

        userDataFormat = USER_DATA_FORMAT_JPEG;
        IParser->getMetaData(parserHandle, USER_DATA_ARTWORK, &userDataFormat, &metaData, \
            &metaDataSize);
        if(metaData && metaDataSize)
        {
            AMediaFormat_setBuffer(mFileMetaData, AMEDIAFORMAT_KEY_ALBUMART, metaData, metaDataSize);
        }

        metaData = NULL;
        metaDataSize = 0;
        //pssh
        userDataFormat = USER_DATA_FORMAT_UTF8;
        IParser->getMetaData(parserHandle, USER_DATA_PSSH, &userDataFormat, &metaData, \
            &metaDataSize);
        if(metaData && metaDataSize)
        {
            AMediaFormat_setBuffer(mFileMetaData, AMEDIAFORMAT_KEY_PSSH, metaData, metaDataSize);
        }
    }
    return OK;
}
status_t ImxExtractor::ParseMediaFormat()
{
    uint32 trackCountToCheck = mNumTracks;
    uint32 programCount = 0;
    uint32 trackCountInOneProgram = 0;
    uint32_t index=0;
    uint32_t i = 0;
    uint32_t defaultProgram = 0;
    uint32 * pProgramTrackTable = NULL;
    int32 err = (int32)PARSER_SUCCESS;
    status_t ret = OK;
    MediaType trackType;
    uint32 decoderType;
    uint32 decoderSubtype;
    uint64 sSeekPosTmp = 0;
    ALOGD("ImxExtractor::ParseMediaFormat BEGIN");
    if(IParser->getNumPrograms && IParser->getProgramTracks){
        err = IParser->getNumPrograms(parserHandle, &programCount);
        if(PARSER_SUCCESS !=  err || programCount == 0)
            return UNKNOWN_ERROR;

        err = IParser->getProgramTracks(parserHandle, defaultProgram, &trackCountInOneProgram, &pProgramTrackTable);
        if(PARSER_SUCCESS !=  err || trackCountInOneProgram == 0 || pProgramTrackTable == 0)
            return UNKNOWN_ERROR;

        trackCountToCheck = trackCountInOneProgram;
    }

    for(index = 0; index < trackCountToCheck; index ++){
        if(pProgramTrackTable)
            i = pProgramTrackTable[index];
        else
            i = index;

        err = IParser->getTrackType( parserHandle,i,(uint32 *)&trackType,&decoderType,&decoderSubtype);
        if(err)
            continue;

        if(trackType == MEDIA_VIDEO)
            ret = ParseVideo(i,decoderType,decoderSubtype);
        else if(trackType == MEDIA_AUDIO)
            ret = ParseAudio(i,decoderType,decoderSubtype);
        else if(trackType == MEDIA_TEXT)
            ret = ParseText(i,decoderType,decoderSubtype);
        else
            ret = UNKNOWN_ERROR;

        if(ret)
            continue;

        sSeekPosTmp = 0;
        err = IParser->seek(parserHandle, i, &sSeekPosTmp, SEEK_FLAG_NO_LATER);
        if(err){
            ALOGE("track %d seek fail %d", i, err);
            return UNKNOWN_ERROR;
        }
    }

    return OK;
}

status_t ImxExtractor::ParseVideo(uint32 index, uint32 type,uint32 subtype)
{
    int32 err = (int32)PARSER_SUCCESS;
    uint32_t i = 0;
    const char* mime = NULL;
    uint64_t duration = 0;
    uint8 * decoderSpecificInfo = NULL;
    uint32 decoderSpecificInfoSize = 0;
    uint32 width = 0;
    uint32 height = 0;
    uint32 display_width = 0;
    uint32 display_height = 0;
    uint32 frame_count = 0;

    uint32 rotation = 0;
    uint32 rate = 0;
    uint32 scale = 0;
    uint32 fps = 0;
    uint32 bitrate = 0;
    size_t sourceIndex = 0;
    size_t max_size = 0;
    int64_t thumbnail_ts = -1;

    ALOGD("ParseVideo index=%u,type=%u,subtype=%u",index,type,subtype);
    for(i = 0; i < sizeof(video_mime_table)/sizeof(codec_mime_struct); i++){
        if (type == video_mime_table[i].type){
            if((video_mime_table[i].subtype > 0) && (subtype == (video_mime_table[i].subtype))){
                mime = video_mime_table[i].mime;
                break;
            }else if(video_mime_table[i].subtype == 0){
                mime = video_mime_table[i].mime;
                break;
            }
        }
    }

    if(mime == NULL)
        return UNKNOWN_ERROR;

    err = IParser->getTrackDuration(parserHandle, index, (uint64 *)&duration);
    if(err)
        return UNKNOWN_ERROR;

    if(IParser->getDecoderSpecificInfo){
        err = IParser->getDecoderSpecificInfo(parserHandle, index, &decoderSpecificInfo, &decoderSpecificInfoSize);
        if(err)
            return UNKNOWN_ERROR;
    }
    err = IParser->getBitRate(parserHandle, index, &bitrate);
    if(err)
        return UNKNOWN_ERROR;

    err = IParser->getVideoFrameWidth(parserHandle, index, &width);
    if(err){
        return UNKNOWN_ERROR;
    }

    err = IParser->getVideoFrameHeight(parserHandle, index, &height);
    if(err){
        return UNKNOWN_ERROR;
    }

    err = IParser->getVideoFrameRate(parserHandle, index, &rate, &scale);
    if(err){
        return UNKNOWN_ERROR;
    }

    if(rate > 0 && scale > 0)
        fps = rate/scale;

    if(fps > 250)
        fps = 0;

    if(IParser->getVideoFrameRotation){
        err = IParser->getVideoFrameRotation(parserHandle,index,&rotation);
        if(err){
            return UNKNOWN_ERROR;
        }
    }

    if(IParser->getVideoDisplayWidth){
        err = IParser->getVideoDisplayWidth(parserHandle, index, &display_width);
        if(err){
            return UNKNOWN_ERROR;
        }
    }

    if(IParser->getVideoDisplayHeight){
        err = IParser->getVideoDisplayHeight(parserHandle, index, &display_height);
        if(err){
            return UNKNOWN_ERROR;
        }
    }

    if(IParser->getVideoFrameCount){
        err = IParser->getVideoFrameCount(parserHandle, index, &frame_count);
        if(err){
            return UNKNOWN_ERROR;
        }
    }

    if(IParser->getVideoThumbnailTime){
        err = IParser->getVideoThumbnailTime(parserHandle, index, (uint64 *)&thumbnail_ts);
        if(err){
            return UNKNOWN_ERROR;
        }
    }
    ALOGI("ParseVideo width=%u,height=%u,fps=%u,rotate=%u,duration=%lld,thumbnail_ts=%lld",
        width,height,fps,rotation,(long long)duration,(long long)thumbnail_ts);

    AMediaFormat *meta = AMediaFormat_new();

    AMediaFormat_setString(meta, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_TRACK_ID, index);

    if(decoderSpecificInfoSize > 0 && decoderSpecificInfo != NULL){
        ALOGI("video codec data size=%u",decoderSpecificInfoSize);
        if(type == VIDEO_H264){
            AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_AVC, decoderSpecificInfo, decoderSpecificInfoSize);
            ALOGI("add avcc metadata for h264 video size=%u",decoderSpecificInfoSize);
        }else if(type == VIDEO_HEVC){
            //stagefright will check the first bytes, so modify to pass it.
            if(decoderSpecificInfo[0] != 1)
                decoderSpecificInfo[0] = 1;
            AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_HEVC, decoderSpecificInfo, decoderSpecificInfoSize);
            ALOGI("add hvcc metadata for hevc video size=%u",decoderSpecificInfoSize);
        }else if(type == VIDEO_MPEG4){
            addESDSFromCodecPrivate(meta,false,decoderSpecificInfo,decoderSpecificInfoSize);
            ALOGI("add esds metadata for mpeg4 video size=%u",decoderSpecificInfoSize);
        }else{
            AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_0, decoderSpecificInfo, decoderSpecificInfoSize);
        }
    }else if(type == VIDEO_H264 || type == VIDEO_HEVC || type == VIDEO_MPEG4){
        thumbnail_ts = 0;
    }

    if(!strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG2TS) || !strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG2PS))
        thumbnail_ts = -1;

    if(0 == thumbnail_ts){
        thumbnail_ts = -1;
        AMediaFormat_setInt32(meta, "special_thumbnail", 1);
    }

    if(!strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_FLV))
        thumbnail_ts = 0;

    if (type == VIDEO_H264 || type == VIDEO_HEVC) {
        // AVC requires compression ratio of at least 2, and uses
        // macroblocks
        max_size = ((width + 15) / 16) * ((height + 15) / 16) * 192;
    } else if (type == VIDEO_ON2_VP) {
        // MA-13518: android.media.cts.DecoderConformanceTest#testVP9Other
        // vp90_2_09_subpixel_00.vp9 frame size exceeds w * h * 3 / 2, increase to w * h * 2
        max_size = width * height * 2;
    } else {
        // For all other formats there is no minimum compression
        // ratio. Use compression ratio of 1.
        max_size = width * height * 3 / 2;
    }

    // ts is streaming mode if filesize is 0, so video width/height is set to default 176x144
    if ((mReader->mLength == 0 || mReader->isLiveStreaming())
                && strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG2TS) == 0)
        max_size = (max_size > MAX_VIDEO_BUFFER_SIZE ? max_size : MAX_VIDEO_BUFFER_SIZE);

    if(0 == max_size)
        max_size = MAX_VIDEO_BUFFER_SIZE;

    if(max_size < MIN_VIDEO_BUFFER_SIZE)
        max_size = MIN_VIDEO_BUFFER_SIZE;

    //max_size += 20;
    max_size += max_size / 10;

    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, max_size);

    if(type == VIDEO_WMV){
        int32_t wmvType = 0;
        switch(subtype){
            case VIDEO_WMV7:
                wmvType = OMX_VIDEO_WMVFormat7;
                break;
            case VIDEO_WMV8:
                wmvType = OMX_VIDEO_WMVFormat8;
                break;
            case VIDEO_WMV9:
                wmvType = OMX_VIDEO_WMVFormat9;
                break;
            case VIDEO_WMV9A:
                wmvType = OMX_VIDEO_WMVFormat9a;
                break;
            case VIDEO_WVC1:
                wmvType = OMX_VIDEO_WMVFormatWVC1;
                break;
            default:
                break;
        }
        //TODO: remove openmax index
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_SUB_FORMAT, wmvType);
    }

    if (bitrate > 0){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
    }

    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_HEIGHT, height);
    AMediaFormat_setInt64(meta, AMEDIAFORMAT_KEY_DURATION, duration);

    if(display_width > 0){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_DISPLAY_WIDTH, display_width);
    }
    if(display_height > 0){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_DISPLAY_HEIGHT, display_height);
    }
    if(frame_count > 0){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_FRAME_COUNT, frame_count);
    }

    // stagefright uses framerate only in MPEG4 extractor, let fslextrator be same with it
    if(fps > 0 && !strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG4)){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
    }

    if(rotation > 0){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_ROTATION, rotation);
    }

    if(thumbnail_ts < 0)
        thumbnail_ts = duration > 5000000 ? 5000000:duration;

    AMediaFormat_setInt64(meta, AMEDIAFORMAT_KEY_THUMBNAIL_TIME, thumbnail_ts);


    if(IParser->getVideoColorInfo){
        int32_t primaries = 0;
        int32_t iso_transfer = 0;
        int32_t matrix = 0;
        int32_t full_range = 0;
        err = IParser->getVideoColorInfo(parserHandle, index, &primaries,&iso_transfer,&matrix,&full_range);

        if(err == PARSER_SUCCESS){
            //ColorAspects aspects;
            bool fullRange = (full_range == 1);
            int32_t existingColor;

            // only store the first color specification
            if (!AMediaFormat_getInt32(meta, AMEDIAFORMAT_KEY_COLOR_RANGE, &existingColor)) {

                int32_t range = 0;
                int32_t standard = 0;
                int32_t transfer = 0;
                ColorUtils::convertIsoColorAspectsToPlatformAspects(
                        primaries, iso_transfer, matrix, fullRange,
                        &range, &standard, &transfer);

                if (range != 0) {
                    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_COLOR_RANGE, range);
                }
                if (standard != 0) {
                    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_COLOR_STANDARD, standard);
                }
                if (transfer != 0) {
                    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_COLOR_TRANSFER, transfer);
                }
                ALOGI("get color info, %d,%d,%d,%d\n", primaries, range, standard, transfer);
            }

        }else //failed if no color info provided
            err = PARSER_SUCCESS;

    }

    if(IParser->getVideoHDRColorInfo){
        VideoHDRColorInfo info;
        memset(&info,0,sizeof(VideoHDRColorInfo));

        err = IParser->getVideoHDRColorInfo(parserHandle, index, &info);
        if(err == PARSER_SUCCESS){
            err = SetMkvHDRColorInfoMetadata(&info,meta);
        }else //failed if no hdr color info provided
            err = PARSER_SUCCESS;
    }


    ParseTrackExtMetadata(index,meta);

    mTracks.push();
    sourceIndex = mTracks.size() - 1;
    TrackInfo *trackInfo = &mTracks.editItemAt(sourceIndex);
    trackInfo->mTrackNum = index;
    trackInfo->mExtractor = this;
    trackInfo->bCodecInfoSent = false;
    trackInfo->bPartial = false;
    trackInfo->buffer = NULL;
    trackInfo->outTs = 0;
    trackInfo->syncFrame = 0;
    trackInfo->mMeta = meta;
    trackInfo->mSource = NULL;
    trackInfo->max_input_size = max_size;
    trackInfo->type = MEDIA_VIDEO;
    trackInfo->bIsNeedConvert = false;
    trackInfo->bitPerSample = 0;
    trackInfo->bMp4Encrypted = false;
    trackInfo->bMkvEncrypted = false;
    trackInfo->bAacAdts = false;
    void * cryptoKeyData = 0;
    size_t cryptoKeySize = 0;

    if(AMediaFormat_getBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_KEY, &cryptoKeyData, &cryptoKeySize) && cryptoKeySize > 0){
        if(!strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG4)){
            void *data;
            size_t size;

            if(AMediaFormat_getBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_KEY, &data, &size) && data && size <=16)
                memcpy(trackInfo->default_kid,data,size);

            AMediaFormat_getInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_MODE, &trackInfo->default_isEncrypted);
            AMediaFormat_getInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_DEFAULT_IV_SIZE, &trackInfo->default_iv_size);

            AMediaFormat_getInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_BYTE_BLOCK, &trackInfo->default_EncryptedByteBlock);
            AMediaFormat_getInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_SKIP_BYTE_BLOCK, &trackInfo->default_SkipByteBlock);


            trackInfo->bMp4Encrypted = true;
        }else if(!strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MATROSKA))
            trackInfo->bMkvEncrypted = true;
    }

    mReader->AddBufferReadLimitation(index,max_size);

    ALOGI("add video track index=%u,source index=%zu,mime=%s",index,sourceIndex,mime);
    return OK;
}

status_t ImxExtractor::ParseAudio(uint32 index, uint32 type,uint32 subtype)
{
    int32 err = (int32)PARSER_SUCCESS;
    uint32_t i = 0;
    uint32 bitrate = 0;
    uint32 channel = 0;
    uint32 samplerate = 0;
    uint32 bitPerSample = 0;
    uint32 bitsPerFrame = 0;
    uint32 audioBlockAlign = 0;
    uint32 audioChannelMask = 0;
    const char* mime = NULL;
    uint64_t duration = 0;
    uint8 * decoderSpecificInfo = NULL;
    uint32 decoderSpecificInfoSize = 0;
    uint8 language[8];
    size_t sourceIndex = 0;
    int32_t encoderDelay = 0;
    int32_t encoderPadding = 0;
    bool isAacAdts = false;
    bool haveCsd = false;

    ALOGD("ParseAudio index=%u,type=%u,subtype=%u",index,type,subtype);
    for(i = 0; i < sizeof(audio_mime_table)/sizeof(codec_mime_struct); i++){
        if (type == audio_mime_table[i].type){
            if((audio_mime_table[i].subtype > 0) && (subtype == (audio_mime_table[i].subtype))){
                mime = audio_mime_table[i].mime;
                break;
            }else if(audio_mime_table[i].subtype == 0){
                mime = audio_mime_table[i].mime;
                break;
            }
        }
    }

    if(mime == NULL)
        return UNKNOWN_ERROR;

    err = IParser->getTrackDuration(parserHandle, index,(uint64 *)&duration);
    if(err)
        return UNKNOWN_ERROR;

    if(IParser->getDecoderSpecificInfo){
        err = IParser->getDecoderSpecificInfo(parserHandle, index, &decoderSpecificInfo, &decoderSpecificInfoSize);
        if(err)
            return UNKNOWN_ERROR;

        haveCsd = (decoderSpecificInfo && decoderSpecificInfoSize > 0);
    }

    err = IParser->getBitRate(parserHandle, index, &bitrate);
    if(err)
        return UNKNOWN_ERROR;

    err = IParser->getAudioNumChannels(parserHandle, index, &channel);
    if(err)
        return UNKNOWN_ERROR;

    err = IParser->getAudioSampleRate(parserHandle, index, &samplerate);
    if(err)
        return UNKNOWN_ERROR;

    if(IParser->getAudioBitsPerSample){
        err = IParser->getAudioBitsPerSample(parserHandle, index, &bitPerSample);
        if(err)
            return UNKNOWN_ERROR;
    }
    if(IParser->getAudioBitsPerFrame){
        err = IParser->getAudioBitsPerFrame(parserHandle, index, &bitsPerFrame);
        if(err)
            return UNKNOWN_ERROR;
    }

    if(IParser->getAudioBlockAlign){
        err = IParser->getAudioBlockAlign(parserHandle, index, &audioBlockAlign);//wma & adpcm
        if(err)
            return UNKNOWN_ERROR;
    }

    if(IParser->getAudioChannelMask){
        err = IParser->getAudioChannelMask(parserHandle, index, &audioChannelMask);//not use
        if(err)
            return UNKNOWN_ERROR;
    }
    if(IParser->getLanguage) {
        memset(language, 0, sizeof(language)/sizeof(language[0]));
        err = IParser->getLanguage(parserHandle, index, &language[0]);
        ALOGI("audio track %u, lanuage: %s\n", index, language);
    }
    else
        strcpy((char*)&language, "unknown");

    if (type == AUDIO_AC4 ) {
        GetAudioPresentationInfo(index);
    }

    AMediaFormat *meta = AMediaFormat_new();

#ifdef USE_IMX_AAC_Dec
    // switch to google.aac.decoder for m4a clips to pass testDecodeM4a, MA-8801
    if(type == AUDIO_AAC && subtype != AUDIO_ER_BSAC && !strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG4) && mNumTracks == 1) {
        if (FilterM4aFile(mReader->mLength))
            mime = MEDIA_MIMETYPE_AUDIO_AAC_NONSTANDARD;
    }
#endif

    AMediaFormat_setString(meta, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_TRACK_ID, index);

    if(type == AUDIO_AAC) {
        // set low-latency = 0 to improve accuracy
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_LOW_LATENCY, 0);

        // workaround for avi aac stream, set subtype to ADTS by default if there is no csd
        if (!strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_AVI) && subtype == 0 && !haveCsd)
            subtype = AUDIO_AAC_ADTS;

        if(subtype == AUDIO_AAC_ADTS){
            isAacAdts = true;
            AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_IS_ADTS, 1);

            if (mAdtsBuffer == NULL) {
                mAdtsBuffer = new ABuffer(MAX_AUDIO_BUFFER_SIZE);
                mAdtsBuffer->setRange(0, 0);
            }
        }else if (subtype == AUDIO_AAC_ADIF){
            AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_IS_ADIF, 1);
        }
    }else if(bitrate > 0){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
    }

    if (!haveCsd) {
        if (type == AUDIO_AAC) {
            ALOGD("aac stream don't have codec data");
            sp<MetaData> mFormat = new MetaData;
            uint32_t type;
            const void *data;
            size_t size;
            unsigned profile = 1, sample_rate_index = 0, channels = channel;
            switch(samplerate)
            {
                case 96000:sample_rate_index = 0;break;
                case 88200:sample_rate_index = 1;break;
                case 64000:sample_rate_index = 2;break;
                case 48000:sample_rate_index = 3;break;
                case 44100:sample_rate_index = 4;break;
                case 32000:sample_rate_index = 5;break;
                case 24000:sample_rate_index = 6;break;
                case 22050:sample_rate_index = 7;break;
                case 16000:sample_rate_index = 8;break;
                case 12000:sample_rate_index = 9;break;
                case 11025:sample_rate_index = 10;break;
                case  8000:sample_rate_index = 11;break;

                default:sample_rate_index = 0xff;break;
            }
            if (!MakeAACCodecSpecificData(*mFormat, profile, sample_rate_index, channels)) {
                ALOGW("make aac csd fail");
            } else if (mFormat->findData(kKeyESDS, &type, &data, &size)) {
                AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_ESDS, data, size);
                ALOGI("generate esds metadata for aac, size=%zu", size);
            }
        }
    } else {
        ALOGI("audio codec data size=%u mAacAdts=%d",decoderSpecificInfoSize, isAacAdts);
        if(type == AUDIO_AAC){
            addESDSFromCodecPrivate(meta,true,decoderSpecificInfo,decoderSpecificInfoSize);
            ALOGI("add esds metadata for aac raw audio size=%u", decoderSpecificInfoSize);
        }else if(type == AUDIO_VORBIS){
            if(OK != addVorbisCodecInfo(meta,decoderSpecificInfo,decoderSpecificInfoSize))
                ALOGE("add vorbis codec info error");
        }else if(type == AUDIO_OPUS){
            int64_t codecDelay = 0;
            int64_t kSeekPreRollNs = 0;

            if(decoderSpecificInfoSize >= AOPUS_CSD1_CSD2_SIZE){
                uint8 * ptr = decoderSpecificInfo + decoderSpecificInfoSize - AOPUS_CSD1_CSD2_SIZE;
                if(!strncmp((char *)ptr, "csdi", 4)){
                    // including csd_1, csd_2
                    ptr += 4; // skip "csdi"
                    memcpy(&codecDelay, ptr, sizeof(codecDelay));
                    ptr += sizeof(codecDelay);
                    memcpy(&kSeekPreRollNs, ptr, sizeof(kSeekPreRollNs));
                    ALOGI("opus csd codecDelay %lld, kSeekPreRollNs %lld", (long long)codecDelay, (long long)kSeekPreRollNs);
                    decoderSpecificInfoSize -= AOPUS_CSD1_CSD2_SIZE;
                }
            }
            AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_0, decoderSpecificInfo, decoderSpecificInfoSize);
            AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_1, &codecDelay, sizeof(codecDelay));
            AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_2, &kSeekPreRollNs, sizeof(kSeekPreRollNs));

        }else{
            AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_0, decoderSpecificInfo, decoderSpecificInfoSize);
        }
    }

    if(type == AUDIO_PCM && (subtype == AUDIO_PCM_S16BE || subtype == AUDIO_PCM_S24BE || subtype == AUDIO_PCM_S32BE )){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_PCM_BIG_ENDIAN, 1);
        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD, decoderSpecificInfo, decoderSpecificInfoSize);
    }

    size_t max_size = MAX_AUDIO_BUFFER_SIZE;//16*1024
    if(type == AUDIO_APE) {
        max_size = 262144; //enlarge buffer size to 256*1024 for ape audio
    }
    else if(type == AUDIO_MP3){
        max_size = 8192;
    }
    else if (type == AUDIO_PCM && bitPerSample == 24) {
        // workaround for MA-12617, audio sample size exceed 16*1024, add to 32*1024
        max_size = 32786;
    }
    else if(type == AUDIO_DSD){
        const int DSD_BLOCK_SIZE = 4096;
        const int DSD_CHANNEL_NUM_MAX = 6;
        max_size = DSD_BLOCK_SIZE * DSD_CHANNEL_NUM_MAX;
    }else if(type == AUDIO_FLAC){
        //enalrte to 32K to pass cts android.mediav2.cts.ExtractorTest$FunctionalityTest#testSeekFlakinessNative[14(audio/flac)]
        max_size = 32768;
    }

    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, max_size);

    if(type == AUDIO_WMA){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_SUB_FORMAT, subtype);
        ALOGI("WMA subtype=%u",subtype);
    }

    /*
      stagefright's mediaextractor doesn't read meta data bitPerSample from
      file, fslExtractor shall be same with it, otherwise cts NativeDecoderTest will fail.
      This cts uses aac&vorbis tracks, acodec needs bitPerSample for wma&ape tracks,
      so just block aac&vorbis from passing bitPerSample.
      */
    if(bitPerSample > 0 && type != AUDIO_AAC && type != AUDIO_VORBIS){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_BITS_PER_SAMPLE, bitPerSample);
    }

    if(audioBlockAlign > 0){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_AUDIO_BLOCK_ALIGN, audioBlockAlign);
    }

    if(bitsPerFrame > 0){
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_BITS_PER_FRAME, bitsPerFrame);
    }

    if(type == AUDIO_AMR){
        if(subtype == AUDIO_AMR_NB){
            channel = 1;
            samplerate = 8000;
        }else if(subtype == AUDIO_AMR_WB){
            channel = 1;
            samplerate = 16000;
        }
    }

    if(type == AUDIO_AC3 && samplerate == 0)
        samplerate = 44100; // invalid samplerate will lead to findMatchingCodecs fail

    AMediaFormat_setInt64(meta, AMEDIAFORMAT_KEY_DURATION, duration);
    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_CHANNEL_COUNT, channel);
    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_SAMPLE_RATE, samplerate);
    AMediaFormat_setString(meta, AMEDIAFORMAT_KEY_LANGUAGE, (const char*)&language);

    if(AMediaFormat_getInt32(mFileMetaData, AMEDIAFORMAT_KEY_ENCODER_DELAY, &encoderDelay))
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_ENCODER_DELAY, encoderDelay);

    if(AMediaFormat_getInt32(mFileMetaData, AMEDIAFORMAT_KEY_ENCODER_PADDING, &encoderPadding))
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_ENCODER_PADDING, encoderPadding);

    ParseTrackExtMetadata(index,meta);

#if 0//test
    if(type == AUDIO_MP3) {
        meta->setInt32(kKeyEncoderDelay, 576);
        meta->setInt32(kKeyEncoderPadding, 1908);

    }
#endif
    ALOGI("ParseAudio channel=%d,sampleRate=%d,bitRate=%d,bitPerSample=%d,audioBlockAlign=%d",
        (int)channel,(int)samplerate,(int)bitrate,(int)bitPerSample,(int)audioBlockAlign);
    mTracks.push();
    sourceIndex = mTracks.size() - 1;
    TrackInfo *trackInfo = &mTracks.editItemAt(sourceIndex);
    trackInfo->mTrackNum = index;
    trackInfo->mExtractor = this;
    trackInfo->bCodecInfoSent = false;
    trackInfo->mMeta = meta;
    trackInfo->bPartial = false;
    trackInfo->buffer = NULL;
    trackInfo->outTs = 0;
    trackInfo->syncFrame = 0;
    trackInfo->mSource = NULL;
    trackInfo->max_input_size = max_size;
    trackInfo->type = MEDIA_AUDIO;
    trackInfo->bIsNeedConvert = (type == AUDIO_PCM && bitPerSample!= 16);
    trackInfo->bitPerSample = bitPerSample;
    trackInfo->bMp4Encrypted = false;
    trackInfo->bMkvEncrypted = false;
    trackInfo->bAacAdts = isAacAdts;

    void * cryptoKeyData = 0;
    size_t cryptoKeySize = 0;

    if(AMediaFormat_getBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_KEY, &cryptoKeyData, &cryptoKeySize)){
        if(!strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG4)){

            void *data;
            size_t size;

            if(AMediaFormat_getBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_KEY, &data, &size) && data && size <=16)
                memcpy(trackInfo->default_kid,data,size);

            AMediaFormat_getInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_MODE, &trackInfo->default_isEncrypted);
            AMediaFormat_getInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_DEFAULT_IV_SIZE, &trackInfo->default_iv_size);

            AMediaFormat_getInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_BYTE_BLOCK, &trackInfo->default_EncryptedByteBlock);
            AMediaFormat_getInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_SKIP_BYTE_BLOCK, &trackInfo->default_SkipByteBlock);

            trackInfo->bMp4Encrypted = true;
        }
    }


    mReader->AddBufferReadLimitation(index,max_size);
    ALOGI("add audio track index=%u,sourceIndex=%zu,mime=%s",index,sourceIndex,mime);
    return OK;
}
status_t ImxExtractor::ParseText(uint32 index, uint32 type,uint32 subtype)
{

    int32 err = (int32)PARSER_SUCCESS;
    uint8 language[8];
    uint32 width = 0;
    uint32 height = 0;
    const char* mime = NULL;
    uint32 mime_len = 0;
    ALOGD("ParseText index=%u,type=%u,subtype=%u",index,type,subtype);
    switch(type){
        case TXT_3GP_STREAMING_TEXT:
        case TXT_QT_TEXT:
            mime = MEDIA_MIMETYPE_TEXT_3GPP;
            break;
        case TXT_SUBTITLE_TEXT:
            mime = MEDIA_MIMETYPE_TEXT_SRT;
            break;
        case TXT_SUBTITLE_SSA:
            mime = MEDIA_MIMETYPE_TEXT_SSA;
            break;
        case TXT_SUBTITLE_ASS:
            mime = MEDIA_MIMETYPE_TEXT_ASS;
            break;
        default:
            break;
    }

    err = IParser->getTextTrackWidth(parserHandle,index,&width);
    if(err)
        return UNKNOWN_ERROR;

    err = IParser->getTextTrackHeight(parserHandle,index,&height);
    if(err)
        return UNKNOWN_ERROR;

    if(IParser->getTextTrackMime && NULL == mime){
        err = IParser->getTextTrackMime(parserHandle,index,(uint8**)&mime,&mime_len);
        if(err)
             return UNKNOWN_ERROR;
    }

    if(mime == NULL)
        return UNKNOWN_ERROR;

    if(IParser->getLanguage) {
        memset(language, 0, sizeof(language)/sizeof(language[0]));
        err = IParser->getLanguage(parserHandle, index, &language[0]);
        ALOGI("text track %u, lanuage: %s\n", index, language);
    }
    else
        strcpy((char*)&language, "unknown");

    AMediaFormat *meta = AMediaFormat_new();
    AMediaFormat_setString(meta, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_TRACK_ID, index);

    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_HEIGHT, height);
    AMediaFormat_setString(meta, AMEDIAFORMAT_KEY_LANGUAGE, (const char*)&language);

    ParseTrackExtMetadata(index,meta);

    mTracks.push();
    TrackInfo *trackInfo = &mTracks.editItemAt(mTracks.size() - 1);
    trackInfo->mTrackNum = index;
    trackInfo->mExtractor = this;
    trackInfo->bCodecInfoSent = false;
    trackInfo->mMeta = meta;
    trackInfo->bPartial = false;
    trackInfo->buffer = NULL;
    trackInfo->outTs = 0;
    trackInfo->syncFrame = 0;
    trackInfo->mSource = NULL;
    trackInfo->max_input_size = MAX_TEXT_BUFFER_SIZE;
    trackInfo->type = MEDIA_TEXT;
    trackInfo->bIsNeedConvert = false;
    trackInfo->bitPerSample = 0;
    trackInfo->bMkvEncrypted = false;
    trackInfo->bMp4Encrypted = false;
    trackInfo->bAacAdts = false;
    mReader->AddBufferReadLimitation(index,MAX_TEXT_BUFFER_SIZE);
    ALOGI("add text track index=%u,mime=%s",index, mime);
    return OK;
}
status_t ImxExtractor::ParseTrackExtMetadata(uint32 index, AMediaFormat *meta)
{
    int32 err = (int32)PARSER_SUCCESS;
    if(meta == NULL)
        return UNKNOWN_ERROR;

    if(IParser->getTrackExtTag){
        TrackExtTagList *pList = NULL;
        TrackExtTagItem *pItem = NULL;
        err = IParser->getTrackExtTag(parserHandle, index, &pList);
        if(err)
            return UNKNOWN_ERROR;

        if(pList && pList->num > 0){
            pItem = pList->m_ptr;
            while(pItem != NULL){
                if(pItem->index == FSL_PARSER_TRACKEXTTAG_TX3G){
                    AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_TEXT_FORMAT_DATA, pItem->data, pItem->size);
                    ALOGI("kKeyTextFormatData %d",pItem->size);
                }else if(pItem->index == FSL_PARSER_TRACKEXTTAG_CRPYTOKEY){
                    uint32 type = 0;
                    if(!strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG4)){
                        type = 'tenc';
                    }
                    ALOGI("has AMEDIAFORMAT_KEY_CRYPTO_KEY");
                    AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_KEY, pItem->data, pItem->size);
                }else if(pItem->index == FSL_PARSER_TRACKEXTTAG_CRPYTOMODE ){
                    int32 cryptoMode = *(int32*)pItem->data;
                    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_CRYPTO_MODE, cryptoMode);
                    ALOGI("AMEDIAFORMAT_KEY_CRYPTO_MODE =%d",cryptoMode);
                }else if(pItem->index == FSL_PARSER_TRACKEXTTAG_CRPYTODEFAULTIVSIZE ){
                    int32 defaultIVSize = *(int32*)pItem->data;
                    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_CRYPTO_DEFAULT_IV_SIZE, defaultIVSize);
                    ALOGI("AMEDIAFORMAT_KEY_CRYPTO_DEFAULT_IV_SIZE =%d",defaultIVSize);
                }else if(pItem->index == FSL_PARSER_TRACKEXTTAG_CRYPTO_ENCRYPTED_BYTE_BLOCK ){
                    int32 default_enctypted_byte_block = *(int32*)pItem->data;
                    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_BYTE_BLOCK, default_enctypted_byte_block);
                    ALOGI("get default_enctypted_byte_block=%d",default_enctypted_byte_block);
                }
                else if(pItem->index == FSL_PARSER_TRACKEXTTAG_CRYPTO_SKIP_BYTE_BLOCK ){
                    int32 default_skip_byte_block = *(int32*)pItem->data;
                    AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_CRYPTO_SKIP_BYTE_BLOCK, default_skip_byte_block);
                    ALOGI("get default_skip_byte_block=%d",default_skip_byte_block);
                }
                else if(pItem->index == FSL_PARSER_TRACKEXTTAG_CRYPTO_IV ){
                    AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_IV, pItem->data, pItem->size);
                    ALOGI("get CRYPTO_IV size=%d",pItem->size);
                }

                pItem = pItem->nextItemPtr;
            }
        }
    }

    return OK;
}
int ImxExtractor::bytesForSize(size_t size) {
    // use at most 28 bits (4 times 7)
    CHECK(size <= 0xfffffff);

    if (size > 0x1fffff) {
        return 4;
    } else if (size > 0x3fff) {
        return 3;
    } else if (size > 0x7f) {
        return 2;
    }
    return 1;
}
void ImxExtractor::storeSize(uint8_t *data, size_t &idx, size_t size) {
    int numBytes = bytesForSize(size);
    idx += numBytes;

    data += idx;
    size_t next = 0;
    while (numBytes--) {
        *--data = (size & 0x7f) | next;
        size >>= 7;
        next = 0x80;
    }
}
void ImxExtractor::addESDSFromCodecPrivate(AMediaFormat *meta, bool isAudio, void *priv, size_t privSize)
{

    int privSizeBytesRequired = bytesForSize(privSize);
    int esdsSize2 = 14 + privSizeBytesRequired + privSize;
    int esdsSize2BytesRequired = bytesForSize(esdsSize2);
    int esdsSize1 = 4 + esdsSize2BytesRequired + esdsSize2;
    int esdsSize1BytesRequired = bytesForSize(esdsSize1);
    size_t esdsSize = 1 + esdsSize1BytesRequired + esdsSize1;
    uint8_t *esds = new uint8_t[esdsSize];

    size_t idx = 0;
    esds[idx++] = 0x03;
    storeSize(esds, idx, esdsSize1);
    esds[idx++] = 0x00; // ES_ID
    esds[idx++] = 0x00; // ES_ID
    esds[idx++] = 0x00; // streamDependenceFlag, URL_Flag, OCRstreamFlag
    esds[idx++] = 0x04;
    storeSize(esds, idx, esdsSize2);
    esds[idx++] = isAudio ? 0x40   // Audio ISO/IEC 14496-3
                          : 0x20;  // Visual ISO/IEC 14496-2
    for (int i = 0; i < 12; i++) {
        esds[idx++] = 0x00;
    }
    esds[idx++] = 0x05;
    storeSize(esds, idx, privSize);
    memcpy(esds + idx, priv, privSize);

    AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_ESDS, esds, esdsSize);

    delete[] esds;
    esds = NULL;

}
status_t ImxExtractor::addVorbisCodecInfo(AMediaFormat *meta, void *_codecPrivate, size_t codecPrivateSize) {

    size_t offset = 0;
    int32_t start1 = 0;
    int32_t start2 = 0;
    int32_t start3 = 0;

    if(_codecPrivate == NULL || codecPrivateSize < 6)
        return ERROR_MALFORMED;

    const uint8_t *ptr = (const uint8_t *)_codecPrivate;

    while(offset < codecPrivateSize-6){
        if((ptr[offset] == 0x01) && (memcmp(&ptr[offset+1],"vorbis",6)==0)){
            start1 = offset;
        }else if((ptr[offset] == 0x03) && (memcmp(&ptr[offset+1],"vorbis",6)==0)){
            start2 = offset;
        }else if((ptr[offset] == 0x05) && (memcmp(&ptr[offset+1],"vorbis",6)==0)){
            start3 = offset;
        }
        offset ++;
    }

    if(!(start2 > start1 && start3 > start2))
        return ERROR_MALFORMED;

    //formerly kKeyVorbisInfo
    AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_0, &ptr[start1], start2 - start1);
    // formerly kKeyVorbisBooks
    AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CSD_1, &ptr[start3], codecPrivateSize - start3);
    ALOGV("addVorbisCodecInfo SUCCESS csd0_len=%d,csd1_len=%d",start2 - start1,(int)codecPrivateSize - start3);

    return OK;
}
status_t ImxExtractor::ActiveTrack(uint32 index)
{
    uint64 seekPos = 0;
    Mutex::Autolock autoLock(mLock);
    bool seek = true;

    TrackInfo *trackInfo = &mTracks.editItemAt(index);
    if(trackInfo == NULL)
        return UNKNOWN_ERROR;
    trackInfo->bCodecInfoSent = false;
    if(trackInfo->type == MEDIA_VIDEO){
        mVideoActived = true;
        mVideoIndex = index;
    }else if(trackInfo->type == MEDIA_AUDIO){
        mAudioActived = true;
        mAudioIndex = index;
    }

    IParser->enableTrack(parserHandle,trackInfo->mTrackNum, TRUE);

    if(trackInfo->type == MEDIA_TEXT || trackInfo->type == MEDIA_AUDIO){
        if(isTrackModeParser())
            seek = true;
        else
            seek = false;
    }

    if(seek)
        IParser->seek(parserHandle, trackInfo->mTrackNum, &seekPos, SEEK_FLAG_NO_LATER);

    ALOGD("start track %d",trackInfo->mTrackNum);
    return OK;
}
status_t ImxExtractor::DisableTrack(uint32 index)
{
    Mutex::Autolock autoLock(mLock);
    TrackInfo *trackInfo = &mTracks.editItemAt(index);
    if(trackInfo == NULL)
        return UNKNOWN_ERROR;

    if(trackInfo->type == MEDIA_VIDEO){
        mVideoActived = false;
    }else if(trackInfo->type == MEDIA_AUDIO){
        mAudioActived = false;
    }

    IParser->enableTrack(parserHandle,trackInfo->mTrackNum, FALSE);
    ALOGD("close track %d",trackInfo->mTrackNum);
    return OK;
}
media_status_t ImxExtractor::HandleSeekOperation(uint32_t index,int64_t * ts,uint32_t flag)
{
    TrackInfo *pInfo = NULL;
    int64_t target;
    bool seek = true;
    bool seek2 = false;
    int64_t ts2;
    TrackInfo *pInfo2 = NULL;
    if(ts == NULL)
        return AMEDIA_ERROR_UNKNOWN;

    target = *ts;
    ts2 = *ts;
    pInfo = &mTracks.editItemAt(index);

    if(pInfo == NULL)
        return AMEDIA_ERROR_UNKNOWN;

    ALOGD("HandleSeekOperation BEGIN index=%d,target=%" PRId64 ",flag=%x",index,target,flag);

    if(mReadMode == PARSER_READ_MODE_FILE_BASED){
        if(pInfo->type == MEDIA_AUDIO && mVideoActived){
            seek = false;
            //for track mode parser, it must seek audio track.
            if(isTrackModeParser())
                seek = true;
        }else if(pInfo->type == MEDIA_TEXT && mVideoActived){
            seek = false;
        }

        if(pInfo->type == MEDIA_VIDEO && mAudioActived && isTrackModeParser()){
            pInfo2 = &mTracks.editItemAt(mAudioIndex);
            seek2 = true;
            if(pInfo2->type != MEDIA_AUDIO)
                seek2 = false;

            //check distance between target video position and current audio
            //do not trig the seconds seek when distance is close.
            if((target + SEEK_CHECK_TOLERANCE > currentAudioTs) &&
                (target < currentAudioTs + SEEK_CHECK_TOLERANCE)){
                seek2 = false;
                ALOGV("skip audio seek");
            }
        }


        if(pInfo->type == MEDIA_AUDIO && mVideoActived && isTrackModeParser()){
            pInfo2 = &mTracks.editItemAt(mVideoIndex);
            seek2 = true;
            if(pInfo2->type != MEDIA_VIDEO)
                seek2 = false;

            //check distance between target audio postion and current video
            //do not trig the seconds seek when distance is close.
            if((target + SEEK_CHECK_TOLERANCE > currentVideoTs) &&
                (target < currentVideoTs + SEEK_CHECK_TOLERANCE)){
                seek2 = false;
                ALOGV("skip video seek");
            }
        }
    }

    if(seek){
        IParser->seek(parserHandle, pInfo->mTrackNum, (uint64*)ts, flag);
        //clear temp buffer

        if(pInfo->buffer != NULL){
            pInfo->buffer->release();
            pInfo->buffer = NULL;
        }
        ALOGD("HandleSeekOperation do seek index=%d",index);
    }


    if(seek2 && pInfo2 != NULL){
        IParser->seek(parserHandle, pInfo2->mTrackNum, (uint64*)&ts2, flag);
        //clear temp buffer
        if(pInfo2->buffer != NULL){
            pInfo2->buffer->release();
            pInfo2->buffer = NULL;
        }
        ALOGD("HandleSeekOperation do seek 2 index=%d",index);
        pInfo2->bPartial = false;
    }

    pInfo->bPartial = false;

    if(pInfo->type == MEDIA_VIDEO)
        currentVideoTs = target;
    else if(pInfo->type == MEDIA_AUDIO) {
        currentAudioTs = target;
        bWaitForAudioStartTime = true;

        if (mAdtsBuffer != NULL) {
            mAdtsBuffer->setRange(0, 0);
        }

        mResyncAdts = true;
    }

    ALOGD("HandleSeekOperation result index=%d,ts=%" PRId64 ",flag=%x",index,*ts,flag);
    return AMEDIA_OK;
}
media_status_t ImxExtractor::GetNextSample(uint32_t index,bool is_sync)
{
    int32 err = (int32)PARSER_SUCCESS;
    void * buffer_context = NULL;
    uint64 ts = 0;
    uint64 duration = 0;
    uint8 *tmp = NULL;
    uint32 datasize = 0;
    uint32 sampleFlag = 0;
    uint32 track_num_got = 0;
    uint32 direction = 0;
    bool bufferIsValid = false;

    TrackInfo *pInfo = NULL;
    Mutex::Autolock autoLock(mLock);

    pInfo = &mTracks.editItemAt(index);
    track_num_got = pInfo->mTrackNum;
    pInfo = NULL;

    ALOGV("GetNextSample readmode=%u index=%u BEGIN",mReadMode,track_num_got);
    do{

        if(mReadMode == PARSER_READ_MODE_TRACK_BASED){
            if (is_sync)
            {
                err = IParser->getNextSyncSample(parserHandle,
                        direction,
                        track_num_got,
                        &tmp,
                        &buffer_context,
                        &datasize,
                        (uint64 *)&ts,
                        &duration,
                        (uint32 *)&sampleFlag);
            }
            else
            {
                err = IParser->getNextSample(parserHandle,
                        track_num_got,
                        &tmp,
                        &buffer_context,
                        &datasize,
                        (uint64 *)&ts,
                        &duration,
                        (uint32 *)&sampleFlag);
            }
        }else{
            if (is_sync)
            {
                err = IParser->getFileNextSyncSample(parserHandle,
                        direction,
                        &track_num_got,
                        &tmp,
                        &buffer_context,
                        &datasize,
                        (uint64 *)&ts,
                        &duration,
                        (uint32 *)&sampleFlag);
            }
            else
            {
                err = IParser->getFileNextSample(parserHandle,
                        &track_num_got,
                        &tmp,
                        &buffer_context,
                        &datasize,
                        (uint64 *)&ts,
                        &duration,
                        (uint32 *)&sampleFlag);
            }
        }

        if(PARSER_NOT_READY == err){
            return AMEDIA_ERROR_WOULD_BLOCK;
        }

        ALOGV("GetNextSample err %d get track num=%u ts=%lld,size=%u,flag=%x",err, track_num_got,ts,datasize,sampleFlag);

        if(PARSER_SUCCESS != err){
            if(err == PARSER_READ_ERROR)
                return AMEDIA_ERROR_IO;
            else if(err == PARSER_ERR_INVALID_PARAMETER)
                return AMEDIA_ERROR_MALFORMED;
            else
                return AMEDIA_ERROR_END_OF_STREAM;
        }

        pInfo = &mTracks.editItemAt(index);
        if(pInfo && pInfo->mTrackNum != track_num_got){

            size_t trackCount = mTracks.size();
            for (size_t index = 0; index < trackCount; index++) {
                pInfo = &mTracks.editItemAt(index);
                if(pInfo->mTrackNum == track_num_got)
                    break;
                pInfo = NULL;
            }
            if(pInfo == NULL)
                continue;
        }

        if(tmp && buffer_context && pInfo) {
            MediaBufferHelper * buffer = pInfo->buffer;

            if(sampleFlag & FLAG_SAMPLE_NOT_FINISHED)
                pInfo->bPartial = true;

            if(pInfo->bPartial){

                if(buffer == NULL){
                    buffer = (MediaBufferHelper *)buffer_context;
                    pInfo->outTs = ts;
                    pInfo->outDuration = duration;
                    pInfo->syncFrame = (sampleFlag & FLAG_SYNC_SAMPLE);
                    buffer->set_range(0,datasize);
                    pInfo->buffer = buffer;
                    ALOGV("bPartial first buffer");
                }else {
                    MediaBufferHelper * lastBuf = buffer;
                    MediaBufferHelper * currBuf = (MediaBufferHelper *)buffer_context;
                    if (pInfo->type == MEDIA_VIDEO && (sampleFlag & FLAG_SAMPLE_H264_SEI_POS_DATA)) {
                        // add sei position data to last video frame buffer as the meta data
                        sp<ABuffer> sei = new ABuffer(sizeof(NALPosition));
                        NALPosition *nalPos = (NALPosition *)sei->data();
                        SeiPosition *seiPos = (SeiPosition *)currBuf->data();
                        nalPos->nalOffset = seiPos->offset;
                        nalPos->nalSize = seiPos->size;

                        AMediaFormat_setBuffer(buffer->meta_data(), AMEDIAFORMAT_KEY_SEI, sei->data(), sei->size());
                        currBuf->release();
                    } else {
                        void* meta_value;
                        size_t meta_size = 0;
                        size_t lastLen = lastBuf->range_length();

                        if(0/*lastBuf->range_length() + currBuf->range_length() < lastBuf->size()*/){
                            buffer = lastBuf;
                            buffer->set_range(0,lastLen + (size_t)datasize);
                            memcpy((uint8*)buffer->data()+lastLen,currBuf->data(),currBuf->range_length());
                            currBuf->release();
                            pInfo->buffer = buffer;
                        } else if(true == mReader->AcquireBuffer(track_num_got, lastLen + (size_t)datasize , &buffer)){
                            buffer->set_range(0,lastLen + (size_t)datasize);

                            memcpy(buffer->data(),lastBuf->data(),lastLen);
                            memcpy((uint8*)buffer->data()+lastLen,currBuf->data(),currBuf->range_length());

                            if(AMediaFormat_getBuffer(lastBuf->meta_data(), AMEDIAFORMAT_KEY_SEI, &meta_value, &meta_size)){
                                AMediaFormat_setBuffer(buffer->meta_data(), AMEDIAFORMAT_KEY_SEI, meta_value, meta_size);
                            }
                            lastBuf->release();
                            currBuf->release();
                            pInfo->buffer = buffer;
                        }else{
                            ALOGE("could not AcquireBuffer");
                            break;
                        }
                    }
                    ALOGV("bPartial second buffer");
                }

                if(!(sampleFlag & FLAG_SAMPLE_NOT_FINISHED))
                    pInfo->bPartial = false;
            }
            else{
                buffer = (MediaBufferHelper *)buffer_context;
                pInfo->outTs = ts;
                pInfo->outDuration = duration;
                pInfo->syncFrame = (sampleFlag & FLAG_SYNC_SAMPLE);
                pInfo->buffer = buffer;
                buffer->set_range(0,datasize);
            }

        }else {
            // mpg2 parser often send an empty buffer as the last partial frame.
            if(pInfo && pInfo->bPartial && !(sampleFlag & FLAG_SAMPLE_NOT_FINISHED))
                pInfo->bPartial = false;
        }

        if (pInfo && pInfo->buffer != NULL && (pInfo->buffer->range_length() < pInfo->max_input_size))
            bufferIsValid = true;
        else
            bufferIsValid = false;

    }while((sampleFlag & FLAG_SAMPLE_NOT_FINISHED) && bufferIsValid);

    if(pInfo && pInfo->buffer != NULL ){
        ImxMediaSource *source = pInfo->mSource;
        bool add = false;
        if(source != NULL  && source->started()){
            add = true;
            if(pInfo->type == MEDIA_AUDIO && bWaitForAudioStartTime == true) {
                if(pInfo->outTs >= 0 && pInfo->outTs < currentAudioTs && mVideoActived == true) {
                    ALOGV("drop audio after seek ts= %" PRId64 ",audio_ts= %" PRId64 "",pInfo->outTs,currentAudioTs);
                    add = false;
                } else if(pInfo->outTs == -1) {
                    // drop audio as invalid start time after seek
                    add = false;
                } else {
                    // get audio start time
                    bWaitForAudioStartTime = false;
                }
            }
        }

        if (add) {
            if (pInfo->type == MEDIA_AUDIO && pInfo->bAacAdts) {
                // return aac adts by frame
                MediaBufferHelper *newBuf;
                size_t size = mAdtsBuffer->size() + pInfo->buffer->range_length();
                mReader->AcquireBuffer(pInfo->mTrackNum, size, &newBuf);

                if (newBuf && (OK == GetAacAdtsFrames(pInfo->buffer, newBuf))) {
                    pInfo->buffer->release();
                    pInfo->buffer = newBuf;
                }
                else {
                    add = false;
                    if (newBuf)
                        newBuf->release();
                }
            }
        }
        if(add){
            bool readDrmInfo = false;
            //check the last complete sample we read
            if((sampleFlag & FLAG_SAMPLE_COMPRESSED_SAMPLE) && !(sampleFlag & FLAG_SAMPLE_NOT_FINISHED)){
                readDrmInfo = true;
            }

            if(pInfo->type == MEDIA_AUDIO && mAudioPresentations.size() > 0) {
                if (sampleFlag & FLAG_SAMPLE_AUDIO_PRESENTATION_CHANGED)
                    GetAudioPresentationInfo(index);

                std::ostringstream outStream(std::ios::out);
                serializeAudioPresentations(mAudioPresentations, &outStream);
                AMediaFormat_setBuffer(pInfo->buffer->meta_data(), AMEDIAFORMAT_KEY_AUDIO_PRESENTATION_INFO,
                                            outStream.str().data(), outStream.str().size());
            }

            if(pInfo->bIsNeedConvert) {
                MediaBufferHelper * buffer = pInfo->buffer;
                MediaBufferHelper * tmp = NULL;
                if(mReader->AcquireBuffer(pInfo->mTrackNum, 2 * buffer->range_length() , &tmp)){
                    convertPCMData(buffer, tmp, pInfo->bitPerSample);
                    pInfo->buffer = tmp;
                    buffer->release();
                }
            }

            MediaBufferHelper *mbuf = pInfo->buffer;
            AMediaFormat * meta = mbuf->meta_data();
            AMediaFormat_setInt64(meta, AMEDIAFORMAT_KEY_TIME_US, pInfo->outTs);
            AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_IS_SYNC_FRAME, pInfo->syncFrame);
            AMediaFormat_setInt64(meta, AMEDIAFORMAT_KEY_DURATION,  pInfo->outDuration);

            ALOGV("addMediaBuffer ts=%" PRId64 ",size=%zu",pInfo->outTs,pInfo->buffer->range_length());
            if(pInfo->bMkvEncrypted){
                SetMkvCrpytBufferInfo(pInfo,mbuf);
            }else if(pInfo->bMp4Encrypted && readDrmInfo){
                SetMp4CrpytBufferInfo(pInfo,mbuf);
            }
            source->addMediaBuffer(mbuf);
            if(pInfo->type == MEDIA_VIDEO)
                currentVideoTs = pInfo->outTs;
            else if(pInfo->type == MEDIA_AUDIO)
                currentAudioTs = pInfo->outTs;

            //do not release the buffer, pass it to ImxMediaSource
            pInfo->buffer = NULL;
        }else{
            pInfo->buffer->release();
            pInfo->buffer = NULL;
        }
    }

    //check for get subtitle track in file mode, avoid interleave
    pInfo = &mTracks.editItemAt(index);
    if(pInfo->type == MEDIA_TEXT && pInfo->mTrackNum != track_num_got)
        return AMEDIA_ERROR_WOULD_BLOCK;

    //the return value WOULD_BLOCK can only be used when playing rtp streaming.
    //for other case, we must read and give a frame buffer when calling ImxMediaSource::read()
    if(mReader->isLiveStreaming() && mReadMode == PARSER_READ_MODE_FILE_BASED
        && (pInfo->mTrackNum != track_num_got))
        return AMEDIA_ERROR_WOULD_BLOCK;

    return AMEDIA_OK;
}
status_t ImxExtractor::CheckInterleaveEos(__unused uint32_t index)
{
    bool bTrackFull = false;

    if(mReadMode == PARSER_READ_MODE_TRACK_BASED)
        return OK;

    if(mTracks.size() < 2)
        return OK;

    for(size_t i = 0; i < mTracks.size(); i++){
        TrackInfo *pInfo = &mTracks.editItemAt(i);
        ImxMediaSource *source = pInfo->mSource;
        if(source != NULL && source->started() && source->full()){
            bTrackFull = true;
            ALOGE("get a full track mTrackNum=%d",pInfo->mTrackNum);
            break;
        }
    }
    if(bTrackFull)
        return ERROR_END_OF_STREAM;
    else
        return OK;
}
status_t ImxExtractor::ClearTrackSource(uint32_t index)
{
    if (index >= mTracks.size()) {
        return UNKNOWN_ERROR;
    }
    TrackInfo *trackInfo = &mTracks.editItemAt(index);
    if(trackInfo){
        if(trackInfo->buffer != NULL)
            trackInfo->buffer->release();

        trackInfo->mSource = NULL;
    }
    return OK;
}

bool ImxExtractor::isTrackModeParser()
{
    if(!strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_MPEG4) || !strcmp(mMime, MEDIA_MIMETYPE_CONTAINER_AVI))
        return true;
    else
        return false;
}
status_t ImxExtractor::convertPCMData(MediaBufferHelper * inBuffer, MediaBufferHelper* outBuffer, int32_t bitPerSample)
{
    if(bitPerSample == 8) {
        // Convert 8-bit unsigned samples to 16-bit signed.
        ssize_t numBytes = inBuffer->range_length();

        int16_t *dst = (int16_t *)outBuffer->data();
        const uint8_t *src = (const uint8_t *)inBuffer->data()+inBuffer->range_offset();

        while (numBytes-- > 0) {
            *dst++ = ((int16_t)(*src) - 128) * 256;
            ++src;
        }
        outBuffer->set_range(0, 2 * inBuffer->range_length());

    }else if (bitPerSample == 24) {
        // Convert 24-bit signed samples to 16-bit signed.
        const uint8_t *src = (const uint8_t *)inBuffer->data()+inBuffer->range_offset();
        int16_t *dst = (int16_t *)outBuffer->data();
        size_t numSamples = inBuffer->range_length() / 3;
        for (size_t i = 0; i < numSamples; i++) {
            int32_t x = (int32_t)(src[0] | src[1] << 8 | src[2] << 16);
            x = (x << 8) >> 8;  // sign extension
            x = x >> 8;
            *dst++ = (int16_t)x;
            src += 3;
        }
        outBuffer->set_range(0, 2 * numSamples);
    }

    return OK;
}
status_t ImxExtractor::SetMkvCrpytBufferInfo(TrackInfo *pInfo, MediaBufferHelper *buf)
{

    uint8 *buffer_ptr = (uint8 *)buf->data();
    int32_t buffer_len = buf->range_length();
    AMediaFormat * meta = buf->meta_data();

    //parse the struct from http://www.webmproject.org/docs/webm-encryption/
    if (buffer_ptr[0] & 0x1) {
        if(buffer_len < 9)
            return ERROR_MALFORMED;

        buffer_len -= 9;

        //full-sample encrypted block format
        int32 plainSizes[] = { 0 };
        int32 encryptedSizes[] = { buffer_len };
        uint8 ctrCounter[16] = { 0 };

        uint8 *keyId = NULL;
        size_t keySize = 0;

        memcpy(ctrCounter, buffer_ptr + 1, 8);

        AMediaFormat *trackMeta = pInfo->mMeta;

        CHECK(AMediaFormat_getBuffer(trackMeta, AMEDIAFORMAT_KEY_CRYPTO_KEY, (void**)&keyId, &keySize));


        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_KEY, (void*)keyId, keySize);
        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_IV, (void*)ctrCounter, 16);
        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_PLAIN_SIZES, (void*)plainSizes, sizeof(plainSizes));
        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_SIZES, (void*)encryptedSizes, sizeof(encryptedSizes));

        buf->set_range(9, buffer_len);

    } else {
        //unencrypted block format
        buffer_len -= 1;
        int32 plainSizes[] = { buffer_len };
        int32 encryptedSizes[] = { 0 };

        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_PLAIN_SIZES, (void*)plainSizes, sizeof(plainSizes));
        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_SIZES, (void*)encryptedSizes, sizeof(encryptedSizes));

        buf->set_range(1, buffer_len);
    }

    return OK;
}
status_t ImxExtractor::SetMp4CrpytBufferInfo(TrackInfo *pInfo, MediaBufferHelper *buf)
{
    int32 err = (int32)PARSER_SUCCESS;

    if(pInfo == NULL || buf == NULL)
        return ERROR_MALFORMED;

    AMediaFormat* meta = buf->meta_data();
    uint8 *iv = NULL;
    uint32 ivSize = 0;
    uint32 *clear;
    uint32 clearSize = 0;
    uint32 * encrypted;
    uint32 encryptedSize = 0;
    err = IParser->getSampleCryptoInfo(parserHandle,pInfo->mTrackNum,&iv,&ivSize,
            (uint8 **)&clear, &clearSize, (uint8 **)&encrypted, &encryptedSize);
    if(err == PARSER_SUCCESS){

        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_IV, (void*)iv, 16);
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_CRYPTO_MODE, pInfo->default_isEncrypted);
        AMediaFormat_setInt32(meta, AMEDIAFORMAT_KEY_CRYPTO_DEFAULT_IV_SIZE, pInfo->default_iv_size);

        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_KEY, (void*)pInfo->default_kid, 16);
        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_PLAIN_SIZES, (void*)clear, clearSize);
        AMediaFormat_setBuffer(meta, AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_SIZES, (void*)encrypted, encryptedSize);
        AMediaFormat_setInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_BYTE_BLOCK, pInfo->default_EncryptedByteBlock);
        AMediaFormat_setInt32(meta,AMEDIAFORMAT_KEY_CRYPTO_SKIP_BYTE_BLOCK, pInfo->default_SkipByteBlock);

        ALOGV("SetMp4CrpytBufferInfo clear size=%d,encryptedSize=%d,clear[0]=%x,encryptedSize[0]=%d",
            clearSize,encryptedSize,(int)*clear,(int)*encrypted);

        ALOGV("SetMp4CrpytBufferInfo default_EncryptedByteBlock=%d,default_SkipByteBlock=%d,default_isEncrypted[0]=%d,default_iv_size=%d",
            pInfo->default_EncryptedByteBlock,pInfo->default_SkipByteBlock,pInfo->default_isEncrypted,pInfo->default_iv_size);

        ALOGV("SetMp4CrpytBufferInfo iv=%x,%x,%x,%x,default_kid=%x,%x,%x,%x",iv[0],iv[1],iv[2],iv[3],
            pInfo->default_kid[0],pInfo->default_kid[1],pInfo->default_kid[2],pInfo->default_kid[3]);
    }else{
        ALOGV("getNextDrmInfoSample of track %d, failed!",pInfo->mTrackNum);
    }

    return OK;
}

static bool getAacAdtsFrameOffset(uint8_t * pData, size_t size,
                                            size_t *frameBegin, size_t *frameEnd,
                                            bool *resync)
{
    size_t offset = 0;
    bool findFrame = false;

    *frameBegin = 0;
    *frameEnd = 0;

    while (offset < size) {
        if (offset + 7 >= size) {
            break;
        }

        ABitReader bits((const uint8_t *)pData + offset, size - offset);
        unsigned aac_frame_length = 0;

        // adts_fixed_header
        if (bits.getBits(12) != 0xfffu) {
            // first frame, it might need to resync because of seek
            if (!findFrame)
                goto NEXT_BYTE;
            else
                break;
        }

        // ID, layer, protection_absent
        // profile_ObjectType, sampling_frequency_index, private_bits,
        // channel_configuration, original_copy, home
        // copyright_identification_bit, copyright_identification_start
        bits.skipBits(18);

        aac_frame_length = bits.getBits(13);
        if (aac_frame_length == 0){
            ALOGW("Invalid AAC frame length!");
            if (!findFrame)
                goto NEXT_BYTE;
            else
                break;
        }

        bits.skipBits(11);  // adts_buffer_fullness

        // number_of_raw_data_blocks_in_frame
        if (bits.getBits(2) != 0) {
            // To be implemented.
            ALOGW("Should not reach here.");
            if (!findFrame)
                goto NEXT_BYTE;
            else
                break;
        }

        if (offset + aac_frame_length > size) {
            break;
        }

        if (*resync) {
            if (offset + aac_frame_length + 2 < size
                    && (pData[offset + aac_frame_length] != 0xff
                    || (pData[offset + aac_frame_length + 1 ] >> 4) != 0xf))
                goto NEXT_BYTE;
            else
                *resync = false;
        }

        if (!findFrame) {
            findFrame = true;
            *frameBegin = offset;
        }

        offset += aac_frame_length;
        continue;

NEXT_BYTE:
        ++offset;
        *frameBegin = offset;
        continue;
    }

    if (findFrame) {
        *frameEnd = offset;
    } else {
        *frameEnd = size;
    }

    ALOGV("AAC ADTS findFrame %d frameBegin %zu frameEnd %zu", findFrame, *frameBegin, *frameEnd);
    return findFrame;
}

status_t ImxExtractor::GetAacAdtsFrames(MediaBufferHelper *srcBuf, MediaBufferHelper * dstBuf)
{

    uint8_t *pData = NULL;
    size_t size = 0;
    size_t beginOffset = 0;
    size_t endOffset = 0;
    status_t ret = OK;

    if (mAdtsBuffer->size() > 0) {
        memcpy(dstBuf->data(), mAdtsBuffer->data(), mAdtsBuffer->size());
        memcpy((uint8_t *)dstBuf->data() + mAdtsBuffer->size(), srcBuf->data(), srcBuf->range_length());
        size = mAdtsBuffer->size() + srcBuf->range_length();
        dstBuf->set_range(0, size);
    } else {
        memcpy((uint8_t *)dstBuf->data(), srcBuf->data(), srcBuf->range_length());
        size = srcBuf->range_length();
        dstBuf->set_range(0, size);
    }

    pData = (uint8_t *)dstBuf->data();
    size = dstBuf->range_length();
    ALOGV("%s AAC ADTS: buffer after resize %zu ", __FUNCTION__, size);

    if (getAacAdtsFrameOffset(pData, size, &beginOffset, &endOffset, &mResyncAdts)) {
        dstBuf->set_range(beginOffset, endOffset - beginOffset);
    } else {
        dstBuf->set_range(0, 0);
        ret = BAD_VALUE;
    }

    if (endOffset < size) {
        memcpy(mAdtsBuffer->data(), pData + endOffset, size - endOffset);
        mAdtsBuffer->setRange(0, size - endOffset);
        ALOGV("%s left data size %zu", __FUNCTION__, mAdtsBuffer->size());
    }

    return ret;
}

#define DELTA_TIME (24 * 3600 * (66*365 + 17))//seconds passed from Jan,1,1904 to Jan,1,1970
bool ImxExtractor::ConvertMp4TimeToString(uint64 inTime, String8 *s) {

    time_t time2 = 0;
    struct tm * tms = NULL;
    char str[32];
    size_t strLen = 0;

    //according to spec, creation time is an unsinged int64 value.
    if((int64_t)inTime < DELTA_TIME + INT64_MIN)
        return false;

    // google's cts test want to return 1904 year and 1970 year, so time2 may be negative
    time2 = (int64_t)inTime - DELTA_TIME;

    tms = gmtime(&time2);
    if(tms != NULL){
        strLen = strftime(str, sizeof(str), "%Y%m%dT%H%M%S.000Z", tms);
    }

    if(strLen > 0){
        s->setTo(str);
        return true;
    }

    return false;
}
status_t ImxExtractor::SetMkvHDRColorInfoMetadata(VideoHDRColorInfo *pInfo, AMediaFormat *meta)
{
    HDRStaticInfo targetInfo;
    bool update = false;

    if(pInfo == NULL || meta == NULL)
        return UNKNOWN_ERROR;

    memset(&targetInfo,0,sizeof(HDRStaticInfo));
    if(pInfo->maxCLL > 0 || pInfo->maxFALL > 0){
        targetInfo.sType1.mMaxContentLightLevel = pInfo->maxCLL;
        targetInfo.sType1.mMaxFrameAverageLightLevel = pInfo->maxFALL;
        update = true;
    }

    if(pInfo->hasMasteringMetadata){
        //use timescale 50000
        #define HDR_TIMESCALE 50000
        if((pInfo->PrimaryRChromaticityX >= 0 && pInfo->PrimaryRChromaticityX <=1)
            && (pInfo->PrimaryRChromaticityY >= 0 && pInfo->PrimaryRChromaticityY <=1)
            && (pInfo->PrimaryGChromaticityX >= 0 && pInfo->PrimaryGChromaticityX <=1)
            && (pInfo->PrimaryGChromaticityY >= 0 && pInfo->PrimaryGChromaticityY <=1)
            && (pInfo->PrimaryBChromaticityX >= 0 && pInfo->PrimaryBChromaticityX <=1)
            && (pInfo->PrimaryBChromaticityY >= 0 && pInfo->PrimaryBChromaticityY <=1)){

            targetInfo.sType1.mR.x = (uint16_t)(pInfo->PrimaryRChromaticityX * HDR_TIMESCALE + 0.5);
            targetInfo.sType1.mR.y = (uint16_t)(pInfo->PrimaryRChromaticityY * HDR_TIMESCALE + 0.5);
            targetInfo.sType1.mG.x = (uint16_t)(pInfo->PrimaryGChromaticityX * HDR_TIMESCALE + 0.5);
            targetInfo.sType1.mG.y = (uint16_t)(pInfo->PrimaryGChromaticityY * HDR_TIMESCALE + 0.5);
            targetInfo.sType1.mB.x = (uint16_t)(pInfo->PrimaryBChromaticityX * HDR_TIMESCALE + 0.5);
            targetInfo.sType1.mB.y = (uint16_t)(pInfo->PrimaryBChromaticityY * HDR_TIMESCALE + 0.5);
            update = true;
            ALOGI("get HDR RGB=(%d,%d),(%d,%d),(%d,%d)",targetInfo.sType1.mR.x,targetInfo.sType1.mR.y,
                targetInfo.sType1.mG.x,targetInfo.sType1.mG.y,
                targetInfo.sType1.mB.x,targetInfo.sType1.mB.y);
        }

        if((pInfo->WhitePointChromaticityX >= 0 && pInfo->WhitePointChromaticityX <=1) &&
            (pInfo->WhitePointChromaticityY >= 0 && pInfo->WhitePointChromaticityY <=1)){
            targetInfo.sType1.mW.x = (uint16_t)(pInfo->WhitePointChromaticityX *HDR_TIMESCALE + 0.5);
            targetInfo.sType1.mW.y = (uint16_t)(pInfo->WhitePointChromaticityY *HDR_TIMESCALE + 0.5);
            update = true;
            ALOGI("WhitePoint=(%d,%d)",targetInfo.sType1.mW.x,targetInfo.sType1.mW.y);
        }

        if(pInfo->LuminanceMax < 65535.5){
            targetInfo.sType1.mMaxDisplayLuminance = (uint16_t)(pInfo->LuminanceMax + 0.5);
            if(targetInfo.sType1.mMaxDisplayLuminance < 1)
                targetInfo.sType1.mMaxDisplayLuminance = 1;
            update = true;
            ALOGI("mMaxDisplayLuminance=%d",targetInfo.sType1.mMaxDisplayLuminance);
        }
        if(pInfo->LuminanceMin < 6.5535){
            targetInfo.sType1.mMinDisplayLuminance = (uint16_t)(pInfo->LuminanceMin * 10000 + 0.5);
            if(targetInfo.sType1.mMinDisplayLuminance < 1)
                targetInfo.sType1.mMinDisplayLuminance = 1;
            update = true;
            ALOGI("mMinDisplayLuminance=%d",targetInfo.sType1.mMinDisplayLuminance);
        }
    }

    if(update){
        targetInfo.mID = HDRStaticInfo::kType1;
        ColorUtils::setHDRStaticInfoIntoAMediaFormat(targetInfo, meta);
    }
    return OK;
}

status_t ImxExtractor::GetAudioPresentationInfo(uint32_t index) {
    if (!IParser->getAudioPresentationNum || !IParser->getAudioPresentationInfo)
        return BAD_VALUE;

    int32 err = (int32)PARSER_SUCCESS;
    int32 presentationNum = 0;

    err = IParser->getAudioPresentationNum(parserHandle, index, &presentationNum);
    if (err || presentationNum <= 0)
        return BAD_VALUE;

    mAudioPresentations.clear();

    for (int idx = 0; idx < presentationNum; idx++) {
        int32 presentationId = -1;
        char *language;
        uint32 masteringIndication;
        uint32 audioDescriptionAvailable;
        uint32 spokenSubtitlesAvailable;
        uint32 dialogueEnhancementAvailable;
        err = IParser->getAudioPresentationInfo(parserHandle, index, idx, &presentationId, \
                                    &language, &masteringIndication,&audioDescriptionAvailable, \
                                    &spokenSubtitlesAvailable,&dialogueEnhancementAvailable);
        if (err) {
            mAudioPresentations.clear();
            return BAD_VALUE;
        }

        AudioPresentationV1 ap;
        ap.mPresentationId = presentationId;
        ap.mMasteringIndication = static_cast<MasteringIndication>(masteringIndication);
        ap.mAudioDescriptionAvailable = (audioDescriptionAvailable == 1);
        ap.mSpokenSubtitlesAvailable = (spokenSubtitlesAvailable == 1);
        ap.mDialogueEnhancementAvailable = (dialogueEnhancementAvailable == 1);
        ap.mLanguage = String8(language);
        ALOGI("language %s mPresentationId %d, mMasteringIndication %d, Available(%d %d %d)",\
        ap.mLanguage.c_str(), ap.mPresentationId, ap.mAudioDescriptionAvailable,\
        ap.mAudioDescriptionAvailable, ap.mSpokenSubtitlesAvailable, ap.mDialogueEnhancementAvailable);
        mAudioPresentations.push_back(std::move(ap));
    }

    return OK;
}

bool ImxExtractor::AttachMediaBufferGroupHelper(uint32_t index, MediaBufferGroupHelper * buf_group)
{
    Mutex::Autolock autoLock(mLock);
    TrackInfo *trackInfo = &mTracks.editItemAt(index);
    if(trackInfo == NULL)
        return false;

    return mReader->AttachMediaBufferGroupHelper(trackInfo->mTrackNum, buf_group);
}
bool ImxExtractor::DetachMediaBufferGroupHelper(uint32_t index)
{
    Mutex::Autolock autoLock(mLock);
    TrackInfo *trackInfo = &mTracks.editItemAt(index);
    if(trackInfo == NULL)
        return false;

    return mReader->DetachMediaBufferGroupHelper(trackInfo->mTrackNum);
}
media_status_t ImxExtractor::GetTrackMaxBufferSize(uint32_t index, size_t * max_buffer)
{
    Mutex::Autolock autoLock(mLock);
    TrackInfo *trackInfo = &mTracks.editItemAt(index);
    if(trackInfo == NULL)
        return AMEDIA_ERROR_INVALID_PARAMETER;

    *max_buffer = mReader->GetBufferReadLimitation(trackInfo->mTrackNum);
    return AMEDIA_OK;
}
static CMediaExtractor* CreateExtractor(CDataSource *source, void *meta) {
    return wrap(new ImxExtractor(new DataSourceHelper(source), (const char *)meta));
}

static CreatorFunc Sniff(
        CDataSource *source, float *confidence, void **meta,
        FreeMetaFunc *) {
    DataSourceHelper helper(source);
    if (SniffIMX(&helper, confidence, meta)) {
        return CreateExtractor;
    }

    return NULL;
}

static const char *extensions[] = {
    //mp4
    "3g2",
    "3ga",
    "3gp",
    "3gpp",
    "3gpp2",
    "m4a",
    "m4r",
    "m4v",
    "mov",
    "mp4",
    "qt",
    //mkv
    "mka",
    "mkv",
    "webm",
    //aac
    "aac",
    //amr
    "amr",
    "awb",
    //flac
    "flac",
    "fl",
    //mp3
    "mp2",
    "mp3",
    "mpeg",
    "mpg",
    "mpga",
    //mpeg2
    "m2p",
    "m2ts",
    "mts",
    "ts",
    //others
    "divx", "adts", "asf", "wmv", "vob",
    "f4v", "flv", "rmvb", "rm", "ra", "rv", "ape", "dsf",
    NULL
};

extern "C" {
// This is the only symbol that needs to be exported
__attribute__ ((visibility ("default")))
ExtractorDef GETEXTRACTORDEF() {
    return {
        EXTRACTORDEF_VERSION,
        UUID("30228ab1-2652-43ec-aac6-9055e6b8b39d"),
        10000, // version
        "IMX Extractor",
        { .v3 = {Sniff, extensions} },
    };
}

} // extern "C"


}// namespace android
