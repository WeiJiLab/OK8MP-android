/**
 *  Copyright 2018-2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "V4l2Dec"

#include "V4l2Dec.h"
#include <media/stagefright/MediaErrors.h>
#include <C2PlatformSupport.h>
#include "graphics_ext.h"
#include "Imx_ext.h"
#include "Memory.h"
#include "IonAllocator.h"
#include <sys/mman.h>
#include <media/stagefright/foundation/avc_utils.h>

namespace android {

#define VPU_DECODER_LOG_LEVELFILE "/data/vpu_dec_level"
#define DUMP_DEC_INPUT_FILE "/data/temp_dec_in.bit"
#define DUMP_DEC_OUTPUT_FILE "/data/temp_dec_out.yuv"

#define DUMP_DEC_FLAG_INPUT     0x1
#define DUMP_DEC_FLAG_OUTPUT    0x2


#define IMX_V4L2_BUF_FLAG_CODECCONFIG      0x00200000
#define IMX_V4L2_BUF_FLAG_TIMESTAMP_INVALID    0x00400000

#define Align(ptr,align)    (((uint32_t)(ptr)+(align)-1)/(align)*(align))
#define FRAME_ALIGN     (512)
#define DEFAULT_OUTPUT_BUFFER_COUNT 6

V4l2Dec::V4l2Dec(const char* mime):
    mMime(mime),
    mPollThread(0),
    mFetchThread(0),
    pDev(NULL),
    mFd(-1){

    bPollStarted = false;
    bPollStopped = false;
    bFetchStarted = false;
    bFetchStopped = false;

    bInputStreamOn = false;
    bOutputStreamOn = false;

    bCodecDataQueued = false;

    bNeedPostProcess = true;
    bMpeg2 = false;
    bH264 = false;

    mState = UNINITIALIZED;

    mVpuOwnedOutputBufferNum = 0;

    mInputFormat.bufferNum = kInputBufferCount;
    mInputFormat.bufferSize = kInputBufferSizeFor4k;
    mInputFormat.width = DEFAULT_FRM_WIDTH;
    mInputFormat.height = DEFAULT_FRM_HEIGHT;
    mInputFormat.interlaced = false;

    mOutputFormat.width = DEFAULT_FRM_WIDTH;
    mOutputFormat.height = DEFAULT_FRM_HEIGHT;
    mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_NV12_TILED;
    //use bufferNum to check if need fetch buffer, so default is 0
    mOutputFormat.minBufferNum = 0;
    mOutputFormat.bufferNum = 0;
    mOutputFormat.rect.left = 0;
    mOutputFormat.rect.top = 0;
    mOutputFormat.rect.right = mOutputFormat.width;
    mOutputFormat.rect.bottom = mOutputFormat.height;
    mOutputFormat.interlaced = false;

    mVc1Format = V4L2_PIX_FMT_VC1_ANNEX_G;
    mLastInputTs = -1;
    mLastInputId = 0;
}
V4l2Dec::~V4l2Dec()
{
}
status_t V4l2Dec::onInit(){
    status_t ret = UNKNOWN_ERROR;

    if(pDev == NULL){
        pDev = new V4l2Dev();
    }
    if(pDev == NULL)
        return ret;

    mFd = pDev->Open(V4L2_DEV_DECODER);
    ALOGV("pV4l2Dev->Open fd=%d",mFd);

    if(mFd < 0)
        return ret;

    mInMemType = V4L2_MEMORY_MMAP;
    mOutMemType = V4L2_MEMORY_DMABUF;//V4L2_MEMORY_USERPTR
    mState = UNINITIALIZED;
    mInCnt = 0;
    mOutCnt = 0;
    mLastInputTs = -1;
    mLastInputId = 0;
    bLowLatency = false;
    ParseVpuLogLevel();
    return OK;
}
status_t V4l2Dec::onStart()
{
    status_t ret = UNKNOWN_ERROR;
    ALOGV("onStart BEGIN");

    ret = prepareInputParams();
    if(ret != OK){
        ALOGE("prepareInputParams failed");
        return ret;
    }

    ret = SetInputFormats();
    if(ret != OK){
        ALOGE("SetInputFormats failed");
        return ret;
    }

    if(bH264 && bLowLatency){
        ALOGI("enable low latency decoder");
        ret = pDev->EnableLowLatencyDecoder(bLowLatency);
        if(ret != OK)
            ALOGE("EnableLowLatencyDecoder failed");
    }

    if(mInputBufferMap.empty() || (mInputFormat.bufferSize != mInputBufferMap[0].plane.size)){

        ret = prepareInputBuffers();
        if(ret != OK)
            return ret;

        if(mInMemType == V4L2_MEMORY_MMAP)
            ret = createInputBuffers();

        if(ret != OK){
            ALOGE("onStart createInputBuffers failed");
            return ret;
        }
    }

    ret = prepareOutputParams();
    if(ret != OK){
        ALOGD("prepareOutputParams not ok");
        mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_NV12_TILED;
        ret = prepareOutputParams();
        if(ret != OK){
            ALOGE("prepareOutputParams failed");
            return ret;
        }
    }

    ret = SetOutputFormats();
    if(ret != OK){
        ALOGE("SetOutputFormats failed");
        return ret;
    }

    ret = createPollThread();
    if(ret != OK)
        return ret;

    mState = RUNNING;

    if(mOutputFormat.bufferNum > 0)
        ret = createFetchThread();

    mInCnt = 0;
    mOutCnt = 0;
    mLastInputTs = -1;
    mLastInputId = 0;
    ALOGV("onStart ret=%d",ret);
    return ret;
}
status_t V4l2Dec::prepareInputParams()
{
    status_t ret = UNKNOWN_ERROR;
    Mutex::Autolock autoLock(mLock);

    //TODO: get mime from base class
    ret = pDev->GetStreamTypeByMime(mMime, &mInFormat);
    if(ret != OK){
        return ret;
    }

    // special for vc1 because vc1 has subtype
    if (strcmp(mMime, MEDIA_MIMETYPE_VIDEO_VC1) == 0) {
        mInFormat = mVc1Format;
    }

    if(!pDev->IsOutputFormatSupported(mInFormat)){
        ALOGE("input format not suppoted");
        return ret;
    }

    if (strcmp(mMime, MEDIA_MIMETYPE_VIDEO_MPEG2) == 0) {
        bMpeg2 = true;
    }else if(strcmp(mMime, MEDIA_MIMETYPE_VIDEO_AVC) == 0) {
        bH264 = true;
    }

    if(mInputFormat.bufferNum == 0){
        mInputFormat.bufferNum = 3;
    }

    #if 0
    struct v4l2_frmsizeenum info;
    memset(&info, 0, sizeof(v4l2_frmsizeenum));
    if(OK != pDev->GetFormatFrameInfo(mInFormat, &info)){
        ALOGE("GetFormatFrameInfo failed");
        return ret;
    }

    if(mInputFormat.width > info->max_width || mInputFormat.height > info->max_height)
        return UNKNOWN_ERROR;
    #endif
    return OK;
}
status_t V4l2Dec::SetInputFormats()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    format.fmt.pix_mp.num_planes = 1;
    format.fmt.pix_mp.pixelformat = mInFormat;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = mInputFormat.bufferSize;
    format.fmt.pix_mp.plane_fmt[0].bytesperline = Align(mInputFormat.width, FRAME_ALIGN);
    format.fmt.pix_mp.width = mInputFormat.width;
    format.fmt.pix_mp.height = mInputFormat.height;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;

    result = ioctl (mFd, VIDIOC_S_FMT, &format);
    if(result != 0)
        return UNKNOWN_ERROR;

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    result = ioctl (mFd, VIDIOC_G_FMT, &format);
    if(result != 0)
        return UNKNOWN_ERROR;

    if(format.fmt.pix_mp.pixelformat != mInFormat){
        ALOGE("SetInputFormats mInFormat mismatch");
        return UNKNOWN_ERROR;
    }

    if( format.fmt.pix_mp.width != mInputFormat.width ||
        format.fmt.pix_mp.height != mInputFormat.height){
        ALOGE("SetInputFormats resolution mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].bytesperline != Align(mInputFormat.width, FRAME_ALIGN)){
        ALOGE("SetInputFormats stride mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].sizeimage != mInputFormat.bufferSize){
        ALOGE("SetInputFormats bufferSize mismatch");
        return UNKNOWN_ERROR;
    }

    return OK;
}
status_t V4l2Dec::prepareOutputParams()
{
    status_t ret = UNKNOWN_ERROR;
    Mutex::Autolock autoLock(mLock);

    ret = pDev->GetV4l2FormatByColor(mOutputFormat.pixelFormat, &mOutFormat);
    if(ret != OK)
        return ret;

    if(!pDev->IsCaptureFormatSupported(mOutFormat)){
        ALOGE("output format not suppoted");
        return ret;
    }

    if(mOutFormat == V4L2_PIX_FMT_NV12 || mOutFormat == HAL_PIXEL_FORMAT_NV12_TILED){
        //update output frame width & height
        mOutputFormat.width = Align(mOutputFormat.rect.right, FRAME_ALIGN);
        mOutputFormat.height = Align(mOutputFormat.rect.bottom, FRAME_ALIGN);
        mOutputPlaneSize[0] = mOutputFormat.width * mOutputFormat.height;
        mOutputPlaneSize[1] = mOutputPlaneSize[0]/2;
    }else
        return UNKNOWN_ERROR;

    return OK;
}
status_t V4l2Dec::SetOutputFormats()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.num_planes = kOutputBufferPlaneNum;
    format.fmt.pix_mp.pixelformat = mOutFormat;

    mOutputFormat.width = Align(mOutputFormat.width, FRAME_ALIGN);
    mOutputFormat.height = Align(mOutputFormat.height, FRAME_ALIGN);

    format.fmt.pix_mp.width = mOutputFormat.width;
    format.fmt.pix_mp.height = mOutputFormat.height;
    ALOGV("SetOutputFormats w=%d,h=%d",format.fmt.pix_mp.width,format.fmt.pix_mp.height);
    format.fmt.pix_mp.plane_fmt[0].sizeimage = mOutputPlaneSize[0];
    format.fmt.pix_mp.plane_fmt[0].bytesperline = Align(mOutputFormat.width, FRAME_ALIGN);
    format.fmt.pix_mp.plane_fmt[1].sizeimage = mOutputPlaneSize[1];
    format.fmt.pix_mp.plane_fmt[1].bytesperline = Align(mOutputFormat.width, FRAME_ALIGN);
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;

    result = ioctl (mFd, VIDIOC_S_FMT, &format);
    if(result != 0){
        ALOGE("SetOutputFormats VIDIOC_S_FMT failed");
        return UNKNOWN_ERROR;
    }

    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    result = ioctl (mFd, VIDIOC_G_FMT, &format);
    if(result < 0)
        return UNKNOWN_ERROR;


    if(format.fmt.pix_mp.pixelformat != mOutFormat){
        ALOGE("SetOutputFormats mOutFormat mismatch");
        return UNKNOWN_ERROR;
    }

    if( format.fmt.pix_mp.width > mOutputFormat.width ||
        format.fmt.pix_mp.height > mOutputFormat.height){
        ALOGE("SetOutputFormats resolution mismatch,w=%d,h=%d,output w=%d,h=%d",
            format.fmt.pix_mp.width,format.fmt.pix_mp.height,mOutputFormat.width, mOutputFormat.height);
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].bytesperline != Align(mOutputFormat.width, FRAME_ALIGN)){
        ALOGE("SetOutputFormats stride mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].sizeimage !=  mOutputPlaneSize[0] ||
        format.fmt.pix_mp.plane_fmt[1].sizeimage !=  mOutputPlaneSize[1]){
        ALOGE("SetOutputFormats bufferSize mismatch");
        return UNKNOWN_ERROR;
    }

    ALOGV("SetOutputFormats success");
    return OK;
}

V4l2Dec::InputRecord::InputRecord()
    : at_device(false), input_id(-1), ts(-1) {
    memset(&plane, 0, sizeof(VideoFramePlane));
}

V4l2Dec::InputRecord::~InputRecord() {}

V4l2Dec::OutputRecord::OutputRecord()
    : at_device(false), picture_id(0), flag(0), mGraphicBlock(NULL) {
    memset(&planes, 0, sizeof(VideoFramePlane)*kOutputBufferPlaneNum);
}

V4l2Dec::OutputRecord::~OutputRecord() {
}

status_t V4l2Dec::prepareInputBuffers()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = mInputFormat.bufferNum;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = mInMemType;

    ALOGV("prepareInputBuffers count=%d",reqbufs.count);
    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        ALOGE("VIDIOC_REQBUFS failed result=%d",result);
        return UNKNOWN_ERROR;
    }

    mInputBufferMap.resize(reqbufs.count);

    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        mInputBufferMap[i].at_device = false;
        mInputBufferMap[i].plane.fd = -1;
        mInputBufferMap[i].plane.vaddr = 0;
        mInputBufferMap[i].plane.paddr = 0;
        mInputBufferMap[i].plane.size = mInputFormat.bufferSize;
        mInputBufferMap[i].plane.length = 0;
        mInputBufferMap[i].plane.offset = 0;
        mInputBufferMap[i].input_id = -1;
    }

    ALOGV("prepareInputBuffers total input=%d size=%d",mInputFormat.bufferNum, mInputBufferMap.size());

    return OK;
}
status_t V4l2Dec::createInputBuffers()
{

    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes;
    void * ptr = NULL;
    uint64_t tmp = 0;

    if(mInMemType != V4L2_MEMORY_MMAP)
        return UNKNOWN_ERROR;

    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&planes, 0, sizeof(planes));

    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        stV4lBuf.memory = V4L2_MEMORY_MMAP;
        stV4lBuf.index = i;
        stV4lBuf.length = kInputBufferPlaneNum;
        stV4lBuf.m.planes = &planes;
        result = ioctl(mFd, VIDIOC_QUERYBUF, &stV4lBuf);
        if(result < 0)
            return UNKNOWN_ERROR;

        planes.length = mInputFormat.bufferSize;

        ptr = mmap(NULL, planes.length,
            PROT_READ | PROT_WRITE, /* recommended */
            MAP_SHARED,             /* recommended */
            mFd, planes.m.mem_offset);

        if(ptr != MAP_FAILED){
            tmp = (uint64_t)ptr;
            mInputBufferMap[i].plane.vaddr = tmp;
        }else
            return NO_MEMORY;
    }

    ALOGV("createInputBuffers success");
    return OK;
}
status_t V4l2Dec::destroyInputBuffers()
{
    Mutex::Autolock autoLock(mLock);
    if (mInputBufferMap.empty())
        return OK;

    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        mInputBufferMap[i].at_device = false;
        mInputBufferMap[i].input_id = -1;
        mInputBufferMap[i].plane.fd = -1;
        if(mInMemType == V4L2_MEMORY_MMAP && mInputBufferMap[i].plane.vaddr != 0)
            munmap((void*)(uintptr_t)mInputBufferMap[i].plane.vaddr, mInputBufferMap[i].plane.size);

        mInputBufferMap[i].plane.vaddr = 0;
        mInputBufferMap[i].plane.paddr = 0;
        mInputBufferMap[i].plane.size = 0;
        mInputBufferMap[i].plane.length = 0;
        mInputBufferMap[i].plane.offset = 0;
    }

    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;//use mmap to free buffer

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        ALOGV("ignore the result");
    }
        //return UNKNOWN_ERROR;

    mInputBufferMap.clear();

    ALOGV("destroyInputBuffers success");
    return OK;
}
status_t V4l2Dec::importOutputBuffers(std::vector<GraphicBlockInfo> buffers)
{
#if 0
    uint32_t out_buf_count = buffers.size();

    if(out_buf_count != mOutputFormat.bufferNum){
        ALOGE("importOutputBuffers failed");
        return UNKNOWN_ERROR;
    }

    out_buf_count = 32; // add 4 + 3 buffers to align with CCodecBufferChannel
    mLock.lock();
    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = out_buf_count;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutMemType;

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    mOutputBufferMap.resize(out_buf_count);

    for (size_t i = 0; i < mOutputBufferMap.size(); i++) {
        OutputRecord& output_record = mOutputBufferMap[i];

        if (i < buffers.size()) {

        output_record.planes[0].fd = buffers[i].mDMABufFd;
        output_record.planes[0].vaddr = buffers[i].mVirtAddr;
        output_record.planes[0].paddr = buffers[i].mPhysAddr;
        output_record.planes[0].size = mOutputPlaneSize[0];
        output_record.planes[0].length = 0;
        output_record.planes[0].offset = 0;
        output_record.planes[1].fd = buffers[i].mDMABufFd;
        output_record.planes[1].vaddr = buffers[i].mVirtAddr + mOutputPlaneSize[0];
        output_record.planes[1].paddr = buffers[i].mPhysAddr + mOutputPlaneSize[0];
        output_record.planes[1].size = mOutputPlaneSize[1];
        output_record.planes[1].length = 0;
        output_record.planes[1].offset = mOutputPlaneSize[0];

        output_record.at_device = false;
        output_record.picture_id = buffers[i].mBlockId;
        } else {
            output_record.planes[0].fd = 0;
            output_record.planes[0].vaddr = 0;
            output_record.planes[0].paddr = 0;
            output_record.at_device = false;
            output_record.picture_id = -1;
        }
    }
#else
    if(mState == STOPPING)
        return OK;

    mState = RUNNING;
    mLock.lock();

    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 32;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutMemType;

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        mLock.unlock();
        return UNKNOWN_ERROR;
    }
    if (!bNeedPostProcess)
        mOutputBufferMap.resize(32);

    mLock.unlock();
