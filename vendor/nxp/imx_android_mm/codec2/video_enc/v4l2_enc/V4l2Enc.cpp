/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "V4l2Enc"

#include "V4l2Enc.h"
#include <media/stagefright/MediaErrors.h>
#include "graphics_ext.h"
#include <sys/mman.h>
#include "C2_imx.h"

namespace android {

#define VPU_ENCODER_LOG_LEVELFILE "/data/vpu_enc_level"
#define DUMP_ENC_INPUT_FILE "/data/temp_enc_in.yuv"
#define DUMP_ENC_OUTPUT_FILE "/data/temp_enc_out.bit"

#define DUMP_ENC_FLAG_INPUT     0x1
#define DUMP_ENC_FLAG_OUTPUT    0x2

#define Align(ptr,align)    (((uint32_t)(ptr)+(align)-1)/(align)*(align))
#define FRAME_ALIGN     (2)

#define DEFAULT_INPUT_BUFFER_COUNT (16)

V4l2Enc::V4l2Enc(const char* mime):
    mMime(mime),
    mPollThread(0),
    mFetchThread(0),
    pDev(NULL),
    mFd(-1){

    mInputFormat.bufferNum = DEFAULT_INPUT_BUFFER_COUNT;
    mInputFormat.bufferSize = DEFAULT_FRM_WIDTH * DEFAULT_FRM_HEIGHT * 3/2;
    mInputFormat.width = DEFAULT_FRM_WIDTH;
    mInputFormat.height = DEFAULT_FRM_HEIGHT;
    mInputFormat.interlaced = false;
    mInputFormat.pixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;

    mOutputFormat.width = DEFAULT_FRM_WIDTH;
    mOutputFormat.height = DEFAULT_FRM_HEIGHT;
    mOutputFormat.minBufferNum = 2;
    mOutputFormat.bufferNum = kDefaultOutputBufferCount;
    mOutputFormat.interlaced = false;

    mConverter = NULL;

    bPreProcess = false;
    ALOGV("mMime=%s",mMime);
    mState = UNINITIALIZED;

}
V4l2Enc::~V4l2Enc()
{
    if(pDev != NULL && (bFetchStarted || bPollStarted))
        onStop();
    ALOGV("V4l2Enc::~V4l2Enc");
    onDestroy();
}
status_t V4l2Enc::onInit(){
    status_t ret = UNKNOWN_ERROR;

    if(pDev == NULL){
        pDev = new V4l2Dev();
    }
    if(pDev == NULL)
        return ret;

    mFd = pDev->Open(V4L2_DEV_ENCODER);
    ALOGV("onInit pV4l2Dev->Open fd=%d",mFd);

    if(mFd < 0)
        return ret;

    //V4L2_MEMORY_USERPTR; V4L2_MEMORY_DMABUF; V4L2_MEMORY_MMAP
    mInMemType = V4L2_MEMORY_DMABUF;
    mOutMemType = V4L2_MEMORY_MMAP;
    mFrameOutNum = 0;

    ret = prepareOutputParams();

    ParseVpuLogLevel();
    mState = UNINITIALIZED;

    return ret;
}
status_t V4l2Enc::onStart()
{

    status_t ret = UNKNOWN_ERROR;

    bPollStarted = false;
    bFetchStarted = false;
    bInputStreamOn = false;
    bOutputStreamOn = false;
    bSyncFrame = false;
    bHasCodecData = false;

    mFrameOutNum = 0;

    if(!strcmp(mMime, "video/avc")){
        mConverter = new FrameConverter();
        if(mConverter == NULL){
            ret = UNKNOWN_ERROR;
            return ret;
        }
        ALOGV("NEW FrameConverter");
        ret = mConverter->Create(V4L2_PIX_FMT_H264);
    }

    ret = SetOutputFormats();
    if(ret != OK)
        return ret;

    ret = prepareOutputBuffers();
    ALOGV("onStart prepareInputBuffers ret=%d",ret);
    if(ret != OK)
        return ret;

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

    if(mInputBufferMap.empty() || (mInputFormat.bufferSize != mInputPlaneSize[0] + mInputPlaneSize[1])){

        ret = prepareInputBuffers();
        ALOGV("onStart prepareInputBuffers ret=%d",ret);
        if(ret != OK)
            return ret;
    }

    ret = pDev->SetEncoderParam(&mEncParam);
    if(ret != OK){
        ALOGE("SetEncoderParam failed");
        return ret;
    }

    ret = pDev->SetFrameRate(mTargetFps);
    if(ret != OK){
        ALOGE("SetFrameRate failed");
        return ret;
    }


    //allocate output buffers
    for(int32_t i = 0; i < mOutputFormat.bufferNum; i++){
        ret = allocateOutputBuffer(i);
        if(ret != OK){
            ALOGE("allocateOutputBuffer failed");
            return ret;
        }
    }

    ret = createPollThread();
    if(ret != OK){
        ALOGE("createPollThread failed");
        return ret;
    }
    mState = RUNNING;

    ret = createFetchThread();

    ALOGV("onStart ret=%d",ret);

    return ret;
}
status_t V4l2Enc::prepareOutputParams()
{
    status_t ret = UNKNOWN_ERROR;
    Mutex::Autolock autoLock(mLock);

    ret = pDev->GetStreamTypeByMime(mMime, &mOutFormat);
    if(ret != OK)
        return ret;

    ALOGV("mOutFormat=%x",mOutFormat);
    if(!pDev->IsCaptureFormatSupported(mOutFormat)){
        ALOGE("encoder format not suppoted");
        return ret;
    }

    if(mOutputFormat.bufferNum == 0){
        mOutputFormat.bufferNum = 2;
    }
    ALOGV("bufferNum=%d,bufferSize=%d",mOutputFormat.bufferNum, mOutputFormat.bufferSize);
    mWidthAlign = 1;
    mHeightAlign = 1;

    struct v4l2_frmsizeenum info;
    memset(&info, 0, sizeof(v4l2_frmsizeenum));
    if(OK != pDev->GetFormatFrameInfo(mOutFormat, &info)){
        ALOGE("GetFormatFrameInfo failed");
        return ret;
    }

    mWidthAlign = info.stepwise.step_width;
    mHeightAlign = info.stepwise.step_height;

    return OK;
}
status_t V4l2Enc::SetOutputFormats()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    mOutputFormat.width = Align(mOutputFormat.width, mWidthAlign);
    mOutputFormat.height = Align(mOutputFormat.height, mHeightAlign);

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.num_planes = kOutputBufferPlaneNum;
    format.fmt.pix_mp.pixelformat = mOutFormat;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = mOutputFormat.bufferSize;
    format.fmt.pix_mp.plane_fmt[0].bytesperline = Align(mOutputFormat.width, mWidthAlign);
    format.fmt.pix_mp.width = mOutputFormat.width;
    format.fmt.pix_mp.height = mOutputFormat.height;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;

