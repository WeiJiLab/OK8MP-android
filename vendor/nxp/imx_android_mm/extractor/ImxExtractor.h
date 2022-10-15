/**
 *  Copyright 2016 Freescale Semiconductor, Inc.
 *  Copyright 2017-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */
#ifndef IMX_EXTRACTOR_H_

#define IMX_EXTRACTOR_H_

#include <media/stagefright/foundation/AudioPresentationInfo.h>
#include <media/MediaExtractorPluginApi.h>
#include <media/MediaExtractorPluginHelper.h>
#include <media/NdkMediaFormat.h>
#include <utils/KeyedVector.h>
#include <utils/List.h>
#include <utils/String8.h>
#include <utils/Vector.h>
#include <utils/Mutex.h>

#undef bool
#define bool bool

#include "fsl_media_types.h"
#include "fsl_parser.h"

namespace android {

struct AMessage;
struct ABuffer;

class DataSource;
class String8;
struct ImxMediaSource;
struct ImxDataSourceReader;
class MediaBuffer;


typedef struct
{
    /* creation & deletion */
    FslParserVersionInfo                getVersionInfo;
    FslCreateParser                     createParser;
    FslDeleteParser                     deleteParser;
    FslCreateParser2                    createParser2;

    /* index export/import */
    FslParserInitializeIndex            initializeIndex;
    FslParserImportIndex                importIndex;
    FslParserExportIndex                exportIndex;

    /* movie properties */
    FslParserIsSeekable                 isSeekable;
    FslParserGetMovieDuration           getMovieDuration;
    FslParserGetUserData                getUserData;
    FslParserGetMetaData                getMetaData;

    FslParserGetNumTracks               getNumTracks;

    FslParserGetNumPrograms             getNumPrograms;
    FslParserGetProgramTracks           getProgramTracks;

    /* generic track properties */
    FslParserGetTrackType               getTrackType;
    FslParserGetTrackDuration           getTrackDuration;
    FslParserGetLanguage                getLanguage;
    FslParserGetBitRate                 getBitRate;
    FslParserGetDecSpecificInfo         getDecoderSpecificInfo;
    FslParserGetTrackExtTag             getTrackExtTag;

    /* video properties */
    FslParserGetVideoFrameWidth         getVideoFrameWidth;
    FslParserGetVideoFrameHeight        getVideoFrameHeight;
    FslParserGetVideoFrameRate          getVideoFrameRate;
    FslParserGetVideoFrameRotation      getVideoFrameRotation;
    FslParserGetVideoColorInfo          getVideoColorInfo;
    FslParserGetVideoHDRColorInfo       getVideoHDRColorInfo;
    FslParserGetVideoDisplayWidth       getVideoDisplayWidth;
    FslParserGetVideoDisplayHeight      getVideoDisplayHeight;
    FslParserGetVideoFrameCount         getVideoFrameCount;
    FslParserGetVideoThumbnailTime      getVideoThumbnailTime;

    /* audio properties */
    FslParserGetAudioNumChannels        getAudioNumChannels;
    FslParserGetAudioSampleRate         getAudioSampleRate;
    FslParserGetAudioBitsPerSample      getAudioBitsPerSample;
    FslParserGetAudioBlockAlign         getAudioBlockAlign;
    FslParserGetAudioChannelMask        getAudioChannelMask;
    FslParserGetAudioBitsPerFrame       getAudioBitsPerFrame;

    /* text/subtitle properties */
    FslParserGetTextTrackWidth          getTextTrackWidth;
    FslParserGetTextTrackHeight         getTextTrackHeight;
    FslParserGetTextTrackMime           getTextTrackMime;

    /* sample reading, seek & trick mode */
    FslParserGetReadMode                getReadMode;
    FslParserSetReadMode                setReadMode;

    FslParserEnableTrack                enableTrack;

    FslParserGetNextSample              getNextSample;
    FslParserGetNextSyncSample          getNextSyncSample;

    FslParserGetFileNextSample              getFileNextSample;
    FslParserGetFileNextSyncSample          getFileNextSyncSample;
    FslParserGetSampleCryptoInfo       getSampleCryptoInfo;
    FslParserSeek                       seek;
    FslParserGetAudioPresentationNum    getAudioPresentationNum;
    FslParserGetAudioPresentationInfo   getAudioPresentationInfo;

}FslParserInterface;

class ImxExtractor : public MediaExtractorPluginHelper {
public:
    explicit ImxExtractor(DataSourceHelper *source, const char *mime = NULL);

    virtual size_t countTracks();
    virtual MediaTrackHelper *getTrack(size_t index);
    virtual media_status_t getTrackMetaData(AMediaFormat *meta,size_t index, uint32_t flags);