#endif

    createFetchThread();
    return OK;
}
status_t V4l2Dec::destroyOutputBuffers()
{
    Mutex::Autolock autoLock(mLock);
    if (mOutputBufferMap.empty())
        return OK;

    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;//use mmap to free buffer

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        ALOGV("ignore VIDIOC_REQBUFS result");
        //return UNKNOWN_ERROR;
    }

    mOutputBufferMap.clear();
    ClearPictureBuffer();//call it here or in base class
    ALOGV("destroyOutputBuffers success");
    return OK;
}
status_t V4l2Dec::HandlePollThread()
{
    status_t ret = OK;
    int32_t poll_ret = 0;

    while(bPollStarted){
        ALOGV("pollThreadHandler BEGIN");
        poll_ret = pDev->Poll();
        ALOGV("pollThreadHandler poll_ret=%x,mInCnt=%d,mOutCnt=%d",
            poll_ret,mInCnt,mOutCnt);
        if(poll_ret & V4L2_DEV_POLL_EVENT){
            ret = onDequeueEvent();
        }

        if(poll_ret & V4L2_DEV_POLL_OUTPUT){
            ret = dequeueInputBuffer();
        }
        if(poll_ret & V4L2_DEV_POLL_CAPTURE){
            ret = dequeueOutputBuffer();
        }
    }
    ALOGV("HandlePollThread stopped");
    bPollStopped = true;
    return OK;

}
status_t V4l2Dec::HandleFetchThread()
{
    while(bFetchStarted){
        // only start fetching when vpu output buffer isn't enough
        if (mVpuOwnedOutputBufferNum >= mOutputFormat.bufferNum || RUNNING != mState) {
            usleep(5000);
            continue;
        }
        ALOGV("getFreeGraphicBlock begin mVpuOwnedOutputBufferNum %d mOutputFormat.bufferNum %d", mVpuOwnedOutputBufferNum, mOutputFormat.bufferNum);

        GraphicBlockInfo *gbInfo = getFreeGraphicBlock();
        if (bNeedPostProcess) {
            if(!gbInfo || gbInfo->mBlockId >= mOutputFormat.bufferNum) {
                usleep(3000);
                continue;
            }
            ALOGV("HandleFetchThread queueOutput BEGIN, blockid=%d",gbInfo->mBlockId);
            queueOutput(gbInfo);
        } else {
            if(!gbInfo) {
                status_t ret = fetchOutputBuffer();
                if (OK == ret) {
                    gbInfo = getFreeGraphicBlock();
                } else if (WOULD_BLOCK == ret) {
                    usleep(1000);
                    continue;
                } else {
                    bReceiveError = true;
                    NotifyError(BAD_VALUE);
                    break;
                }
            }
            ALOGV("HandleFetchThread queueOutput 2 BEGIN");
            queueOutput(gbInfo);
        }
    }
    ALOGV("HandleFetchThread stopped");
    bFetchStopped = true;
    return OK;
}
// static
void *V4l2Dec::PollThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<V4l2Dec *>(me)->HandlePollThread();
}
void *V4l2Dec::FetchThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<V4l2Dec *>(me)->HandleFetchThread();
}