    result = ioctl (mFd, VIDIOC_S_FMT, &format);
    if(result != 0)
        return UNKNOWN_ERROR;

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    result = ioctl (mFd, VIDIOC_G_FMT, &format);
    if(result != 0)
        return UNKNOWN_ERROR;

    if(format.fmt.pix_mp.pixelformat != mOutFormat){
        ALOGE("SetOutputFormats mInFormat mismatch");
        return UNKNOWN_ERROR;
    }

    if( format.fmt.pix_mp.width != mOutputFormat.width ||
        format.fmt.pix_mp.height != mOutputFormat.height){
        ALOGE("SetOutputFormats resolution mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].bytesperline != Align(mOutputFormat.width, mWidthAlign)){
        ALOGE("SetOutputFormats stride mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].sizeimage != mOutputFormat.bufferSize){
        ALOGE("SetOutputFormats bufferSize mismatch");
        return UNKNOWN_ERROR;
    }

    return OK;
}
status_t V4l2Enc::prepareInputParams()
{
    status_t ret = UNKNOWN_ERROR;
    Mutex::Autolock autoLock(mLock);

    ret = pDev->GetV4l2FormatByColor(mInputFormat.pixelFormat, &mInFormat);
    if(ret != OK){
        ALOGE("prepareInputParams failed, pixelFormat=%x,mInFormat=%x",mInputFormat.pixelFormat,mInFormat);
        return ret;
    }

    if(!pDev->IsOutputFormatSupported(mInFormat)){
        ALOGE("encoder input format not suppoted");
        return ret;
    }

    if(mInFormat == V4L2_PIX_FMT_NV12){
        //update output frame width & height
        mInputFormat.width = Align(mInputFormat.width, mWidthAlign);
        mInputFormat.height = Align(mInputFormat.height, mHeightAlign);

        mInputPlaneSize[0] = mInputFormat.width * mInputFormat.height;
        mInputPlaneSize[1] = mInputPlaneSize[0]/2;
        mInputFormat.bufferSize = mInputPlaneSize[0] + mInputPlaneSize[1];
        ALOGV("prepareInputParams w=%d,h=%d,input buffer size=%d",mInputFormat.width, mInputFormat.height, mInputFormat.bufferSize);

    }else{
        ALOGE("encoder input format not NV12");
        return UNKNOWN_ERROR;
    }

    return OK;
}
status_t V4l2Enc::SetInputFormats()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    format.fmt.pix_mp.num_planes = kInputBufferPlaneNum;
    format.fmt.pix_mp.pixelformat = mInFormat;
    format.fmt.pix_mp.width = mInputFormat.width;
    format.fmt.pix_mp.height = mInputFormat.height;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = mInputPlaneSize[0];
    format.fmt.pix_mp.plane_fmt[0].bytesperline = Align(mInputFormat.width, mWidthAlign);
    format.fmt.pix_mp.plane_fmt[1].sizeimage = mInputPlaneSize[1];
    format.fmt.pix_mp.plane_fmt[1].bytesperline = Align(mInputFormat.width, mWidthAlign);
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;

    pDev->SetColorAspectsInfo(&mIsoColorAspect, &format.fmt.pix_mp);

    result = ioctl (mFd, VIDIOC_S_FMT, &format);
    if(result != 0){
        ALOGE("SetInputFormats VIDIOC_S_FMT failed");
        return UNKNOWN_ERROR;
    }

    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    result = ioctl (mFd, VIDIOC_G_FMT, &format);
    if(result < 0)
        return UNKNOWN_ERROR;


    if(format.fmt.pix_mp.pixelformat != mInFormat){
        ALOGE("SetInputFormats mOutFormat mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.width != mInputFormat.width ||
        format.fmt.pix_mp.height != mInputFormat.height){
        ALOGE("SetInputFormats resolution mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].sizeimage != mInputPlaneSize[0] ||
        format.fmt.pix_mp.plane_fmt[1].sizeimage != mInputPlaneSize[1]){
        ALOGE("SetInputFormats bufferSize mismatch");
        return UNKNOWN_ERROR;
    }

    ALOGV("SetInputFormats success");
    return OK;
}

V4l2Enc::InputRecord::InputRecord()
    : at_device(false), input_id(-1), ts(-1) {
    memset(&planes, 0, kInputBufferPlaneNum * sizeof(VideoFramePlane));
}

V4l2Enc::InputRecord::~InputRecord() {}

V4l2Enc::OutputRecord::OutputRecord()
    : at_device(false), picture_id(0), flag(0) {
}

V4l2Enc::OutputRecord::~OutputRecord() {
}
status_t V4l2Enc::prepareInputBuffers()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = mInputFormat.bufferNum;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = mInMemType;
    ALOGV("prepareInputBuffers VIDIOC_REQBUFS bufferNum=%d",reqbufs.count);
    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        ALOGE("VIDIOC_REQBUFS failed result=%d",result);
        return UNKNOWN_ERROR;
    }
    ALOGV("prepareInputBuffers mInputBufferMap resize=%d",reqbufs.count);
    mInputBufferMap.resize(reqbufs.count);

    ALOGV("prepareInputBuffers total input=%d size=%d",mOutputFormat.bufferNum, mOutputBufferMap.size());

    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        mInputBufferMap[i].at_device = false;
        mInputBufferMap[i].input_id = -1;
        mInputBufferMap[i].planes[0].fd = -1;
        mInputBufferMap[i].planes[0].addr = 0;
    }
    return OK;
}
status_t V4l2Enc::prepareOutputBuffers()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = mOutputFormat.bufferNum;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutMemType;

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        ALOGE("VIDIOC_REQBUFS failed result=%d",result);
        return UNKNOWN_ERROR;
    }

    mOutputBufferMap.resize(reqbufs.count);

    for (size_t i = 0; i < mOutputBufferMap.size(); i++) {
        mOutputBufferMap[i].at_device = false;
        mOutputBufferMap[i].plane.fd = -1;
        mOutputBufferMap[i].plane.addr = 0;
        mOutputBufferMap[i].plane.size = mOutputFormat.bufferSize;
        mOutputBufferMap[i].plane.length = 0;
        mOutputBufferMap[i].plane.offset = 0;
        mOutputBufferMap[i].flag = 0;
    }

    ALOGV("VIDIOC_REQBUFS CAPTURE_MPLANE success input=%d size=%d",mOutputFormat.bufferNum, mOutputBufferMap.size());

    return OK;
}
status_t V4l2Enc::destroyInputBuffers()
{
    Mutex::Autolock autoLock(mLock);
    if (mInputBufferMap.empty())
        return OK;

    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;//use mmap to free buffer

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0)
        return UNKNOWN_ERROR;

    mInputBufferMap.clear();
    ALOGV("destroyInputBuffers success");
    return OK;
}
status_t V4l2Enc::destroyOutputBuffers()
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

    if(result != 0)
        return UNKNOWN_ERROR;

    mOutputBufferMap.clear();
    //clearOutputFrameBuffer();//call it here or in base class
    ALOGV("destroyOutputBuffers success");
    return OK;
}
status_t V4l2Enc::HandlePollThread()
{
    status_t ret = OK;
    int32_t poll_ret = 0;

    while(bPollStarted){
        ALOGV("pollThreadHandler BEGIN");
        poll_ret = pDev->Poll();
        ALOGV("pollThreadHandler poll_ret=%x",poll_ret);

        if(!bPollStarted)
            break;

        if(poll_ret & V4L2_DEV_POLL_EVENT){
            ret = onDequeueEvent();
        }
        if(poll_ret & V4L2_DEV_POLL_OUTPUT){
            ret = dequeueInputBuffer();
        }
        if(poll_ret & V4L2_DEV_POLL_CAPTURE){
            ret = dequeueOutputBuffer();
        }
        ALOGV("pollThreadHandler END ret=%x",ret);
    }
    ALOGV("HandlePollThread return");
    return OK;

}
status_t V4l2Enc::HandleFetchThread()
{
    int blockId;
    int fd;
    unsigned long nOutPhy;
    unsigned long nOutVirt;
    uint32_t nOutLength = mOutputFormat.bufferSize;

    while(bFetchStarted){
        int32_t i = 0;
        int32_t j = 0;
        if(mOutputBufferMap.empty() || RUNNING != mState){
            usleep(1000);
            continue;
        }
        
        bool fetch = false;
        int32_t currNum = (mFrameOutNum % mOutputFormat.bufferNum);

        for (i = currNum; i < currNum + mOutputFormat.bufferNum; i++) {
            j = i;
            if(i < mOutputFormat.bufferNum)
                j = i;
            else
                j = i - mOutputFormat.bufferNum;

            if(mOutputBufferMap[j].at_device == false){
                fetch = true;
                break;
            }
        }

        if(!fetch){
            usleep(1000);
            continue;
        }

         if(!bFetchStarted)
            break;

        ALOGV("call FetchOutputBuffer BEGIN");
        queueOutput(j, -1, 0);
        //if(OK == FetchOutputBuffer(&blockId, &fd, &nOutPhy, &nOutVirt, &nOutLength)){
        //queueOutput(blockId, fd, nOutVirt);
        //}

    }
    return OK;
}
// static
void *V4l2Enc::PollThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<V4l2Enc *>(me)->HandlePollThread();
}
void *V4l2Enc::FetchThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<V4l2Enc *>(me)->HandleFetchThread();
}

