/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "IsiPreProcess"

#include "IsiPreProcess.h"
#include <linux/videodev2.h>
#include "graphics_ext.h"
#include "C2_imx.h"

namespace android {
#define Align(ptr,align)    (((uint32_t)(ptr)+(align)-1)/(align)*(align))

status_t IsiPreProcess::onInit(){
    status_t ret = UNKNOWN_ERROR;

    if(pDev == NULL){
        pDev = new V4l2Dev();
    }
    if(pDev == NULL)
        return ret;

    mFd = pDev->Open(V4L2_DEV_ISI);
    ALOGV("onInit pV4l2Dev->Open fd=%d",mFd);

    if(mFd < 0){
        ALOGE("onInit open isi device failed");
        return ret;
    }

    mInMemType = V4L2_MEMORY_DMABUF;
    mOutMemType = V4L2_MEMORY_DMABUF;
    mAddOutCnt = 0;

    mWidthAlign = 1;
    mHeightAlign = 1;

    struct v4l2_frmsizeenum info;
    memset(&info, 0, sizeof(v4l2_frmsizeenum));
    if(OK == pDev->GetFormatFrameInfo(mOutFormat, &info)){
        mWidthAlign = info.stepwise.step_width;
        mHeightAlign = info.stepwise.step_height;
    }
    //align with v4l2 encoder
    mWidthAlign = 16;

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

    ret = prepareInputBuffers();
    if(ret != OK){
        ALOGE("prepareInputBuffers failed");
        return ret;
    }
    
    ret = prepareOutputParams();
    if(ret != OK){
        ALOGE("SetInputFormats failed");
        return ret;
    }

    ret = SetOutputFormats();
    if(ret != OK){
        ALOGE("SetOutputFormats failed");
        return ret;
    }

    if (OK != AllocateProcessBuffers(DEFAULT_PROCESS_BUFFER_NUM, kOutputBufferPlaneNum, &mOutputPlaneSize[0]))
        return BAD_VALUE;

    ret = prepareOutputBuffers();
    if(ret != OK){
        ALOGE("prepareInputBuffers failed");
        return ret;
    }

    bPollStarted = false;
    bInputStreamOn = false;
    bOutputStreamOn = false;
    bSyncFrame = false;
    mAsync = true;

    ret = createPollThread();

    return ret;
}

typedef struct{
    uint32_t v4l2_format;
    int pixel_format;
}V4L2_FORMAT_TABLE;

static V4L2_FORMAT_TABLE color_format_table[]={
{V4L2_PIX_FMT_RGBA32, HAL_PIXEL_FORMAT_RGBA_8888},
{V4L2_PIX_FMT_RGBA32, HAL_PIXEL_FORMAT_RGBX_8888},
{V4L2_PIX_FMT_RGB565, HAL_PIXEL_FORMAT_RGB_565},
{V4L2_PIX_FMT_NV12, HAL_PIXEL_FORMAT_YCbCr_420_SP},
};

uint32_t IsiPreProcess::getV4l2Format(uint32_t color_format)
{
    uint32_t i=0;

    for(i = 0; i < sizeof(color_format_table)/sizeof(V4L2_FORMAT_TABLE);i++){
        if(color_format == color_format_table[i].pixel_format){
            return color_format_table[i].v4l2_format;
        }
    }

    return 0;
}
status_t IsiPreProcess::prepareInputParams()
{
    status_t ret = UNKNOWN_ERROR;
    Mutex::Autolock autoLock(mLock);

    sInFormat.width = Align(sInFormat.width, mWidthAlign);
    sInFormat.height = sInFormat.height;
    sInFormat.stride = Align(sInFormat.stride, mWidthAlign);

    mInFormat = getV4l2Format(sInFormat.format);

    ALOGV("prepareInputParams width=%d,height=%d,format=%x",sInFormat.width,sInFormat.height, mInFormat);

    if(mInFormat == V4L2_PIX_FMT_RGBA32){
        sInFormat.bufferSize = sInFormat.width * sInFormat.height * 4;
    }else{
        ALOGE("mInFormat is not RGBA");
        return ret;
    }

    return OK;
}

status_t IsiPreProcess::prepareOutputParams()
{
    status_t ret = UNKNOWN_ERROR;
    Mutex::Autolock autoLock(mLock);

    sOutFormat.width = Align(sInFormat.width, mWidthAlign);
    sOutFormat.height = sInFormat.height;
    sOutFormat.stride = Align(sInFormat.stride, mWidthAlign);
    sOutFormat.format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    mOutFormat = getV4l2Format(sOutFormat.format);

    ALOGV("sOutFormat width=%d,height=%d,format=%x",sOutFormat.width,sOutFormat.height, mOutFormat);

    if(mOutFormat == V4L2_PIX_FMT_NV12){
        mOutputPlaneSize[0] = Align(sOutFormat.width, mWidthAlign) * Align(sOutFormat.height, mHeightAlign);
        mOutputPlaneSize[1] = mOutputPlaneSize[0]/2;
        sOutFormat.bufferSize = mOutputPlaneSize[0] + mOutputPlaneSize[1];
    }else{
        ALOGE("mOutFormat is not NV12");
        return ret;
    }

    return OK;
}
status_t IsiPreProcess::SetInputFormats()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    format.fmt.pix_mp.num_planes = kInputBufferPlaneNum;
    format.fmt.pix_mp.pixelformat = mInFormat;
    format.fmt.pix_mp.width = sInFormat.width;
    format.fmt.pix_mp.height = sInFormat.height;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = sInFormat.bufferSize;
    format.fmt.pix_mp.plane_fmt[0].bytesperline = sInFormat.stride;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;