status_t V4l2Dec::createPollThread()
{
    ALOGV("%s", __FUNCTION__);
    Mutex::Autolock autoLock(mLock);

    if(!bPollStarted){

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        bPollStarted = true;
        bPollStopped = false;
        pthread_create(&mPollThread, &attr, PollThreadWrapper, this);
        pthread_attr_destroy(&attr);
    }
    return OK;
}
status_t V4l2Dec::destroyPollThread()
{
    ALOGV("%s", __FUNCTION__);

    if(bPollStarted){
        int cnt = 0;
        bPollStarted = false;

        pDev->StopDecoder();

        do {
            usleep(1000);
            cnt ++;
        } while (!bPollStopped && cnt < 20);
        ALOGV("%s bPollStopped bPollStopped=%d,cnt=%d", __FUNCTION__,bPollStopped,cnt);

        pDev->SetPollInterrupt();
        ALOGV("%s call pthread_join", __FUNCTION__);
        pthread_join(mPollThread, NULL);
        pDev->ClearPollInterrupt();
    }
    ALOGV("%s END", __FUNCTION__);
    return OK;
}
status_t V4l2Dec::createFetchThread()
{
    ALOGV("%s", __FUNCTION__);
    Mutex::Autolock autoLock(mLock);
    if(!bFetchStarted){
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        bFetchStarted = true;
        bFetchStopped = false;
        pthread_create(&mFetchThread, &attr, FetchThreadWrapper, this);
        pthread_attr_destroy(&attr);

    }
    return OK;
}
status_t V4l2Dec::destroyFetchThread()
{
    ALOGV("%s", __FUNCTION__);

    if(bFetchStarted){
        int cnt = 0;
        bFetchStarted = false;
        do {
            usleep(1000);
            cnt ++;
        } while (!bFetchStopped && cnt < 20);
        ALOGV("%s bFetchStopped=%d,cnt=%d", __FUNCTION__,bFetchStopped,cnt);

        pthread_join(mFetchThread, NULL);
    }
    ALOGV("%s END", __FUNCTION__);
    return OK;
}
status_t V4l2Dec::decodeInternal(std::unique_ptr<IMXInputBuffer> input)
{
    int result = 0;
    int32_t index = -1;
    uint32_t v4l2_flags = 0;
    bool returnEmptyWork = false;

    if(input == nullptr)
        return BAD_VALUE;

    if(STOPPED == mState || UNINITIALIZED == mState)
        onStart();

    if (!bCodecDataQueued && pCodecDataBuf && nCodecDataLen > 0) {
        bCodecDataQueued = true;
        ALOGV("queue codecdata");
        status_t ret = decodeInternal(std::make_unique<IMXInputBuffer>(
                                        pCodecDataBuf, -1, -1, nCodecDataLen, -1, false, true));
        if (ret != OK) {
            ALOGE("queue codecdata failed with ret %d", ret);
            return ret;
        }
    }

    int32_t fd = input->fd;
    int64_t ts = (int64_t)input->timestamp;
    uint32_t buf_length = 0;
    bool eos = input->eos;

    if (eos) {
        ALOGV("get input eos, call stopDecoder");
        Mutex::Autolock autoLock(mLock);
        if (OK != pDev->StopDecoder())
            ALOGW("Stop Decoder failed\n");
        return OK;
    }

    v4l2_flags |= (V4L2_BUF_FLAG_TIMESTAMP_MASK | V4L2_BUF_FLAG_TIMESTAMP_COPY);

    if (input->csd)
        v4l2_flags |= (IMX_V4L2_BUF_FLAG_CODECCONFIG | IMX_V4L2_BUF_FLAG_TIMESTAMP_INVALID);

    if(input->size > mInputFormat.bufferSize){
        ALOGE("invalid buffer size=%d,cap=%d",input->size, mInputFormat.bufferSize);
        return UNKNOWN_ERROR;
    }

    //check nal type for h264 frame, do not queue single sps or pps frame as one normal frame.
    if(bH264 && fd > 0 && input->id && input->size > 0 && !(v4l2_flags & IMX_V4L2_BUF_FLAG_CODECCONFIG)){

        const uint8_t * data = (uint8_t *)input->pInBuffer;
        size_t data_size = input->size;
        const uint8_t * nal_start = NULL;
        size_t nal_size = 0;
        bool codec_data_nal = false;
        bool has_other_nal = false;

        while (getNextNALUnit(&data, &data_size, &nal_start, &nal_size, true) == OK) {
            if (nal_size == 0) continue;

            unsigned nalType = nal_start[0] & 0x1f;
            if(nalType <= 5 || nalType == 20){
                has_other_nal = true;
            }else if(nalType >= 6 && nalType <= 8){
                codec_data_nal = true;
            }
        }

        if(codec_data_nal && !has_other_nal){
            ALOGV("SKIP SPS or PPS");
            NotifyInputBufferUsed(input->id);
            //return bad value then client IMXC2VideoDecoder will handle it
            return BAD_VALUE;
        }
    }

QueueOneBuffer:
    mLock.lock();

    //try to get index
    for(int32_t i = 0; i < mInputBufferMap.size(); i++){
        if(mInputBufferMap[i].input_id == -1 && !mInputBufferMap[i].at_device){
            index = i;
            break;
        }
    }

    if (index < 0) {
        // no available input index because input buffer queue too fast
        mLock.unlock();
        usleep(1000);
        goto QueueOneBuffer;
    }

    mInputBufferMap[index].input_id = input->id;
    ALOGV("decodeInternal input->BUF=%p, index=%d, len=%zu, ts=%lld, fd=%d",input->pInBuffer, index, input->size, ts, fd);

    if(mInMemType == V4L2_MEMORY_MMAP){
        memcpy((void*)(uintptr_t)(mInputBufferMap[index].plane.vaddr + buf_length), input->pInBuffer, input->size);
        buf_length += input->size;
        dumpInputBuffer((void*)(uintptr_t)mInputBufferMap[index].plane.vaddr, buf_length);
    }

    if(mInputBufferMap[index].at_device){
        ALOGE("onQueueInputBuffer index=%d, at_device",index);
    }

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane plane;
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&plane, 0, sizeof(plane));

    plane.bytesused = buf_length;
    plane.length = mInputFormat.bufferSize;
    plane.data_offset = 0;

    if(mInMemType == V4L2_MEMORY_MMAP)
        plane.m.mem_offset = 0;
    else if(mInMemType == V4L2_MEMORY_USERPTR)
        plane.m.userptr = (unsigned long)mInputBufferMap[index].plane.vaddr;
    else if(mInMemType == V4L2_MEMORY_DMABUF)
        plane.m.fd = mInputBufferMap[index].plane.fd;


    stV4lBuf.index = index;
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    stV4lBuf.timestamp.tv_sec = ts / 1000000;
    stV4lBuf.timestamp.tv_usec = ts % 1000000;

    stV4lBuf.memory = mInMemType;
    stV4lBuf.m.planes = &plane;
    stV4lBuf.length = kInputBufferPlaneNum;
    stV4lBuf.flags = v4l2_flags;

    ALOGV("VIDIOC_QBUF OUTPUT_MPLANE BEGIN index=%d,len=%d, ts=%lld\n",
        stV4lBuf.index, plane.bytesused, (long long)ts);

    result = ioctl(mFd, VIDIOC_QBUF, &stV4lBuf);
    if(result < 0){
        ALOGE("VIDIOC_QBUF OUTPUT_MPLANE failed, index=%d",index);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    mInputBufferMap[index].at_device = true;
    ALOGV("VIDIOC_QBUF OUTPUT_MPLANE END index=%d,len=%d, ts=%lld\n",
        stV4lBuf.index, plane.bytesused, (long long)ts);

    if(bMpeg2 && !input->csd){
        if(ts > 0 && mLastInputTs == ts){
            returnEmptyWork = true;
        }else{
            mLastInputTs = ts;
            mLastInputId = input->id;
        }
    }

    mInCnt++;
    mLock.unlock();

    if(!bInputStreamOn)
        startInputStream();

    if (input->id >= 0) {
        ALOGV("NotifyInputBufferUsed id=%d",input->id);
        NotifyInputBufferUsed(input->id);
    }

    //if one frame was split into several input buffer, then skip previous input id
    if(returnEmptyWork && mLastInputId > 0){
        ALOGV("NotifySkipInputBuffer id=%d",mLastInputId);
        NotifySkipInputBuffer(mLastInputId);
    }
    return OK;
}
status_t V4l2Dec::dequeueInputBuffer()
{
    int result = 0;
    int input_id = -1;


    if(!bInputStreamOn || mState != RUNNING )
        return OK;

    Mutex::Autolock autoLock(mLock);

    if(!bInputStreamOn || mState != RUNNING)
        return OK;

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kInputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(planes, 0, sizeof(planes));
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    stV4lBuf.memory = mInMemType;
    stV4lBuf.m.planes = planes;
    stV4lBuf.length = kInputBufferPlaneNum;
    ALOGV("VIDIOC_DQBUF OUTPUT_MPLANE BEGIN");
    result = ioctl(mFd, VIDIOC_DQBUF, &stV4lBuf);
    if(result < 0)
        return UNKNOWN_ERROR;

    if(stV4lBuf.index >= mInputFormat.bufferNum)
        return BAD_INDEX;

    ALOGV("VIDIOC_DQBUF OUTPUT_MPLANE END index=%d",stV4lBuf.index);
    if(!mInputBufferMap[stV4lBuf.index].at_device){
        ALOGE("dequeueInputBuffer index=%d, not at_device",stV4lBuf.index);
    }
    mInputBufferMap[stV4lBuf.index].at_device = false;
    input_id = mInputBufferMap[stV4lBuf.index].input_id;
    mInputBufferMap[stV4lBuf.index].input_id = -1;

    return OK;
}
status_t V4l2Dec::queueOutput(GraphicBlockInfo* pInfo)
{
    int result = 0;
    int32_t fd[kOutputBufferPlaneNum];
    uint64_t vaddr[kOutputBufferPlaneNum];
    uint64_t paddr[kOutputBufferPlaneNum];
    uint32_t offset[kOutputBufferPlaneNum];
    int32_t index = -1;

    if(!bFetchStarted || STOPPING == mState || FLUSHING == mState || RES_CHANGING == mState){
        ALOGV("queueOutput return 1");
        return OK;
    }

    ALOGV("queueOutput BEGIN id=%d",pInfo->mBlockId);
    mLock.lock();

    if(!bFetchStarted || STOPPING == mState || FLUSHING == mState || RES_CHANGING == mState){
        mLock.unlock();
        ALOGV("queueOutput return 2");
        return OK;
    }

    vaddr[0] = pInfo->mVirtAddr;
    vaddr[1] = pInfo->mVirtAddr;

    paddr[0] = pInfo->mPhysAddr;
    paddr[1] = pInfo->mPhysAddr;

    offset[0] = 0;
    offset[1] = mOutputPlaneSize[0];//mOutputFormat.stride * mOutputFormat.height;

    fd[0] = pInfo->mDMABufFd;
    fd[1] = pInfo->mDMABufFd;

    //try to get index
    for(int32_t i = 0; i < mOutputBufferMap.size(); i++){
        if(pInfo->mPhysAddr == mOutputBufferMap[i].planes[0].paddr){
            index = i;
            break;
        }
    }

    //index not found
    if(index < 0){
        for(int32_t i = 0; i < mOutputBufferMap.size(); i++){
            if(0 == mOutputBufferMap[i].planes[0].paddr){
                mOutputBufferMap[i].planes[0].fd = fd[0];
                mOutputBufferMap[i].planes[0].vaddr = vaddr[0];
                mOutputBufferMap[i].planes[0].paddr = paddr[0];
                mOutputBufferMap[i].planes[0].offset = offset[0];

                mOutputBufferMap[i].planes[1].fd = fd[1];
                mOutputBufferMap[i].planes[1].vaddr = vaddr[1];
                mOutputBufferMap[i].planes[1].paddr = paddr[1];
                mOutputBufferMap[i].planes[1].offset = offset[1];

                mOutputBufferMap[i].picture_id = pInfo->mBlockId;
                mOutputBufferMap[i].at_device = false;
                index = i;
                break;
            }
        }
    }

    if(index < 0){
        ALOGE("could not create index");
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    if(mOutputBufferMap[index].at_device){
        ALOGE("queueOutput at_device,index=%d, pInfo->mBlockId=%d",index,pInfo->mBlockId);
    }

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kOutputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&planes, 0, sizeof(planes));


    if(mOutMemType == V4L2_MEMORY_DMABUF){
        planes[0].m.fd = fd[0];
        planes[1].m.fd = fd[1];
    }else if(mOutMemType == V4L2_MEMORY_USERPTR){
        planes[0].m.userptr = vaddr[0];
        planes[1].m.userptr = vaddr[1];
    }

    planes[0].length = mOutputBufferMap[index].planes[0].size;
    planes[1].length = mOutputBufferMap[index].planes[1].size;

    planes[0].data_offset = mOutputBufferMap[index].planes[0].offset;
    planes[1].data_offset = mOutputBufferMap[index].planes[1].offset;

    stV4lBuf.index = index;
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    stV4lBuf.memory = mOutMemType;
    stV4lBuf.m.planes = &planes[0];
    stV4lBuf.length = kOutputBufferPlaneNum;
    stV4lBuf.flags = 0;

    ALOGV("VIDIOC_QBUF CAPTURE_MPLANE BEGIN index=%d blockId=%d\n",index, pInfo->mBlockId);

    result = ioctl(mFd, VIDIOC_QBUF, &stV4lBuf);
    if(result < 0){
        ALOGE("VIDIOC_QBUF CAPTURE_MPLANE failed, index=%d",index);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    ALOGV("VIDIOC_QBUF CAPTURE_MPLANE END index=%d blockId=%d\n",index, pInfo->mBlockId);
    pInfo->mState = GraphicBlockInfo::State::OWNED_BY_VPU;

    mVpuOwnedOutputBufferNum++;
    mOutputBufferMap[index].at_device = true;
    mLock.unlock();

    if(!bOutputStreamOn)
        startOutputStream();
    return OK;
}

status_t V4l2Dec::startInputStream()
{
    Mutex::Autolock autoLock(mLock);
    if(!bInputStreamOn){
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        if(0 == ioctl(mFd, VIDIOC_STREAMON, &buf_type)){
            bInputStreamOn = true;
        }
    }
    return OK;
}
status_t V4l2Dec::stopInputStream()
{
    ALOGV("%s", __FUNCTION__);
    Mutex::Autolock autoLock(mLock);
    if(bInputStreamOn){
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        if(0 == ioctl(mFd, VIDIOC_STREAMOFF, &buf_type)){
            bInputStreamOn = false;
        }
    }

    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        mInputBufferMap[i].at_device = false;
        mInputBufferMap[i].input_id = -1;
    }

    bInputStreamOn = false;
    return OK;
}
status_t V4l2Dec::startOutputStream()
{
    ALOGV("%s", __FUNCTION__);
    Mutex::Autolock autoLock(mLock);
    if(!bOutputStreamOn){
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if(0 == ioctl(mFd, VIDIOC_STREAMON, &buf_type)){
            bOutputStreamOn = true;
        }
    }
    return OK;
}
status_t V4l2Dec::stopOutputStream()
{
    ALOGV("%s", __FUNCTION__);
    Mutex::Autolock autoLock(mLock);
    //call VIDIOC_STREAMOFF and ignore the result
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(mFd, VIDIOC_STREAMOFF, &buf_type);
    bOutputStreamOn = false;

    // return capture buffer to component
    for(int32_t i = 0; i < mOutputBufferMap.size(); i++){
        if(mOutputBufferMap[i].planes[0].paddr > 0 && mOutputBufferMap[i].at_device) {
            GraphicBlockInfo *gbInfo = getGraphicBlockById(mOutputBufferMap[i].picture_id);
            gbInfo->mState = GraphicBlockInfo::State::OWNED_BY_COMPONENT;
            mOutputBufferMap[i].at_device = false;
            ALOGV("return capture buffer %d ", mOutputBufferMap[i].picture_id);
        }
    }

    mVpuOwnedOutputBufferNum = 0;

    return OK;
}
status_t V4l2Dec::dequeueOutputBuffer()
{
    int result = 0;

    if(!bOutputStreamOn || mState != RUNNING)
        return OK;

    Mutex::Autolock autoLock(mLock);

    if(!bOutputStreamOn || mState != RUNNING)
        return OK;

    int64_t ts = 0;
    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kOutputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(planes, 0, sizeof(planes));
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    stV4lBuf.memory = mOutMemType;
    stV4lBuf.m.planes = planes;
    stV4lBuf.length = kOutputBufferPlaneNum;
    ALOGV("VIDIOC_DQBUF CAPTURE_MPLANE BEGIN");
    result = ioctl(mFd, VIDIOC_DQBUF, &stV4lBuf);
    if(result < 0) {
        ALOGV("%s VIDIOC_DQBUF err=%d", __FUNCTION__, result);
        return UNKNOWN_ERROR;
    }

    if(stV4lBuf.index >= 32/*mOutputFormat.bufferNum*/) {
        ALOGI("dequeueOutputBuffer error");
        return BAD_INDEX;
    }

    ts = (int64_t)stV4lBuf.timestamp.tv_sec *1000000;
    ts += stV4lBuf.timestamp.tv_usec;

    ALOGV("VIDIOC_DQBUF CAPTURE_MPLANE END index=%d ts=%lld",stV4lBuf.index, (long long)ts);
    mVpuOwnedOutputBufferNum--;
    mOutCnt ++;
    mOutputBufferMap[stV4lBuf.index].at_device = false;
    dumpOutputBuffer((void*)(uintptr_t)mOutputBufferMap[stV4lBuf.index].planes[0].vaddr,mOutputFormat.bufferSize);
    NotifyPictureReady(mOutputBufferMap[stV4lBuf.index].picture_id, ts);
    return OK;
}
status_t V4l2Dec::onDequeueEvent()
{
    int result = 0;
    struct v4l2_event event;
    memset(&event, 0, sizeof(struct v4l2_event));

    result = ioctl(mFd, VIDIOC_DQEVENT, &event);
    if(result == 0){
        ALOGV("onDequeueEvent type=%d",event.type);
        switch(event.type){
            case V4L2_EVENT_SOURCE_CHANGE:
                if(event.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION){
                    //TODO: send event
                    if(STOPPING != mState)
                        handleFormatChanged();
                }
                break;
            case V4L2_EVENT_EOS:
            {
                Mutex::Autolock autoLock(mLock);
                usleep(1000);
                if(STOPPING != mState)
                    NotifyEOS();//send eos event
                bPollStarted = false;
                bFetchStarted = false;
                mState = STOPPED;
                break;
            }
            case V4L2_EVENT_DECODE_ERROR:
                ALOGE("get V4L2_EVENT_DECODE_ERROR");
                NotifyError(UNKNOWN_ERROR);//send error event
                break;
            case V4L2_EVENT_SKIP:
                NotifySkipInputBuffer(-1/*unused*/);
                break;
            default:
                break;
        }
    }

    return OK;
}