status_t V4l2Enc::createPollThread()
{
    Mutex::Autolock autoLock(mLock);

    if(!bPollStarted){
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        bPollStarted = true;
        pthread_create(&mPollThread, &attr, PollThreadWrapper, this);
        pthread_attr_destroy(&attr);
    }
    return OK;
}
status_t V4l2Enc::destroyPollThread()
{
    ALOGV("destroyPollThread BEGIN");

    if(bPollStarted){
        bPollStarted = false;
        ALOGV("destroyPollThread bPollStarted FALSE");
        usleep(1000);
        pDev->SetPollInterrupt();
        mLock.lock();
        pthread_join(mPollThread, NULL);
        mLock.unlock();
        pDev->ClearPollInterrupt();
    }
    return OK;
}
status_t V4l2Enc::createFetchThread()
{
    Mutex::Autolock autoLock(mLock);

    if(!bFetchStarted){
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        
        bFetchStarted = true;
        pthread_create(&mFetchThread, &attr, FetchThreadWrapper, this);
        pthread_attr_destroy(&attr);
    }
    return OK;
}
status_t V4l2Enc::destroyFetchThread()
{
    ALOGV("destroyFetchThread BEGIN");

    if(bFetchStarted){
        bFetchStarted = false;
        ALOGV("destroyFetchThread bFetchStarted FALSE");
        usleep(1000);
        mLock.lock();
        pthread_join(mFetchThread, NULL);
        mLock.unlock();
    }

    return OK;
}
status_t V4l2Enc::encodeInternal(std::unique_ptr<IMXInputBuffer> input)
{
    int result = 0;

    int32_t index = -1;
    uint32_t v4l2_flags = 0;

    if(input == nullptr)
        return BAD_VALUE;

    if(STOPPED == mState || UNINITIALIZED == mState)
        onStart();

    mLock.lock();

    bool eos = input->eos;

    if(eos)
        v4l2_flags |= V4L2_BUF_FLAG_LAST;

    //handle eos
    if(eos && -1 == input->id && 0 == input->size){
        ALOGV("encodeInternal StopEncoder");
        pDev->StopEncoder();
        mLock.unlock();
        return OK;
    }

    if((int64_t)input->timestamp >= 0)
        v4l2_flags |= (V4L2_BUF_FLAG_TIMESTAMP_MASK | V4L2_BUF_FLAG_TIMESTAMP_COPY);

    if(bSyncFrame || (input->flag & FLAG_SYNC_FRAME)){
        v4l2_flags |= V4L2_BUF_FLAG_KEYFRAME;
        bSyncFrame = false;
    }

    if(input->size > mInputFormat.bufferSize){
        ALOGE("invalid buffer size=%d,cap=%d",input->size, mInputFormat.bufferSize);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    //try to get index
    for(int32_t i = 0; i < mInputBufferMap.size(); i++){
        if((mInputBufferMap[i].planes[0].addr == (uint64_t)input->pInputPhys)
            && !mInputBufferMap[i].at_device){
            index = i;
            break;
        }
    }

    //index not found
    if(index < 0){
        for(int32_t i = 0; i < mInputBufferMap.size(); i++){
            if(0 == mInputBufferMap[i].planes[0].addr){
                mInputBufferMap[i].planes[0].fd = input->fd;
                mInputBufferMap[i].planes[0].addr = (uint64_t)input->pInputPhys;
                mInputBufferMap[i].planes[0].offset = 0;
                index = i;
                break;
            }
        }
    }

    if(index < 0){
        mLock.unlock();
        ALOGE("encodeInternal invalid index");
        return UNKNOWN_ERROR;
    }

    mInputBufferMap[index].input_id = input->id;
    ALOGV("encodeInternal input_id=%d,input->BUF=%p,phys=%x, index=%d, len=%zu, ts=%lld, fd=%d",
        input->id, input->pInputVirt,input->pInputPhys, index, input->size, input->timestamp, input->fd);

    if(mInputBufferMap[index].at_device){
        ALOGE("onQueueInputBuffer index=%d, at_device",index);
    }

    if (v4l2_flags & V4L2_BUF_FLAG_KEYFRAME) {
        pDev->SetForceKeyFrame();
    }

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kInputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&planes[0], 0, kInputBufferPlaneNum * sizeof(struct v4l2_plane));

    planes[0].bytesused = mInputPlaneSize[0];
    planes[0].length = mInputPlaneSize[0];
    planes[0].data_offset = 0;

    if(bPreProcess){
        planes[1].bytesused = mInputPlaneSize[1];
        planes[1].length = mInputPlaneSize[1];
        planes[1].data_offset = 0;
    }else{
        planes[1].bytesused = mInputPlaneSize[0]+ mInputPlaneSize[1];
        planes[1].length = mInputPlaneSize[0] + mInputPlaneSize[1];
        planes[1].data_offset = mInputPlaneSize[0];
    }

    if(mInMemType == V4L2_MEMORY_USERPTR){
        planes[0].m.userptr = mInputBufferMap[index].planes[0].addr = (uint64_t)input->pInputVirt;
        planes[1].m.userptr = mInputBufferMap[index].planes[1].addr = (uint64_t)input->pInputVirt;
    }else if(mInMemType == V4L2_MEMORY_DMABUF){
        if(bPreProcess)
            planes[0].m.fd = mInputBufferMap[index].planes[0].fd = (int)(uint64_t)input->pInputPhys;
        else
            planes[0].m.fd = mInputBufferMap[index].planes[0].fd = input->fd;

        planes[1].m.fd = mInputBufferMap[index].planes[1].fd = input->fd;

    }

    stV4lBuf.index = index;
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if((int64_t)input->timestamp > 0){
        stV4lBuf.timestamp.tv_sec = (int64_t)input->timestamp/1000000;
        stV4lBuf.timestamp.tv_usec = (int64_t)input->timestamp - stV4lBuf.timestamp.tv_sec * 1000000;
    }
    stV4lBuf.memory = mInMemType;
    stV4lBuf.m.planes = &planes[0];
    stV4lBuf.length = kInputBufferPlaneNum;
    stV4lBuf.flags = v4l2_flags;

    ALOGV("V4l2Enc OUTPUT_MPLANE VIDIOC_QBUF index=%d,len=%d, ts=%lld,fd0=%d,fd1=%d\n",
        stV4lBuf.index, planes[0].bytesused, (long long)input->timestamp,planes[0].m.fd, planes[1].m.fd);

    result = ioctl(mFd, VIDIOC_QBUF, &stV4lBuf);
    if(result < 0){
        ALOGE("VIDIOC_QBUF failed, index=%d, result=%x",index,result);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    mInputBufferMap[index].at_device = true;

    dumpInputBuffer(input->fd, mInputPlaneSize[0]+ mInputPlaneSize[1]);
    mLock.unlock();

    if(!bInputStreamOn)
        startInputStream();

    if(eos)
        pDev->StopEncoder();

    return OK;
}
status_t V4l2Enc::dequeueInputBuffer()
{
    int result = 0;
    int input_id = -1;

    if(!bInputStreamOn || mState != RUNNING )
        return OK;

    Mutex::Autolock autoLock(mLock);
    if(!bInputStreamOn  || mState != RUNNING )
        return OK;

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kInputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(planes, 0, sizeof(planes));
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    stV4lBuf.memory = mInMemType;
    stV4lBuf.m.planes = planes;
    stV4lBuf.length = kInputBufferPlaneNum;
    result = ioctl(mFd, VIDIOC_DQBUF, &stV4lBuf);
    if(result < 0)
        return UNKNOWN_ERROR;

    ALOGV("V4l2Enc OUTPUT_MPLANE VIDIOC_DQBUF index=%d\n", stV4lBuf.index);

    if(stV4lBuf.index >= mInputFormat.bufferNum)
        return BAD_INDEX;

    if(!mInputBufferMap[stV4lBuf.index].at_device){
        ALOGE("dequeueInputBuffer index=%d, not at_device",stV4lBuf.index);
    }
    mInputBufferMap[stV4lBuf.index].at_device = false;
    input_id = mInputBufferMap[stV4lBuf.index].input_id;
    mInputBufferMap[stV4lBuf.index].input_id = -1;

    ALOGV("dequeueInputBuffer NotifyInputBufferUsed id=%d",input_id);
    NotifyInputBufferUsed(input_id);

    return OK;
}
status_t V4l2Enc::queueOutput(int buffer_id, int fd, unsigned long nVaddr)
{
    int result = 0;
    int32_t index = -1;
    
    if(!bFetchStarted || STOPPING == mState || FLUSHING == mState){
        ALOGV("queueOutput return 1");
        return OK;
    }

    mLock.lock();

    if(!bFetchStarted || STOPPING == mState || FLUSHING == mState){
        ALOGV("queueOutput return 2");
        mLock.unlock();
        return OK;
    }

    if(V4L2_MEMORY_MMAP == mOutMemType){
        //pass index from buffer_id
        index = buffer_id;
    }else{

        //try to get index
        for(int32_t i = 0; i < mOutputBufferMap.size(); i++){
            if((fd == mOutputBufferMap[i].plane.fd || nVaddr == mOutputBufferMap[i].plane.addr) 
                && !mInputBufferMap[i].at_device){
                index = i;
                break;
            }
        }

        //index not found
        if(index < 0){
            for(int32_t i = 0; i < mOutputBufferMap.size(); i++){
                if(-1 == mOutputBufferMap[i].plane.fd && !mOutputBufferMap[index].at_device){
                    mOutputBufferMap[i].plane.fd = fd;
                    mOutputBufferMap[i].plane.addr = nVaddr;
                    mOutputBufferMap[i].plane.offset = 0;
                    index = i;
                    break;
                }
            }
        }
    }

    if(index < 0){
        ALOGE("could not create mOutputBufferMap index, fd=%d,vaddr=%x, buffer_id=%d", fd, nVaddr, buffer_id);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    ALOGV("queueOutput index=%d, nVaddr=%x, fd=%d, buffer_id=%d", index, nVaddr, fd, buffer_id);

    mOutputBufferMap[index].picture_id = buffer_id;

    if(mOutputBufferMap[index].at_device){
        ALOGE("onQueueInputBuffer index=%d, at_device",index);
    }

    if(mOutputBufferMap[index].plane.fd != fd){
        ALOGE("mOutputBufferMap addr mismatch index=%d,%d,%d",
            index,mOutputBufferMap[index].plane.fd, fd);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kOutputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&planes[0], 0, sizeof(struct v4l2_plane));

    planes[0].length = mOutputBufferMap[index].plane.size;
    planes[0].data_offset = mOutputBufferMap[index].plane.offset;

    if(mOutMemType == V4L2_MEMORY_DMABUF){
        planes[0].m.fd = fd;
    }else if(mOutMemType == V4L2_MEMORY_USERPTR){
        planes[0].m.userptr = nVaddr;
    }else if(mOutMemType == V4L2_MEMORY_MMAP){
        planes[0].m.mem_offset = mOutputBufferMap[index].plane.offset;
        planes[0].data_offset = 0;
    }

    stV4lBuf.index = index;
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    stV4lBuf.memory = mOutMemType;
    stV4lBuf.m.planes = &planes[0];
    stV4lBuf.length = kOutputBufferPlaneNum;
    stV4lBuf.flags = 0;

    ALOGV("CAPTURE_MPLANE VIDIOC_QBUF index=%d\n",index);

    result = ioctl(mFd, VIDIOC_QBUF, &stV4lBuf);
    if(result < 0){
        ALOGE("VIDIOC_QBUF failed, index=%d",index);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    mOutputBufferMap[index].at_device = true;

    mLock.unlock();

    if(!bOutputStreamOn)
        startOutputStream();
    return OK;
}

status_t V4l2Enc::startInputStream()
{
    Mutex::Autolock autoLock(mLock);
    if(!bInputStreamOn){
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        if(0 == ioctl(mFd, VIDIOC_STREAMON, &buf_type)){
            bInputStreamOn = true;
            ALOGV("VIDIOC_STREAMON OUTPUT_MPLANE success");
        }
    }
    return OK;
}
status_t V4l2Enc::stopInputStream()
{
    Mutex::Autolock autoLock(mLock);
    if(bInputStreamOn){
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        if(0 == ioctl(mFd, VIDIOC_STREAMOFF, &buf_type)){
            bInputStreamOn = false;
            ALOGV("VIDIOC_STREAMOFF OUTPUT_MPLANE success");
        }
    }


    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        mInputBufferMap[i].at_device = false;
        mInputBufferMap[i].input_id = -1;
    }

    return OK;
}
status_t V4l2Enc::startOutputStream()
{
    Mutex::Autolock autoLock(mLock);
    if(!bOutputStreamOn){
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if(0 == ioctl(mFd, VIDIOC_STREAMON, &buf_type)){
            bOutputStreamOn = true;
            ALOGV("VIDIOC_STREAMON CAPTURE_MPLANE success");
        }
    }
    return OK;
}
status_t V4l2Enc::stopOutputStream()
{
    Mutex::Autolock autoLock(mLock);

    //call VIDIOC_STREAMOFF and ignore the result
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(mFd, VIDIOC_STREAMOFF, &buf_type);
    bOutputStreamOn = false;
    ALOGV("VIDIOC_STREAMOFF CAPTURE_MPLANE success");

    for (size_t i = 0; i < mOutputBufferMap.size(); i++) {
        mOutputBufferMap[i].at_device = false;
    }
    return OK;
}
status_t V4l2Enc::dequeueOutputBuffer()
{
    int result = 0;
    if(!bOutputStreamOn  || mState != RUNNING)
        return OK;

    Mutex::Autolock autoLock(mLock);
    if(!bOutputStreamOn  || mState != RUNNING)
        return OK;

    uint64_t ts = 0;
    uint32_t out_len = 0;
    int keyFrame = 0;
    uint32_t out_offset = 0;

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kOutputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(planes, 0, sizeof(planes));
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    stV4lBuf.memory = mOutMemType;
    stV4lBuf.m.planes = planes;
    stV4lBuf.length = kOutputBufferPlaneNum;
    result = ioctl(mFd, VIDIOC_DQBUF, &stV4lBuf);
    if(result < 0)
        return UNKNOWN_ERROR;

    if(stV4lBuf.index >= mOutputFormat.bufferNum)
        return BAD_INDEX;

    out_len = stV4lBuf.m.planes[0].bytesused;
    ts = (uint64_t)stV4lBuf.timestamp.tv_sec *1000000;
    ts += stV4lBuf.timestamp.tv_usec;
    keyFrame = (stV4lBuf.flags & V4L2_BUF_FLAG_KEYFRAME)?1:0;

    ALOGV("V4l2Enc CAPTURE_MPLANE VIDIOC_DQBUF index=%d, len=%d,ts=%lld,flag=%x",stV4lBuf.index, out_len, ts, stV4lBuf.flags);

    if(!bHasCodecData && mConverter != NULL){
        uint8_t* vAddr = (uint8_t*)mOutputBufferMap[stV4lBuf.index].plane.addr;
        if(OK ==  mConverter->CheckSpsPps(vAddr, out_len, &out_offset))
            bHasCodecData = true;
    }

    int blockId = 0;
    int fd;
    unsigned long nOutPhy;
    unsigned long nOutVirt;

    if(mOutMemType == V4L2_MEMORY_MMAP){
        if (OK == FetchOutputBuffer(&blockId, &fd, &nOutPhy, &nOutVirt, &out_len))
            memcpy((void*)nOutVirt, (void*)mOutputBufferMap[stV4lBuf.index].plane.addr, out_len);
        else
            return UNKNOWN_ERROR;
        dumpOutputBuffer((void*)nOutVirt, out_len);
    }

    if(stV4lBuf.flags & V4L2_BUF_FLAG_LAST){
        ALOGV("get last frame");
        NotifyEOS();
        bPollStarted = false;
        bFetchStarted = false;
        mState = STOPPED;
        mOutputBufferMap[stV4lBuf.index].at_device = false;
        return OK;
    }

    ALOGV("dequeueOutputBuffer NotifyOutputFrameReady blockId=%d,len=%d,ts=%lld,keyFrame=%x,out_offset=%d",blockId, out_len, ts, keyFrame,out_offset);
    NotifyOutputFrameReady(blockId, out_len, ts, keyFrame, out_offset);

    mFrameOutNum ++;
    mOutputBufferMap[stV4lBuf.index].at_device = false;
    return OK;
}
status_t V4l2Enc::onDequeueEvent()
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
                    //handleFormatChanged();
                }
                break;
            case V4L2_EVENT_EOS:
                ALOGV("get V4L2_EVENT_EOS event, wait for last frame");
                break;
            case V4L2_EVENT_DECODE_ERROR:
                NotifyError(UNKNOWN_ERROR);//send error event
                break;
            default:
                break;
        }
    }

    return OK;
}