    result = ioctl (mFd, VIDIOC_S_FMT, &format);
    if(result != 0){
        ALOGE("IsiPreProcess VIDIOC_S_FMT OUTPUT_MPLANE failed");
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

    if(format.fmt.pix_mp.width != sInFormat.width ||
        format.fmt.pix_mp.height != sInFormat.height){
        ALOGE("SetInputFormats resolution mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].sizeimage != sInFormat.bufferSize){
        ALOGE("SetInputFormats bufferSize mismatch sizeimage=%d,buffersize=%d", format.fmt.pix_mp.plane_fmt[0].sizeimage, sInFormat.bufferSize);
        return UNKNOWN_ERROR;
    }

    ALOGV("SetInputFormats success mInFormat=%x,width=%d,height=%d",mInFormat, sInFormat.width, sInFormat.height);
    return OK;
}
status_t IsiPreProcess::SetOutputFormats()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.num_planes = kOutputBufferPlaneNum;
    format.fmt.pix_mp.pixelformat = mOutFormat;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = mOutputPlaneSize[0];
    format.fmt.pix_mp.plane_fmt[0].bytesperline = sOutFormat.stride;
    format.fmt.pix_mp.plane_fmt[1].sizeimage = mOutputPlaneSize[1];
    format.fmt.pix_mp.plane_fmt[1].bytesperline = sOutFormat.stride;
    format.fmt.pix_mp.width = sOutFormat.width;
    format.fmt.pix_mp.height = sOutFormat.height;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;
    ALOGV("SetOutputFormats mOutFormat=%x,w=%d,h=%d,stride=%d,size0=%d,size1=%d",
        mOutFormat,sOutFormat.width,sOutFormat.height,sOutFormat.stride,mOutputPlaneSize[0],mOutputPlaneSize[1]);
    result = ioctl (mFd, VIDIOC_S_FMT, &format);
    if(result != 0){
        ALOGE("IsiPreProcess VIDIOC_S_FMT CAPTURE_MPLANE failed err=%d",result);
        return UNKNOWN_ERROR;
    }

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    result = ioctl (mFd, VIDIOC_G_FMT, &format);
    if(result != 0)
        return UNKNOWN_ERROR;

    if(format.fmt.pix_mp.pixelformat != mOutFormat){
        ALOGE("SetOutputFormats mInFormat mismatch");
        return UNKNOWN_ERROR;
    }

    if( format.fmt.pix_mp.width != sOutFormat.width ||
        format.fmt.pix_mp.height != sOutFormat.height){
        ALOGE("SetOutputFormats resolution mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].bytesperline != sOutFormat.stride){
        ALOGE("SetOutputFormats stride mismatch");
        return UNKNOWN_ERROR;
    }

    if(format.fmt.pix_mp.plane_fmt[0].sizeimage != mOutputPlaneSize[0] ||
        format.fmt.pix_mp.plane_fmt[1].sizeimage != mOutputPlaneSize[1]){
        ALOGE("SetOutputFormats bufferSize mismatch");
        return UNKNOWN_ERROR;
    }

    return OK;
}
status_t IsiPreProcess::prepareInputBuffers()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 16;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = mInMemType;
    ALOGV("prepareInputBuffers VIDIOC_REQBUFS bufferNum=%d",reqbufs.count);
    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        ALOGE("VIDIOC_REQBUFS failed result=%d",result);
        return UNKNOWN_ERROR;
    }
    ALOGV("prepareInputBuffers reqbufs.count=%d",reqbufs.count);