status_t V4l2Dec::DoSetConfig(DecConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    status_t ret = OK;

    switch (index) {
        case DEC_CONFIG_VC1_SUB_FORMAT: {
            if (strcmp(mMime, MEDIA_MIMETYPE_VIDEO_VC1) != 0) {
                ALOGE("DoSetConfig DEC_CONFIG_VC1_SUB_FORMAT only support for VC1");
                return BAD_VALUE;
            }

            int* format = (int*)pConfig;
            // TODO: remove this OMX_VIDEO_WMVFormat9=0x08, OMX_VIDEO_WMVFormatWVC1=0x7f000002
            if (*format == 0x08)
                mVc1Format = V4L2_PIX_FMT_VC1_ANNEX_G;
            else if (*format == 0x7f000002)
                mVc1Format = V4L2_PIX_FMT_VC1_ANNEX_L;

            ALOGV("vc1 sub-format 0x%x mVc1Format %d", *format, mVc1Format);
            break;
        }
        case DEC_CONFIG_HAL_PIXEL_FORMAT:{
            uint32_t* format = (uint32_t*)pConfig;
            if(*format == HAL_PIXEL_FORMAT_NV12_TILED)
                bNeedPostProcess = false;
            else
                bNeedPostProcess = true;
            ALOGV("set DEC_CONFIG_HAL_PIXEL_FORMAT fmt=0x%x,bNeedPostProcess=%d",*format, bNeedPostProcess);
            break;
        }
        case DEC_CONFIG_LOW_LATENCY: {
            int32_t* enable = (int32_t*)pConfig;
            ALOGV("set DEC_CONFIG_LOW_LATENCY enable=%d",*enable);
            if(1 == *enable)
                bLowLatency = true;
            else
                bLowLatency = false;
            break;
        }
        default:
            ret = BAD_VALUE;
            break;
    }
    return ret;
}