status_t V4l2Enc::DoSetConfig(EncConfig index, void* pConfig) {

    status_t ret = OK;

    if (!pConfig)
        return BAD_VALUE;

    switch (index) {
        case ENC_CONFIG_BIT_RATE:
        {
            int32_t tar = (*(int*)pConfig);
            if(mEncParam.nBitRate > 0 && mEncParam.nBitRate != tar){
                Mutex::Autolock autoLock(mLock);
                ret = pDev->SetEncoderBitrate(V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, tar);
            }
            ALOGV("SetConfig ENC_CONFIG_BIT_RATE src=%d,tar=%d",mEncParam.nBitRate,tar);
            mEncParam.nBitRate = tar;
            break;
        }
        case ENC_CONFIG_INTRA_REFRESH:
        {
            if(0 == (*(int*)pConfig))
                bSyncFrame = false;
            else
                bSyncFrame = true;
            break;
        }
        case ENC_CONFIG_COLOR_FORMAT:
        {
            mInputFormat.pixelFormat = (*(int*)pConfig);
            break;
        }
        default:
            ret = BAD_VALUE;
            break;
    }
    return ret;
}

status_t V4l2Enc::DoGetConfig(EncConfig index, void* pConfig) {
    if (!pConfig)
        return BAD_VALUE;

    status_t ret = OK;

    return ret;
}
#if 0
status_t V4l2Enc::handleFormatChanged() {

    status_t ret = OK;

    {
        Mutex::Autolock autoLock(mLock);

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

        mOutputFormat.width = Align(format.fmt.pix_mp.width, FRAME_ALIGN);
        mOutputFormat.height = Align(format.fmt.pix_mp.height, FRAME_ALIGN);
        mOutputFormat.interlaced = ((format.fmt.pix_mp.field == V4L2_FIELD_INTERLACED) ? true: false);

        mOutputPlaneSize[0] = format.fmt.pix_mp.plane_fmt[0].sizeimage;
        mOutputPlaneSize[1] = format.fmt.pix_mp.plane_fmt[1].sizeimage;

        struct v4l2_control ctl;
        memset(&ctl, 0, sizeof(struct v4l2_control));

        ctl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
        result = ioctl(mFd, VIDIOC_G_CTRL, &ctl);
        if(result < 0)
            return UNKNOWN_ERROR;

        mOutputFormat.minBufferNum = ctl.value;
        if(mOutputFormat.minBufferNum > mOutputFormat.bufferNum)
            mOutputFormat.bufferNum = mOutputFormat.minBufferNum;

        struct v4l2_crop crop;
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        result = ioctl (mFd, VIDIOC_G_CROP, &crop);
        if(result < 0)
            return UNKNOWN_ERROR;

        mOutputFormat.rect.right = crop.c.width;
        mOutputFormat.rect.bottom = crop.c.height;
        mOutputFormat.rect.top = crop.c.top;
        mOutputFormat.rect.left = crop.c.left;
    }

    outputFormatChanged();

    ALOGV("getOutputVideoInfo w=%d,h=%d,buf cnt=%d, buffer size[0]=%d,size[1]=%d",
        mOutputFormat.width, mOutputFormat.height, mOutputFormat.minBufferNum, mOutputPlaneSize[0], mOutputPlaneSize[1]);
    return OK;
}
#endif