    return OK;
}
status_t IsiPreProcess::prepareOutputBuffers()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = DEFAULT_PROCESS_BUFFER_NUM;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = mOutMemType;
    ALOGV("prepareOutputBuffers VIDIOC_REQBUFS bufferNum=%d",reqbufs.count);
    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0){
        ALOGE("VIDIOC_REQBUFS failed result=%d",result);
        return UNKNOWN_ERROR;
    }

    ALOGV("prepareOutputBuffers reqbufs.count=%d",reqbufs.count);

    return OK;
}
status_t IsiPreProcess::HandlePollThread()
{
    status_t ret = OK;
    int32_t poll_ret = 0;

    while(bPollStarted){
        ALOGV("pollThreadHandler BEGIN");
        poll_ret = pDev->Poll();
        ALOGV("pollThreadHandler poll_ret=%x",poll_ret);

        if(!bPollStarted)
            break;

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
status_t IsiPreProcess::createPollThread()
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

status_t IsiPreProcess::destroyPollThread()
{
    ALOGV("destroyPollThread BEGIN");

    if(bPollStarted){
        bPollStarted = false;
        ALOGV("destroyPollThread bPollStarted FALSE");
        usleep(1000);
        pthread_join(mPollThread, NULL);
    }
    return OK;
}
status_t IsiPreProcess::dequeueInputBuffer()
{
    int result = 0;
    int input_id = -1;
    Mutex::Autolock autoLock(mLock);
    if(!bInputStreamOn)
        return OK;

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane plane[kInputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(plane, 0, sizeof(v4l2_plane));
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    stV4lBuf.memory = mInMemType;
    stV4lBuf.m.planes = plane;
    stV4lBuf.length = kInputBufferPlaneNum;
    result = ioctl(mFd, VIDIOC_DQBUF, &stV4lBuf);
    if(result < 0)
        return UNKNOWN_ERROR;

    ALOGV("OUTPUT_MPLANE VIDIOC_DQBUF index=%d\n", stV4lBuf.index);

    if(stV4lBuf.index >= 16)
        return BAD_INDEX;

    ALOGV("dequeueInputBuffer NotifyInputBufferUsed id=%d",stV4lBuf.index);


    int inId = 0;
    int inFd = -1;
    uint32_t inFlag = 0;
    unsigned long inPhys;

    if(OK == ProcessFrameGetNode(&sInMemInfo, stV4lBuf.index, &inPhys, &inId, &inFd, &inFlag)){
        ProcessFrameClear(&sInMemInfo, stV4lBuf.index);
        NotifyProcessInputUsed(inId);
    }

    return OK;
}
status_t IsiPreProcess::dequeueOutputBuffer()
{
    int result = 0;
    Mutex::Autolock autoLock(mLock);
    if(!bOutputStreamOn)
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

    if(stV4lBuf.index >= DEFAULT_PROCESS_BUFFER_NUM)
        return BAD_INDEX;

    out_len = stV4lBuf.m.planes[0].bytesused;
    ts = (uint64_t)stV4lBuf.timestamp.tv_sec *1000000;
    ts += stV4lBuf.timestamp.tv_usec;
    keyFrame = (stV4lBuf.flags & V4L2_BUF_FLAG_KEYFRAME)?1:0;

    ALOGV("CAPTURE_MPLANE VIDIOC_DQBUF index=%d, len=%d,ts=%lld,flag=%x",stV4lBuf.index, out_len, (long long)ts, stV4lBuf.flags);


    int outId = 0;
    int outFd = -1;
    uint32_t outFlag = 0;
    unsigned long outPhys;

    if(OK == ProcessFrameGetNode(&sOutMemInfo, stV4lBuf.index, &outPhys, &outId, &outFd, &outFlag)){
        //ProcessFrameClear(&sOutMemInfo, stV4lBuf.index);
        if(keyFrame)
            outFlag = FLAG_SYNC_FRAME;
        NotifyProcessDone(outId, outFlag);
        ALOGV("NotifyProcessDone blockId=%d,len=%d,ts=%lld,keyFrame=%x,out_offset=%d",outId, out_len, (long long)ts, keyFrame,out_offset);

    }

    return OK;
}
status_t IsiPreProcess::queueInput()
{
    //return error for no more input
    if (0 == mInputQueue.size()){
        return OK;
    }

    status_t ret = OK;
    int result = 0;
    int inIndex = 0;;
    int inId = 0;
    int inFd = -1;
    unsigned long inPhys;
    uint32_t flag = 0;

    inIndex = mInputQueue.front();

    ret = ProcessFrameGetNode(&sInMemInfo, inIndex, &inPhys, &inId, &inFd, &flag);
    if(ret != OK){
        return ret;
    }

    uint32_t v4l2_flags = 0;

    //TODO: handle eos, sync frame flag, timestamps
    if(flag & C2FrameData::FLAG_END_OF_STREAM && inFd < 0){
        return BAD_VALUE;
    }

    if(flag & C2FrameData::FLAG_END_OF_STREAM)
        v4l2_flags |= V4L2_BUF_FLAG_LAST;

    if(bSyncFrame/*flag & FLAG_SYNC_FRAME*/){
        v4l2_flags |= V4L2_BUF_FLAG_KEYFRAME;
        bSyncFrame = false;
    }

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane plane[kInputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&plane[0], 0, kInputBufferPlaneNum * sizeof(struct v4l2_plane));

    plane[0].bytesused = sInFormat.bufferSize;
    plane[0].length = sInFormat.bufferSize;
    plane[0].data_offset = 0;

    if(mInMemType == V4L2_MEMORY_DMABUF){
        plane[0].m.fd = inFd;
    }

    stV4lBuf.index = inIndex;
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    //stV4lBuf.timestamp.tv_sec = input->timestamp/1000000;
    //stV4lBuf.timestamp.tv_usec = input->timestamp - stV4lBuf.timestamp.tv_sec * 1000000;
    stV4lBuf.memory = mInMemType;
    stV4lBuf.m.planes = &plane[0];
    stV4lBuf.length = kInputBufferPlaneNum;
    stV4lBuf.flags = v4l2_flags;

    ALOGV("OUTPUT_MPLANE VIDIOC_QBUF index=%d,len=%d,fd=%d, ts=%lld, v4l2_flags=%x\n",
        stV4lBuf.index, plane[0].bytesused, plane[0].m.fd, (long long)0, v4l2_flags);

    result = ioctl(mFd, VIDIOC_QBUF, &stV4lBuf);
    if(result < 0){
        ALOGE("OUTPUT_MPLANE VIDIOC_QBUF failed, index=%d, result=%x",inIndex,result);
        return UNKNOWN_ERROR;
    }

    mInputQueue.pop();

    if(!bInputStreamOn)
        startInputStream();

    return OK;
}
status_t IsiPreProcess::queueOutput()
{
    //return error for no more input
    if (0 == mOutputQueue.size()){
        return OK;
    }

    status_t ret = OK;
    int result = 0;
    int outIndex = 0;
    int outId = 0;
    int outFd = -1;
    uint32_t outFlag = 0;
    unsigned long outPhys;
    
    outIndex = mOutputQueue.front();

    ret = ProcessFrameGetNode(&sOutMemInfo, outIndex, &outPhys, &outId, &outFd, &outFlag);
    if(ret != OK){
        return ret;
    }

    uint32_t v4l2_flags = 0;

    //TODO: handle eos, sync frame flag, timestamps
    #if 0
    bool eos = input->eos;

    if(eos)
        v4l2_flags |= V4L2_BUF_FLAG_LAST;

    if((int64_t)input->timestamp >= 0)
        v4l2_flags |= (V4L2_BUF_FLAG_TIMESTAMP_MASK | V4L2_BUF_FLAG_TIMESTAMP_COPY);

    if(bSyncFrame){
        v4l2_flags |= V4L2_BUF_FLAG_KEYFRAME;
        bSyncFrame = false;
    }
    #endif

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[kOutputBufferPlaneNum];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&planes[0], 0, kOutputBufferPlaneNum * sizeof(struct v4l2_plane));

    planes[0].bytesused = mOutputPlaneSize[0];
    planes[0].length = mOutputPlaneSize[0];
    planes[0].data_offset = 0;
    
    planes[1].bytesused = mOutputPlaneSize[1];
    planes[1].length = mOutputPlaneSize[1];
    planes[1].data_offset = 0;

    if(mInMemType == V4L2_MEMORY_DMABUF){
        planes[0].m.fd = (int)outPhys;
        planes[1].m.fd = outFd;
    }

    stV4lBuf.index = outIndex;
    stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    //stV4lBuf.timestamp.tv_sec = input->timestamp/1000000;
    //stV4lBuf.timestamp.tv_usec = input->timestamp - stV4lBuf.timestamp.tv_sec * 1000000;
    stV4lBuf.memory = mOutMemType;
    stV4lBuf.m.planes = &planes[0];
    stV4lBuf.length = kOutputBufferPlaneNum;
    stV4lBuf.flags = v4l2_flags;

    ALOGV("CAPTURE_MPLANE VIDIOC_QBUF index=%d,len0=%d,len1=%d, fd0=%d,fd1=%d ts=%lld\n",
        stV4lBuf.index, planes[0].length, planes[1].length, planes[0].m.fd, planes[1].m.fd, (long long)0);

    result = ioctl(mFd, VIDIOC_QBUF, &stV4lBuf);
    if(result < 0){
        ALOGE("CAPTURE_MPLANE VIDIOC_QBUF failed, index=%d, result=%x",outIndex,result);
        return UNKNOWN_ERROR;
    }

    mAddOutCnt++;
    mOutputQueue.pop();

    if(!bOutputStreamOn && mAddOutCnt == DEFAULT_PROCESS_BUFFER_NUM)
        startOutputStream();

    return OK;
}
// static
void *IsiPreProcess::PollThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<IsiPreProcess *>(me)->HandlePollThread();
}

status_t IsiPreProcess::onProcess() {

    status_t ret = OK;
    ret = queueInput();
    if(ret != OK)
        ALOGE("queueInput failed ret=%x",ret);

    while (mOutputQueue.size() > 0) {
        ret = queueOutput();
        if(ret != OK)
            ALOGE("queueOutput failed ret=%x",ret);
    }
    return ret;
}
status_t IsiPreProcess::startInputStream()
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
status_t IsiPreProcess::stopInputStream()
{
    Mutex::Autolock autoLock(mLock);
    if(bInputStreamOn){
        enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        if(0 == ioctl(mFd, VIDIOC_STREAMOFF, &buf_type)){
            bInputStreamOn = false;
            ALOGV("VIDIOC_STREAMOFF OUTPUT_MPLANE success");
        }
    }

    return OK;
}
status_t IsiPreProcess::startOutputStream()
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
status_t IsiPreProcess::stopOutputStream()
{
    Mutex::Autolock autoLock(mLock);

    //call VIDIOC_STREAMOFF and ignore the result
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(mFd, VIDIOC_STREAMOFF, &buf_type);
    bOutputStreamOn = false;
    ALOGV("VIDIOC_STREAMOFF CAPTURE_MPLANE success");

    return OK;
}
status_t IsiPreProcess::destroyInputBuffers()
{
    Mutex::Autolock autoLock(mLock);

    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;//use mmap to free buffer

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0)
        return UNKNOWN_ERROR;

    ALOGV("destroyInputBuffers success");
    return OK;
}
status_t IsiPreProcess::destroyOutputBuffers()
{
    Mutex::Autolock autoLock(mLock);

    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;//use mmap to free buffer

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);

    if(result != 0)
        return UNKNOWN_ERROR;

    ALOGV("destroyOutputBuffers success");
    return OK;
}
status_t IsiPreProcess::onFlush()
{
    stopInputStream();
    stopOutputStream();
    mAddOutCnt = 0;
    return OK;
}
status_t IsiPreProcess::onDestroy()
{
    if(mAddOutCnt != 0)
        onFlush();

    destroyPollThread();

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

    return OK;
}
status_t IsiPreProcess::onStart()
{
    createPollThread();
    return OK;
}
status_t IsiPreProcess::onStop()
{
    onFlush();
    destroyPollThread();
    return OK;
}
status_t IsiPreProcess::DoSetConfig(ProcessConfig index, void* pConfig) {

    status_t ret = BAD_VALUE;

    if (!pConfig)
        return ret;
    
    switch (index) {
        case PROCESS_CONFIG_INTRA_REFRESH: {
            if(0 == (*(int*)pConfig))
                bSyncFrame = false;
            else
                bSyncFrame = true;
            ret = OK;
            break;
        }
        default: {
            break;
        }
    }

    return ret;
}

status_t IsiPreProcess::DoGetConfig(ProcessConfig index, void* pConfig) {
    return BAD_VALUE; // not support any index yet
}
ProcessBase * CreatePreProcessInstance() {
    return static_cast<ProcessBase *>(new IsiPreProcess());
}
}