status_t V4l2Dec::DoGetConfig(DecConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    status_t ret = OK;

    switch (index) {
        case DEC_CONFIG_OUTPUT_DELAY: {
            int *pOutputDelayValue = (int*)pConfig;
            if(mOutputFormat.bufferNum > DEFAULT_OUTPUT_BUFFER_COUNT)
                *pOutputDelayValue = mOutputFormat.bufferNum;
            else
                *pOutputDelayValue = DEFAULT_OUTPUT_BUFFER_COUNT;
            ALOGV("DoGetConfig DEC_CONFIG_OUTPUT_DELAY =%d, mOutputFormat.bufferNum=%d",
                *pOutputDelayValue,mOutputFormat.bufferNum);
            break;
        }
        case DEC_CONFIG_COLOR_ASPECTS:{
            DecIsoColorAspects* isocolor = (DecIsoColorAspects*)pConfig;

            isocolor->colourPrimaries = mIsoColorAspect.colourPrimaries;
            isocolor->transferCharacteristics = mIsoColorAspect.transferCharacteristics;
            isocolor->matrixCoeffs = mIsoColorAspect.matrixCoeffs;
            isocolor->fullRange = mIsoColorAspect.fullRange;
            break;
        }
        default:
            ret = BAD_VALUE;
            break;
    }

    return ret;
}