status_t V4l2Enc::onFlush()
{
    status_t ret = UNKNOWN_ERROR;
    ALOGV("onFlush BEGIN");
    int pre_state;
    {
    Mutex::Autolock autoLock(mLock);
    pre_state = mState;
    mState = FLUSHING;
    }

    ret = stopInputStream();
    if(ret != OK)
        return ret;

    ret = stopOutputStream();
    if(ret != OK)
        return ret;

    mState = pre_state;
    ALOGV("onFlush END ret=%d",ret);

    return ret;
}
status_t V4l2Enc::onStop()
{
    status_t ret = UNKNOWN_ERROR;
  
    ALOGV("onStop BEGIN");
    {
    Mutex::Autolock autoLock(mLock);
    mState = STOPPING;
    }
    ret = onFlush();
    if(ret != OK)
        return ret;

    ret = destroyFetchThread();
    if(ret != OK)
        return ret;

    ALOGV("onStop destroyFetchThread success");
    ret = destroyPollThread();
    if(ret != OK)
        return ret;

    ALOGV("onStop destroyPollThread success");

    ret = destroyInputBuffers();
    if(ret != OK)
        return ret;

    ret = freeOutputBuffers();
    if(ret != OK)
        return ret;

    ret = destroyOutputBuffers();
    if(ret != OK)
        return ret;

    if(mConverter != NULL){
        mConverter->Destroy();
        delete mConverter;
    }
    ALOGV("onStop END ret=%d",ret);

    mState = STOPPED;
    return ret;
}
status_t V4l2Enc::onDestroy()
{
    status_t ret = UNKNOWN_ERROR;

    ALOGV("onDestroy");
    Mutex::Autolock autoLock(mLock);

    if(pDev == NULL)
        return UNKNOWN_ERROR;

    if(mFd > 0){
        pDev->Close();
        mFd = 0;
    }

    if(pDev != NULL)
        delete pDev;
    pDev = NULL;

    ALOGV("onDestroy END");
    return OK;
}
void V4l2Enc::initEncInputParamter(EncInputParam *pInPara) {
    if(pInPara == NULL)
        return;

    memset(&mEncParam, 0, sizeof(V4l2EncInputParam));

    mTargetFps = pInPara->nFrameRate;
    mEncParam.nRotAngle = pInPara->nRotAngle;
    mEncParam.nBitRate = pInPara->nBitRate;
    mEncParam.nBitRateMode = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
    mEncParam.nGOPSize = pInPara->nGOPSize;
    mEncParam.nIntraFreshNum = pInPara->nRefreshIntra;
    mEncParam.nProfile = pInPara->nProfile;
    mEncParam.nLevel = pInPara->nLevel;

    mInputFormat.pixelFormat = pInPara->eColorFormat;
    mWidth = pInPara->nPicWidth;
    mHeight = pInPara->nPicHeight;

    mInputFormat.bufferNum = DEFAULT_INPUT_BUFFER_COUNT;
    mInputFormat.bufferSize = mWidth * mHeight * 3/2;
    mInputFormat.width = mWidth;
    mInputFormat.height = mHeight;
    mInputFormat.interlaced = false;
    mInputFormat.pixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;

    mOutputFormat.width = mWidth;
    mOutputFormat.height = mHeight;
    mOutputFormat.minBufferNum = 2;
    mOutputFormat.bufferNum = kDefaultOutputBufferCount;
    mOutputFormat.interlaced = false;

    mIsoColorAspect.colourPrimaries = pInPara->sColorAspects.primaries;
    mIsoColorAspect.transferCharacteristics = pInPara->sColorAspects.transfer;
    mIsoColorAspect.matrixCoeffs = pInPara->sColorAspects.matrixCoeffs;
    mIsoColorAspect.fullRange = pInPara->sColorAspects.fullRange;

    ALOGD("initEncInputParamter nRotAngle=%d,nBitRate=%d,nGOPSize=%d,nRefreshIntra=%d,mTargetFps=%d",
        pInPara->nRotAngle, pInPara->nBitRate, pInPara->nGOPSize, pInPara->nRefreshIntra,mTargetFps);
    
    return;
}
status_t V4l2Enc::getCodecData(uint8_t** pCodecData, uint32_t* size) {
    ALOGV("getCodecData");
    if(bHasCodecData && mConverter != NULL)
        return mConverter->GetSpsPpsPtr(pCodecData, size);
    else
        return UNKNOWN_ERROR;
}
bool V4l2Enc::checkIfPreProcessNeeded(int pixelFormat) 
{
    switch (pixelFormat) {
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
        ALOGV("bPreProcess TRUE");
        bPreProcess = true;
        return true;
    default:
        bPreProcess = false;
        return false;
    }
}