    virtual media_status_t getMetaData(AMediaFormat *meta);

    virtual uint32_t flags() const;
    virtual const char * name() { return "ImxExtractor"; }
    status_t Init();
    status_t ActiveTrack(uint32 index);
    status_t DisableTrack(uint32 index);
    media_status_t HandleSeekOperation(uint32_t index,int64_t * ts, uint32_t flag);
    media_status_t GetNextSample(uint32_t index,bool is_sync);
    status_t CheckInterleaveEos(uint32_t index);
    status_t ClearTrackSource(uint32_t index);
    bool AttachMediaBufferGroupHelper(uint32_t index, MediaBufferGroupHelper * buf_group);
    bool DetachMediaBufferGroupHelper(uint32_t index);
    media_status_t GetTrackMaxBufferSize(uint32_t index, size_t * max_buffer);

protected:
    virtual ~ImxExtractor();

private:
    DataSourceHelper *mDataSource;
    ImxDataSourceReader *mReader;
    char *mMime;
    bool bInit;
    char mLibName[255];
    void *mLibHandle;
    FslParserInterface * IParser;
    FslFileStream fileOps;
    ParserMemoryOps memOps;
    ParserOutputBufferOps outputBufferOps;

    uint32_t mReadMode;
    uint32_t mNumTracks;
    bool bSeekable;
    uint64_t mMovieDuration;
    struct TrackInfo {
        uint32_t mTrackNum;
        ImxMediaSource *mSource;
        AMediaFormat *mMeta;
        const ImxExtractor *mExtractor;
        bool bCodecInfoSent;

        bool bPartial;
        MediaBufferHelper * buffer;

        int64_t outTs = 0;
        int64_t outDuration = 0;
        int32_t syncFrame = 0;
        uint32_t max_input_size;
        uint32_t type;
        bool bIsNeedConvert;
        int32_t bitPerSample;
        bool bMkvEncrypted;
        bool bMp4Encrypted;
        //mp4 track crypto info
        int32_t default_isEncrypted;
        int32_t default_iv_size;
        uint8_t default_kid[16];
        int32_t default_EncryptedByteBlock;
        int32_t default_SkipByteBlock;
        bool bAacAdts;
    };
    Vector<TrackInfo> mTracks;

    AMediaFormat *mFileMetaData;

    FslParserHandle  parserHandle;

    Mutex mLock;
    int64_t currentVideoTs;
    int64_t currentAudioTs;
    uint32_t mVideoIndex;
    uint32_t mAudioIndex;
    bool mVideoActived;
    bool mAudioActived;
    bool bWaitForAudioStartTime;

    sp<ABuffer> mAdtsBuffer;
    bool mResyncAdts;

    AudioPresentationCollection mAudioPresentations;

    bool isLiveStreaming() const;
    status_t GetLibraryName();
    status_t CreateParserInterface();
    status_t ParseFromParser();
    status_t ParseMetaData();
    status_t ParseMediaFormat();
    status_t ParseVideo(uint32 index, uint32 type,uint32 subtype);
    status_t ParseAudio(uint32 index, uint32 type,uint32 subtype);
    status_t ParseText(uint32 index, uint32 type,uint32 subtype);
    status_t ParseTrackExtMetadata(uint32 index, AMediaFormat *meta);
    int bytesForSize(size_t size);
    void storeSize(uint8_t *data, size_t &idx, size_t size);
    void addESDSFromCodecPrivate(AMediaFormat *meta, bool isAudio, void *priv, size_t privSize);
    status_t addVorbisCodecInfo(AMediaFormat *meta, void *_codecPrivate, size_t codecPrivateSize);

    bool isTrackModeParser();
    status_t convertPCMData(MediaBufferHelper* inBuffer, MediaBufferHelper* outBuffer, int32_t bitPerSample);
    status_t SetMkvCrpytBufferInfo(TrackInfo *pInfo, MediaBufferHelper *mbuf);
    status_t SetMp4CrpytBufferInfo(TrackInfo *pInfo, MediaBufferHelper *mbuf);
    bool ConvertMp4TimeToString(uint64 inTime, String8 *s);
    status_t SetMkvHDRColorInfoMetadata(VideoHDRColorInfo *pInfo, AMediaFormat *meta);
    status_t GetAudioPresentationInfo(uint32_t index);
    status_t GetAacAdtsFrames(MediaBufferHelper *srcBuf, MediaBufferHelper * dstBuf);

    ImxExtractor(const ImxExtractor &);
    ImxExtractor &operator=(const ImxExtractor &);
};
}
#endif