status_t V4l2Dec::allocateOutputBuffers() {
    int ret;

    if(mState == STOPPING)
        return OK;

    Mutex::Autolock autoLock(mLock);

    if(mState == STOPPING)
        return OK;

    fsl::IonAllocator * pIonAllocator = fsl::IonAllocator::getInstance();

    mOutputBufferMap.resize(mOutputFormat.bufferNum);

    for (int i = 0; i < mOutputFormat.bufferNum; i++) {
        if (!bNeedPostProcess) {
            status_t ret;
            do {
                ret = fetchOutputBuffer();
            } while (WOULD_BLOCK == ret);

            if (ret != OK) {
                bReceiveError = true;
                NotifyError(ret);
                return ret;
            } else
                continue;
        }

        int fd = 0;
        uint64_t phys_addr = 0;
        uint64_t virt_addr = 0;

        //allocate dma buffer for decoder output buffers when enable post process
        fd = pIonAllocator->allocMemory(mOutputFormat.bufferSize, ION_MEM_ALIGN, fsl::MFLAGS_CONTIGUOUS);

        if (fd <= 0) {
            ALOGE("Ion allocate failed i=%d,size=%d", i, mOutputFormat.bufferSize);
            return BAD_VALUE;
        }

        ret = pIonAllocator->getPhys(fd, mOutputFormat.bufferSize, phys_addr);
        if (ret != 0) {
            ALOGE("DmaBuffer getPhys failed");
            return BAD_VALUE;
        }

        ret = pIonAllocator->getVaddrs(fd, mOutputFormat.bufferSize, virt_addr);
        if (ret != 0) {
            ALOGE("DmaBuffer getVaddrs failed");
            return BAD_VALUE;
        }

        GraphicBlockInfo info;
        memset(&info, 0, sizeof(GraphicBlockInfo));
        info.mBlockId = i;
        info.mDMABufFd = fd;
        info.mPhysAddr = phys_addr;
        info.mVirtAddr = virt_addr;
        info.mCapacity = mOutputFormat.bufferSize;
        info.mState = GraphicBlockInfo::State::OWNED_BY_COMPONENT;
        mGraphicBlocks.push_back(std::move(info));
        ALOGV("Ion allocate fd=%d phys_addr=%p vaddr=%p\n",fd, (void*)phys_addr, (void*)virt_addr);
        ALOGV("mOutputBufferMap[%d] phys %p, at_device %d", i,
            (void*)mOutputBufferMap[i].planes[0].paddr, mOutputBufferMap[i].at_device);

    }
    return OK;
}