status_t V4l2Enc::allocateOutputBuffer(int32_t index)
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    uint8_t * ptr = NULL;
    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kOutputBufferPlaneNum];

    if(index > mOutputFormat.bufferNum)
        return UNKNOWN_ERROR;

    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    stV4lBuf.memory = V4L2_MEMORY_MMAP;
    stV4lBuf.index = index;
    stV4lBuf.length = kOutputBufferPlaneNum;
    stV4lBuf.m.planes = planes;

    result = ioctl(mFd, VIDIOC_QUERYBUF, &stV4lBuf);
    if(result < 0)
        return UNKNOWN_ERROR;
        
    planes[0].length = mOutputFormat.bufferSize;

    ptr = (uint8_t *)mmap(NULL, planes[0].length,
        PROT_READ | PROT_WRITE, /* recommended */
        MAP_SHARED,             /* recommended */
        mFd, planes[0].m.mem_offset);
    if(ptr != MAP_FAILED){
       mOutputBufferMap[index].plane.addr = (uint64_t)ptr;
    }else
        return UNKNOWN_ERROR;

    mOutputBufferMap[index].plane.fd = -1;
    mOutputBufferMap[index].plane.size = planes[0].length;
    mOutputBufferMap[index].plane.offset = planes[0].m.mem_offset;
    mOutputBufferMap[index].at_device = false;
    ALOGV("allocateOutputBuffer index=%d,size=%d,offset=%x,addr=%x",
        index,planes[0].length, planes[0].m.mem_offset, ptr);
    return OK;
}
status_t V4l2Enc::freeOutputBuffers()
{
    Mutex::Autolock autoLock(mLock);

    if(mOutputBufferMap.empty())
        return OK;

    for(int32_t i = 0; i < mOutputFormat.bufferNum; i++){
        if(0 != mOutputBufferMap[i].plane.addr){
            ALOGV("munmap %p",mOutputBufferMap[i].plane.addr);
            munmap((void*)mOutputBufferMap[i].plane.addr,mOutputBufferMap[i].plane.size);
        }

        if(mOutputBufferMap[i].plane.fd > 0)
            close(mOutputBufferMap[i].plane.fd);

        mOutputBufferMap[i].plane.offset = 0;
        mOutputBufferMap[i].at_device = false;
    }
    mOutputBufferMap.clear();
    return OK;
}
void V4l2Enc::ParseVpuLogLevel()
{
    int level=0;
    FILE* fpVpuLog;
    nDebugFlag = 0;
    
    fpVpuLog=fopen(VPU_ENCODER_LOG_LEVELFILE, "r");
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
void V4l2Enc::dumpInputBuffer(int fd, uint32_t size)
{
    FILE * pfile = NULL;
    void* buf = NULL;

    if(fd <= 0){
        ALOGV("dumpInputBuffer invalid fd");
        return;
    }

    if(!(nDebugFlag & DUMP_ENC_FLAG_INPUT))
        return;

    if(mFrameOutNum < 200)
        pfile = fopen(DUMP_ENC_INPUT_FILE,"ab");

    if(pfile){
        buf = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
        fwrite(buf,1,size,pfile);
        ALOGV("dumpInputBuffer write %d",size);
        munmap(buf, size);
        fclose(pfile);
    }else
        ALOGE("dumpInputBuffer failed to open %s",DUMP_ENC_INPUT_FILE);
    return;
}
void V4l2Enc::dumpOutputBuffer(void* buf, uint32_t size)
{
    FILE * pfile = NULL;
    if(buf == NULL)
        return;

    if(!(nDebugFlag & DUMP_ENC_FLAG_OUTPUT))
        return;

    if(mFrameOutNum < 200)
        pfile = fopen(DUMP_ENC_OUTPUT_FILE,"ab");

    if(pfile){
        fwrite(buf,1,size,pfile);
        fclose(pfile);
    }
    return;
}

VideoEncoderBase * CreateVideoEncoderInstance(const char* mime) {
    return static_cast<VideoEncoderBase *>(new V4l2Enc(mime));
}
}