status_t V4l2Dec::freeOutputBuffers() {

    ALOGV("%s", __FUNCTION__);
    Mutex::Autolock autoLock(mLock);

    if(mGraphicBlocks.empty())
        return OK;

    for (auto& info : mGraphicBlocks) {
        ALOGV("freeOutputBuffers fd=%d,id=%d",info.mDMABufFd,info.mBlockId);

        if (info.mVirtAddr > 0 && info.mCapacity > 0)
            munmap((void*)info.mVirtAddr, info.mCapacity);

        if(bNeedPostProcess && info.mDMABufFd > 0)
            close(info.mDMABufFd);
        if(info.mGraphicBlock != NULL){
            ALOGV("info.mGraphicBlock reset");
            info.mGraphicBlock.reset();
        }
    }

    mGraphicBlocks.clear();
    return OK;
}

status_t V4l2Dec::handleFormatChanged() {

    status_t ret = OK;
    int pre_state;
    Mutex::Autolock autoThreadLock(mThreadLock);
    ALOGV("outputFormatChanged BEGIN");
    {
        Mutex::Autolock autoLock(mLock);
        pre_state = mState;
        mState = RES_CHANGING;
        int result = 0;
        struct v4l2_format format;
        uint32_t pixel_format = 0;
        memset(&format, 0, sizeof(struct v4l2_format));

        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        result = ioctl (mFd, VIDIOC_G_FMT, &format);

        if(result < 0)
            return UNKNOWN_ERROR;

        ret = pDev->GetColorFormatByV4l2( format.fmt.pix_mp.pixelformat, &pixel_format);
        if(ret != OK)
            return ret;

        mOutputFormat.pixelFormat = static_cast<int>(pixel_format);
        if(mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_P010){
            bNeedPostProcess = true;
            mOutFormat = format.fmt.pix_mp.pixelformat;
            ALOGV("10bit video stride=%d",format.fmt.pix_mp.plane_fmt[0].bytesperline);
        }

        mOutputFormat.width = Align(format.fmt.pix_mp.width, FRAME_ALIGN);
        mOutputFormat.height = Align(format.fmt.pix_mp.height, FRAME_ALIGN);
        mOutputFormat.stride = mOutputFormat.width;

        //for 10bit video, stride is larger than width, should use stride to allocate buffer
        if(mOutputFormat.width < format.fmt.pix_mp.plane_fmt[0].bytesperline){
            mOutputFormat.stride = format.fmt.pix_mp.plane_fmt[0].bytesperline;
        }

        mOutputFormat.interlaced = ((format.fmt.pix_mp.field == V4L2_FIELD_INTERLACED) ? true: false);
        mOutputFormat.bufferSize = format.fmt.pix_mp.plane_fmt[0].sizeimage + format.fmt.pix_mp.plane_fmt[1].sizeimage;

        mOutputPlaneSize[0] = format.fmt.pix_mp.plane_fmt[0].sizeimage;
        mOutputPlaneSize[1] = format.fmt.pix_mp.plane_fmt[1].sizeimage;

        pDev->GetColorAspectsInfo( &format.fmt.pix_mp, &mIsoColorAspect);

        struct v4l2_control ctl;
        memset(&ctl, 0, sizeof(struct v4l2_control));

        ctl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
        result = ioctl(mFd, VIDIOC_G_CTRL, &ctl);
        if(result < 0)
            return UNKNOWN_ERROR;

        mOutputFormat.minBufferNum = ctl.value+1;

        if(mOutputFormat.minBufferNum > mOutputFormat.bufferNum)
            mOutputFormat.bufferNum = mOutputFormat.minBufferNum;

        if (!bNeedPostProcess) {
            // workaround: surface setMaxDequeuedBufferCount: numOutputSlots + numInputSlots + depth + kRenderingDepth
            // as default, kRenderingDepth = 3, depth = 0, numInputSlots = 4, numOutputSlots = outputDelayValue + 4
            // 4 is value of kSmoothnessFactor
            // so if we return bufferNum directly, surface will setMaxDequeuedBufferCount as bufferNum + 4 + 4 + 3,
            // it's too much for VPU, we can cut some here to avoid it exceed the max value(32).
            if (mOutputFormat.bufferNum >= 18)
                mOutputFormat.bufferNum -= (4+4);
        }

        struct v4l2_crop crop;
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        result = ioctl (mFd, VIDIOC_G_CROP, &crop);
        if(result < 0)
            return UNKNOWN_ERROR;

        //seems decoder just be flushed
        if(crop.c.width == 0 && crop.c.height == 0){
            ALOGE("handleFormatChanged flushed return");
            return OK;
        }

        mOutputFormat.rect.right = crop.c.width;
        mOutputFormat.rect.bottom = crop.c.height;
        mOutputFormat.rect.top = crop.c.top;
        mOutputFormat.rect.left = crop.c.left;
    }

    ALOGV("outputFormatChanged w=%d,h=%d,buf cnt=%d, buffer size[0]=%d,size[1]=%d",
        mOutputFormat.width, mOutputFormat.height, mOutputFormat.minBufferNum, mOutputPlaneSize[0], mOutputPlaneSize[1]);

    ALOGV("mIsoColorAspect c=%d,t=%d,m=%d,f=%d",mIsoColorAspect.colourPrimaries, mIsoColorAspect.transferCharacteristics,mIsoColorAspect.matrixCoeffs, mIsoColorAspect.fullRange);
    if(pre_state == STOPPING || pre_state == FLUSHING){
        ALOGI("do not handle resolution while flushing or stopping");
        return OK;
    }

    if (bFetchStarted) {
        destroyFetchThread();
    }

    if (bOutputStreamOn) {
        stopOutputStream();
        // clear capture buffer
        mOutputBufferMap.clear();

        SetOutputFormats();
    }

    outputFormatChanged();

    ALOGV("outputFormatChanged end");
    return OK;
}

status_t V4l2Dec::stopStreaming() {
    status_t ret = UNKNOWN_ERROR;

    ret = stopInputStream();
    if(ret != OK)
        return ret;

    ret = stopOutputStream();
    if(ret != OK)
        return ret;

    ret = destroyPollThread();
    if(ret != OK)
        return ret;

    ret = destroyFetchThread();

    return ret;
}

status_t V4l2Dec::startStreaming() {
    status_t ret = UNKNOWN_ERROR;

    ret = startInputStream();
    if(ret != OK)
        return ret;

    ret = startOutputStream();
    if(ret != OK)
        return ret;

    ret = createPollThread();
    if(ret != OK)
        return ret;

    ret = createFetchThread();

    return ret;

}

status_t V4l2Dec::onFlush()
{
    ALOGV("%s", __FUNCTION__);
    int pre_state;
    {
        Mutex::Autolock autoLock(mLock);
        pre_state = mState;
        if(mState != STOPPING)
            mState = FLUSHING;
    }
    status_t ret = UNKNOWN_ERROR;

    // let codecdata queue again after each seek
    bCodecDataQueued = false;

    ret = stopInputStream();
    if(ret != OK)
        return ret;

    ret = stopOutputStream();
    if(ret != OK)
        return ret;

    mState = pre_state;
    mInCnt = 0;
    mOutCnt = 0;
    mLastInputTs = -1;
    mLastInputId = 0;
    bReceiveError = false;
    ALOGV("%s end", __FUNCTION__);
    return ret;
}
status_t V4l2Dec::onStop()
{

    ALOGV("%s", __FUNCTION__);
    status_t ret = UNKNOWN_ERROR;
    Mutex::Autolock autoThreadLock(mThreadLock);
    {
    Mutex::Autolock autoLock(mLock);
    mState = STOPPING;
    }
    ret = onFlush();
    if(ret != OK){
        return ret;
    }

    ret = destroyPollThread();
    if(ret != OK){
        return ret;
    }

    ret = destroyFetchThread();
    if(ret != OK){
        return ret;
    }

    ret = destroyInputBuffers();
    if(ret != OK){
        return ret;
    }

    ret = destroyOutputBuffers();
    if(ret != OK){
        return ret;
    }

    Mutex::Autolock autoLock(mLock);
    mState = STOPPED;

    if(pDev != NULL)
        pDev->ResetDecoder();
    ALOGV("%s end", __FUNCTION__);
    return OK;
}
status_t V4l2Dec::onDestroy()
{
    status_t ret = UNKNOWN_ERROR;
    ALOGV("%s", __FUNCTION__);

    if(mState != STOPPED){
        onStop();
        mState = STOPPED;
    }

    Mutex::Autolock autoLock(mLock);

    if(mFd > 0){
        pDev->Close();
        ALOGV("pDev->Close %d",mFd);
        mFd = 0;
    }

    if(pDev != NULL)
        delete pDev;
    pDev = NULL;
    ALOGV("%s end", __FUNCTION__);
    return OK;
}

bool V4l2Dec::checkIfPostProcessNeeded() {
    return bNeedPostProcess;
}
void V4l2Dec::ParseVpuLogLevel()
{
    int level=0;
    FILE* fpVpuLog;
    nDebugFlag = 0;
    
    fpVpuLog=fopen(VPU_DECODER_LOG_LEVELFILE, "r");
    if (NULL==fpVpuLog){
        return;
    }

    char symbol;
    int readLen = 0;

    readLen = fread(&symbol,1,1,fpVpuLog);
    if(feof(fpVpuLog) != 0){
        ;
    }
    else{
        level=atoi(&symbol);
        if((level<0) || (level>255)){
            level=0;
        }
    }
    fclose(fpVpuLog);

    nDebugFlag=level;

    if(nDebugFlag != 0)
        ALOGV("ParseVpuLogLevel nDebugFlag=%x",nDebugFlag);
    return;
}
void V4l2Dec::dumpInputBuffer(void* inBuf, uint32_t size)
{
    FILE * pfile = NULL;

    if(!(nDebugFlag & DUMP_DEC_FLAG_INPUT))
        return;

    if(!inBuf){
        ALOGV("dumpInputBuffer invalid fd");
        return;
    }

    pfile = fopen(DUMP_DEC_INPUT_FILE,"ab");

    if(pfile){
        fwrite(inBuf,1,size,pfile);
        ALOGV("dumpInputBuffer write %d",size);
        fclose(pfile);
    }else
        ALOGV("dumpInputBuffer failed to open %s",DUMP_DEC_INPUT_FILE);
    return;
}
void V4l2Dec::dumpOutputBuffer(void* inBuf, uint32_t size)
{
    FILE * pfile = NULL;

    if(!(nDebugFlag & DUMP_DEC_FLAG_OUTPUT))
        return;

    if(!inBuf){
        ALOGV("dumpOutputBuffer invalid fd");
        return;
    }

    pfile = fopen(DUMP_DEC_OUTPUT_FILE,"ab");

    if(pfile){
        fwrite(inBuf,1,size,pfile);
        ALOGV("dumpOutputBuffer write %d",size);
        fclose(pfile);
    }else
        ALOGV("dumpOutputBuffer failed to open %s",DUMP_DEC_OUTPUT_FILE);
    return;
}
bool V4l2Dec::OutputBufferFull() {
    if(mOutputBufferMap.size() > mOutputFormat.bufferNum)
        return true;
    return false;
}

VideoDecoderBase * CreateVideoDecoderInstance(const char* mime) {
    return static_cast<VideoDecoderBase *>(new V4l2Dec(mime));
}
}
