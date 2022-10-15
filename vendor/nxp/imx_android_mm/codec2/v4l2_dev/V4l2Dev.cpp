/**
 *  Copyright 2018-2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */
//#define LOG_NDEBUG 0
#define LOG_TAG "V4l2Dev"
#include "V4l2Dev.h"

#include <linux/videodev2.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>

#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include "graphics_ext.h"
#include "Imx_ext.h"
#include "C2Config.h"

namespace android {

#define VPU_DEC_NODE "/dev/video12"
#define VPU_ENC_NODE "/dev/video13"

#define V4L2_CID_USER_FRAME_DEPTH (V4L2_CID_USER_BASE + 0x1200)
#define V4L2_CID_USER_TS_THRESHOLD     (V4L2_CID_USER_BASE + 0x1101)
#define V4L2_CID_USER_BS_L_THRESHOLD	(V4L2_CID_USER_BASE + 0x1102)
#define V4L2_CID_USER_BS_H_THRESHOLD	(V4L2_CID_USER_BASE + 0x1103)

#define V4L2_CID_USER_FRAME_DIS_REORDER (V4L2_CID_USER_BASE + 0x1300)

#define MAX_VIDEO_SEARCH_NODE (20)

V4l2Dev::V4l2Dev()
{
    memset((char*)sDevName, 0, MAX_DEV_NAME_LEN);
    nFd = -1;
    nEventFd = -1;
}
int32_t V4l2Dev::Open(V4l2DEV_TYPE type){

    if(OK != SearchName(type))
        return -1;
    
    nFd = open ((char*)sDevName, O_RDWR | O_NONBLOCK);

    if(nFd > 0){
        struct v4l2_event_subscription  sub;
        memset(&sub, 0, sizeof(struct v4l2_event_subscription));

        sub.type = V4L2_EVENT_SOURCE_CHANGE;
        ioctl(nFd, VIDIOC_SUBSCRIBE_EVENT, &sub);

        sub.type = V4L2_EVENT_EOS;
        ioctl(nFd, VIDIOC_SUBSCRIBE_EVENT, &sub);

        if(type == V4L2_DEV_DECODER){
            sub.type = V4L2_EVENT_SKIP;
            ioctl(nFd, VIDIOC_SUBSCRIBE_EVENT, &sub);

            sub.type = V4L2_EVENT_DECODE_ERROR;
            ioctl(nFd, VIDIOC_SUBSCRIBE_EVENT, &sub);
        }

    }

    //nEventFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK );

    return nFd;
}
status_t V4l2Dev::Close()
{
    if(nFd >= 0){
        close(nFd);
        nFd = -1;
    }

    if(nEventFd >= 0){
        close(nEventFd);
        nEventFd = -1;
    }
    return OK;
}
status_t V4l2Dev::SearchName(V4l2DEV_TYPE type)
{
    //open device node directly to save search time.
    if(type == V4L2_DEV_DECODER){
         strcpy((char *)sDevName, VPU_DEC_NODE );
         return OK;
    }else if(type == V4L2_DEV_ENCODER){
         strcpy((char *)sDevName, VPU_ENC_NODE );
         return OK;
    }

    int32_t index = 0;
    //isi node is /dev/video2 on board with due camera
    int32_t fd = -1;
    char name[MAX_DEV_NAME_LEN];
    bool bGet = false;
    struct v4l2_capability cap;

    while(index < MAX_VIDEO_SEARCH_NODE){

        sprintf((char*)name, "/dev/video%d", index);

        fd = open ((char*)name, O_RDWR);
        if(fd < 0){
            ALOGV("open index %d failed\n",index);
            index ++;
            continue;
        }
        if (ioctl (fd, VIDIOC_QUERYCAP, &cap) < 0) {
            close(fd);
            ALOGV("VIDIOC_QUERYCAP %d failed\n",index);
            index ++;
            continue;
        }
        ALOGV("index %d name=%s\n",index,(char*)cap.driver);

        if(type == V4L2_DEV_DECODER || type == V4L2_DEV_ENCODER){
            if(NULL == strstr((char*)cap.driver, "vpu")){
                close(fd);
                index ++;
                continue;
            }
        }
        if(type == V4L2_DEV_ISI){
            if(NULL == strstr((char*)cap.driver, "mxc-isi-m2m")){
                close(fd);
                index ++;
                continue;
            }
        }

        if (!((cap.capabilities & (V4L2_CAP_VIDEO_M2M |
                        V4L2_CAP_VIDEO_M2M_MPLANE)) ||
                ((cap.capabilities &
                        (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) &&
                    (cap.capabilities &
                        (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE))))){
            close(fd);
            index ++;
            continue;
        }
        ALOGV("index %d \n",index);
        if((isV4lBufferTypeSupported(fd,type,V4L2_BUF_TYPE_VIDEO_OUTPUT)||
            isV4lBufferTypeSupported(fd,type,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)) &&
            (isV4lBufferTypeSupported(fd,type,V4L2_BUF_TYPE_VIDEO_CAPTURE)||
            isV4lBufferTypeSupported(fd,type,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE))){
            bGet = true;
            close(fd);
            ALOGV("get device %s \n",name);
            strcpy((char *)sDevName, name);
            break;
        }
        close(fd);
        index ++;
    }

    if(bGet)
        return OK;
    else
        return UNKNOWN_ERROR;
}
bool V4l2Dev::isV4lBufferTypeSupported(int32_t fd,V4l2DEV_TYPE dec_type, uint32_t v4l2_buf_type )
{
    uint32_t i = 0;
    bool bGot = false;
    struct v4l2_fmtdesc sFmt;

    while(true){
        sFmt.index = i;
        sFmt.type = v4l2_buf_type;
        if(ioctl(fd,VIDIOC_ENUM_FMT,&sFmt) < 0)
            break;

        i++;
        if(v4l2_buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT || v4l2_buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){
            if(dec_type == V4L2_DEV_DECODER && (sFmt.flags & V4L2_FMT_FLAG_COMPRESSED)){
                bGot = true;
                break;
            }else if(dec_type == V4L2_DEV_ENCODER && !(sFmt.flags & V4L2_FMT_FLAG_COMPRESSED)){
                bGot = true;
                break;
            }else if(dec_type == V4L2_DEV_ISI && !(sFmt.flags & V4L2_FMT_FLAG_COMPRESSED)){
                bGot = true;
                break;
            }
        }else if(v4l2_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE || v4l2_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE){
            if(dec_type == V4L2_DEV_DECODER && !(sFmt.flags & V4L2_FMT_FLAG_COMPRESSED)){
                bGot = true;
                break;
            }else if(dec_type == V4L2_DEV_ENCODER && (sFmt.flags & V4L2_FMT_FLAG_COMPRESSED)){
                bGot = true;
                break;
            }else if(dec_type == V4L2_DEV_ISI && !(sFmt.flags & V4L2_FMT_FLAG_COMPRESSED)){
                bGot = true;
                break;
            }
        }
    }

    return bGot;

}
status_t V4l2Dev::QueryFormats(uint32_t format_type)
{
    struct v4l2_fmtdesc fmt;
    int32_t i = 0;
    if(format_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){
        output_formats.clear();
        while(true){
            fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            fmt.index = i;
            if(ioctl(nFd,VIDIOC_ENUM_FMT,&fmt) < 0)
                break;
 
            output_formats.push_back(fmt.pixelformat);
            ALOGV("QueryFormat add output format %x\n",fmt.pixelformat);
            i++;
        }
        if(output_formats.size() > 0)
            return OK;
        else
            return UNKNOWN_ERROR;
    }
    else if(format_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE){
        capture_formats.clear();
        while(true){
            fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            fmt.index = i;
            if(ioctl(nFd,VIDIOC_ENUM_FMT,&fmt) < 0)
                break;

            capture_formats.push_back(fmt.pixelformat);
            ALOGV("QueryFormat add capture format %x\n",fmt.pixelformat);
            i++;
        }

        if(capture_formats.size() > 0)
            return OK;
        else
            return UNKNOWN_ERROR;
    }
    return BAD_TYPE;
}
bool V4l2Dev::IsOutputFormatSupported(uint32_t format)
{
    if(output_formats.empty()){
        status_t ret = QueryFormats(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
        if(ret != OK)
            return false;
    }
    
    for (uint32_t i = 0; i < output_formats.size(); i++) {
        if(format == output_formats.at(i)){
            return true;
        }
    }

    return false;
}
bool V4l2Dev::IsCaptureFormatSupported(uint32_t format)
{
    if(capture_formats.empty()){
        status_t ret = QueryFormats(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
        if(ret != OK)
            return false;
    }
    
    for (uint32_t i = 0; i < capture_formats.size(); i++) {
        if(format == capture_formats.at(i)){
            return true;
        }
    }
    return false;
}

typedef struct{
    const char * mime;
    uint32_t v4l2_format;
}V4L2_FORMAT_TABLE;

static const V4L2_FORMAT_TABLE v4l2_format_table[]={
    { MEDIA_MIMETYPE_VIDEO_AVC, V4L2_PIX_FMT_H264 },
    { MEDIA_MIMETYPE_VIDEO_HEVC, v4l2_fourcc('H', 'E', 'V', 'C') },
    { MEDIA_MIMETYPE_VIDEO_H263, V4L2_PIX_FMT_H263 },
    { MEDIA_MIMETYPE_VIDEO_MPEG4, V4L2_PIX_FMT_MPEG4 },
    { MEDIA_MIMETYPE_VIDEO_MPEG2, V4L2_PIX_FMT_MPEG2 },
    { MEDIA_MIMETYPE_VIDEO_VP8, V4L2_PIX_FMT_VP8 },
    { MEDIA_MIMETYPE_VIDEO_VC1, V4L2_PIX_FMT_VC1_ANNEX_G },
    { MEDIA_MIMETYPE_VIDEO_XVID, V4L2_PIX_FMT_XVID },
    { MEDIA_MIMETYPE_VIDEO_REAL, v4l2_fourcc('R', 'V', '0', '0')},
    { MEDIA_MIMETYPE_VIDEO_MJPEG, V4L2_PIX_FMT_JPEG },
    { MEDIA_MIMETYPE_VIDEO_SORENSON, v4l2_fourcc('S', 'P', 'K', '0')},
#if 0//TODO: add extended format
    { MEDIA_MIMETYPE_VIDEO_DIV3, v4l2_fourcc('D', 'I', 'V', '3') },
    { MEDIA_MIMETYPE_VIDEO_DIV4, v4l2_fourcc('D', 'I', 'V', 'X') },
    { MEDIA_MIMETYPE_VIDEO_DIVX, v4l2_fourcc('D', 'I', 'V', 'X') },
#endif
};

typedef struct{
    uint32_t color_format;
    uint32_t v4l2_format;
}COLOR_FORMAT_TABLE;

//TODO: add android pixel format
static const COLOR_FORMAT_TABLE color_format_table[]={
    { HAL_PIXEL_FORMAT_NV12_TILED, V4L2_PIX_FMT_NV12 },
    { HAL_PIXEL_FORMAT_YCbCr_420_SP, V4L2_PIX_FMT_NV12 },
    { HAL_PIXEL_FORMAT_YCbCr_420_888, V4L2_PIX_FMT_NV12 },
    { HAL_PIXEL_FORMAT_P010, v4l2_fourcc('N', 'T', '1', '2')}
};
status_t V4l2Dev::GetStreamTypeByMime(const char * mime, uint32_t * format_type)
{
    
    for( size_t i = 0; i < sizeof(v4l2_format_table)/sizeof(V4L2_FORMAT_TABLE); i++){
        if (!strcmp(mime, v4l2_format_table[i].mime)) {
            *format_type = v4l2_format_table[i].v4l2_format;
            return OK;
        }
    }

    *format_type = 0x0;
    return ERROR_UNSUPPORTED;
}

status_t V4l2Dev::GetMimeByStreamType(uint32_t format_type, const char ** mime)
{
    for( size_t i = 0; i < sizeof(v4l2_format_table)/sizeof(V4L2_FORMAT_TABLE); i++){
        if (format_type == v4l2_format_table[i].v4l2_format) {
            *mime = v4l2_format_table[i].mime;
            return OK;
        }
    }

    *mime = NULL;
    return ERROR_UNSUPPORTED;
}

status_t V4l2Dev::GetColorFormatByV4l2(uint32_t v4l2_format, uint32_t * color_format)
{
    for( size_t i = 0; i < sizeof(color_format_table)/sizeof(COLOR_FORMAT_TABLE); i++){
        if (v4l2_format == color_format_table[i].v4l2_format) {
            *color_format = color_format_table[i].color_format;
            return OK;
        }
    }
    *color_format = 0;
    return ERROR_UNSUPPORTED;
}
status_t V4l2Dev::GetV4l2FormatByColor(uint32_t color_format, uint32_t * v4l2_format)
{
    for( size_t i = 0; i < sizeof(color_format_table)/sizeof(COLOR_FORMAT_TABLE); i++){
        if (color_format == color_format_table[i].color_format) {
            *v4l2_format = color_format_table[i].v4l2_format;
            return OK;
        }
    }
    *v4l2_format = 0;
    return ERROR_UNSUPPORTED;
}

status_t V4l2Dev::GetFormatFrameInfo(uint32_t format, struct v4l2_frmsizeenum * info)
{
    if(info == NULL)
        return BAD_TYPE;

    info->index = 0;
    info->type = V4L2_FRMSIZE_TYPE_STEPWISE;
    info->pixel_format = format;

    if(0 == ioctl(nFd, VIDIOC_ENUM_FRAMESIZES, info)){
        return OK;
    }

    return UNKNOWN_ERROR;
}
uint32_t V4l2Dev::Poll()
{
    uint32_t ret = V4L2_DEV_POLL_NONE;
    int r;
    struct pollfd pfd[2];
    struct timespec ts;
    ts.tv_sec = 0;//default timeout 1 seconds
    ts.tv_nsec = 400000000;

    pfd[0].fd = nFd;
    pfd[0].events = POLLERR | POLLNVAL | POLLHUP;
    pfd[0].revents = 0;

    pfd[0].events |= POLLOUT | POLLPRI | POLLWRNORM;
    pfd[0].events |= POLLIN | POLLRDNORM;

    pfd[1].fd = nEventFd;
    pfd[1].events = POLLIN | POLLERR;

    ALOGV("Poll BEGIN %p\n",this);
    r = ppoll (&pfd[0], 2, &ts, NULL);

    if(r <= 0){
        ret = V4L2_DEV_POLL_NONE;
    }else{
        if(pfd[1].revents & POLLERR){
            ret = V4L2_DEV_POLL_NONE;
            return ret;
        }

        if(pfd[0].revents & POLLPRI){
            ALOGV("[%p]POLLPRI \n",this);
            ret = V4L2_DEV_POLL_EVENT;
            return ret;
        }

        if(pfd[0].revents & POLLERR){
            //char tembuf[1];
            //read (mFd, tembuf, 1);
            ret = V4L2_DEV_POLL_NONE;
            usleep(2000);
            return ret;
        }

        if((pfd[0].revents & POLLIN) || (pfd[0].revents & POLLRDNORM)){
            ret |= V4L2_DEV_POLL_CAPTURE;
        }
        if((pfd[0].revents & POLLOUT) || (pfd[0].revents & POLLWRNORM)){
            ret |= V4L2_DEV_POLL_OUTPUT;
        }
    }

    ALOGV("Poll END,ret=%x\n",ret);
    return ret;
}
status_t V4l2Dev::SetPollInterrupt()
{
    if(nEventFd > 0){
        const uint64_t buf = EFD_CLOEXEC|EFD_NONBLOCK;
        eventfd_write(nEventFd, buf);
    }
    return OK;
}
status_t V4l2Dev::ClearPollInterrupt()
{
    if(nEventFd > 0){
        uint64_t buf;
        eventfd_read(nEventFd, &buf);
    }
    return OK;
}
status_t V4l2Dev::ResetDecoder()
{
    int ret = 0;
    struct v4l2_decoder_cmd cmd;
    memset(&cmd, 0, sizeof(struct v4l2_decoder_cmd));

    cmd.cmd = IMX_V4L2_DEC_CMD_RESET;
    cmd.flags = V4L2_DEC_CMD_STOP_IMMEDIATELY;

    ret = ioctl(nFd, VIDIOC_DECODER_CMD, &cmd);
    if(ret < 0){
        ALOGE("V4l2Dev::ResetDecoder ret=%x\n",ret);
        return UNKNOWN_ERROR;
    }

    ALOGV("V4l2Dev::ResetDecoder SUCCESS\n");
    return OK;

}

status_t V4l2Dev::StopDecoder()
{
    int ret = 0;
    struct v4l2_decoder_cmd cmd;
    memset(&cmd, 0, sizeof(struct v4l2_decoder_cmd));

    cmd.cmd = V4L2_DEC_CMD_STOP;
    cmd.flags = V4L2_DEC_CMD_STOP_IMMEDIATELY;

    ret = ioctl(nFd, VIDIOC_DECODER_CMD, &cmd);
    if(ret < 0){
        ALOGV("V4l2Dev::StopDecoder ret=%x\n",ret);
        return UNKNOWN_ERROR;
    }

    ALOGV("V4l2Dev::StopDecoder SUCCESS\n");
    return OK;
}
status_t V4l2Dev::EnableLowLatencyDecoder(bool enabled)
{

    int ret = 0;
    struct v4l2_control ctl = { 0,0 };
    ctl.id = V4L2_CID_USER_FRAME_DIS_REORDER;
    ctl.value = enabled;
    ret = ioctl(nFd, VIDIOC_S_CTRL, &ctl);

    if(ret < 0){
        ALOGV("V4l2Dev::EnableLowLatencyDecoder ret=%x\n",ret);
        return UNKNOWN_ERROR;
    }

    return OK;
}

status_t V4l2Dev::StopEncoder()
{
    int ret = 0;

    struct v4l2_encoder_cmd cmd;
    memset(&cmd, 0, sizeof(struct v4l2_encoder_cmd));

    cmd.cmd = V4L2_ENC_CMD_STOP;
    cmd.flags = V4L2_ENC_CMD_STOP_AT_GOP_END;
    ret = ioctl(nFd, VIDIOC_ENCODER_CMD, &cmd);

    if(ret < 0){
        ALOGV("V4l2Dev::StopEncoder FAILED\n");
        return UNKNOWN_ERROR;
    }

    ALOGV("V4l2Dev::StopEncoder SUCCESS\n");
    return OK;
}
status_t V4l2Dev::SetEncoderParam(V4l2EncInputParam *param)
{
    int ret = 0;
    if(param == NULL)
        return UNKNOWN_ERROR;

    ALOGV("SetEncoderParam nBitRate=%d\n",param->nBitRate);
    ALOGV("SetEncoderParam nGOPSize=%d\n",param->nGOPSize);
    ALOGV("SetEncoderParam nIntraFreshNum=%d\n",param->nIntraFreshNum);
    ret = SetEncoderBitrate(param->nBitRateMode, param->nBitRate);

    if(param->nGOPSize > 0)
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_GOP_SIZE,param->nGOPSize);

    ALOGV("SetEncoderParam V4L2_CID_MPEG_VIDEO_GOP_SIZE ret=%x\n",ret);
    
    if(param->nH264_i_qp > 0)
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP,param->nH264_i_qp);
    if(param->nH264_p_qp > 0)
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP,param->nH264_p_qp);
    if(param->nH264_min_qp > 0)
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_H264_MIN_QP,param->nH264_min_qp);
    if(param->nH264_max_qp > 0)
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_H264_MAX_QP,param->nH264_max_qp);
    if(param->nMpeg4_i_qp > 0)
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP,param->nMpeg4_i_qp);
    if(param->nMpeg4_p_qp > 0)
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP,param->nMpeg4_p_qp);
    if(param->nIntraFreshNum > 0)
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB,param->nIntraFreshNum);

    ALOGV("SetEncoderParam 1 ret=%x\n",ret);

    //ignore result
    int32_t value= 1;
    if(90 == param->nRotAngle || 270 == param->nRotAngle)
        SetCtrl(V4L2_CID_HFLIP,value);
    else if(0 == param->nRotAngle || 180 == param->nRotAngle)
        SetCtrl(V4L2_CID_VFLIP,value);

    ret = SetH264EncoderProfileAndLevel(param->nProfile, param->nLevel);
    
    ALOGV("SetH264EncoderProfileAndLevel ret=%x\n",ret);
    return ret;
}
status_t V4l2Dev::SetH264EncoderProfileAndLevel(uint32_t profile, uint32_t level)
{
    int ret = 0;
    int v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    int v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_1;

    switch(profile){
        case PROFILE_AVC_BASELINE:
        case PROFILE_AVC_CONSTRAINED_BASELINE:
            v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
            break;
        case PROFILE_AVC_MAIN:
            v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
            break;
        case PROFILE_AVC_HIGH:
            v4l2_profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
            break;
        default:
            break;
    }

    switch (level) {
        case 10:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
            break;
        case 9:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1B;
            break;
        case 11:
            //adjust according to cts's code
            //v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
            break;
        case 12:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
            break;
        case 13:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
            break;
        case 20:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
            break;
        case 21:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
            break;
        case 22:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
            break;
        case 30:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
            break;
        case 31:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
            break;
        case 32:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
            break;
        case 40:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
            break;
        case 41:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
            break;
        case 42:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
            break;
        case 50:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
            break;
        case 51:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
            break;
        case 52:
            v4l2_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
            break;
        default:
            break;
    }

    ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_H264_PROFILE, v4l2_profile);
    ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_H264_LEVEL, v4l2_level);
    ALOGV("set profile=%d,level=%d,v4l2_profile=%d,v4l2_level=%d,ret=%d",profile,level,v4l2_profile,v4l2_level,ret);
    return OK;
}

status_t V4l2Dev::SetFrameRate(uint32_t framerate)
{
    struct v4l2_streamparm parm;
    int ret = 0;

    if (0 == framerate)
        return UNKNOWN_ERROR;

    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    parm.parm.capture.timeperframe.numerator = 0x1000;
    parm.parm.capture.timeperframe.denominator = framerate * 0x1000;
    ALOGV("set frame rate =%d",framerate);
    ret = ioctl(nFd, VIDIOC_S_PARM, &parm);
    if (ret) {
        ALOGE("SetFrameRate fail\n");
        return UNKNOWN_ERROR;
    }
    return OK;
}

status_t V4l2Dev::SetForceKeyFrame()
{
    return SetCtrl(V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1);
}

status_t V4l2Dev::SetCtrl(uint32_t id, int32_t value)
{
    int ret = 0;
    struct v4l2_control ctl = { 0,0 };
    ctl.id = id;
    ctl.value = value;
    ret = ioctl(nFd, VIDIOC_S_CTRL, &ctl);

    return (status_t)ret;
}

typedef struct{
    uint32_t iso_value;
    uint32_t v4l2_value;
}V4L2_ISO_MAP;

static const V4L2_ISO_MAP v4l2_color_table[]={
    { 1, V4L2_COLORSPACE_REC709 },
    { 4, V4L2_COLORSPACE_470_SYSTEM_M },
    { 5, V4L2_COLORSPACE_470_SYSTEM_BG },
    { 6, V4L2_COLORSPACE_SMPTE170M },
    { 7, V4L2_COLORSPACE_SMPTE240M },
    { 8, V4L2_COLORSPACE_GENERIC_FILM },
    { 9, V4L2_COLORSPACE_BT2020 },
    { 10, V4L2_COLORSPACE_BT2020 },
};
static const V4L2_ISO_MAP v4l2_xfer_table[]={
    { 1, V4L2_XFER_FUNC_709 },
    { 4, V4L2_XFER_FUNC_GAMMA22 },
    { 5, V4L2_XFER_FUNC_GAMMA28 },
    { 6, V4L2_XFER_FUNC_709 },
    { 7, V4L2_XFER_FUNC_SMPTE240M },
    { 8, V4L2_XFER_FUNC_LINEAR },
    { 11, V4L2_XFER_FUNC_XVYCC },
    { 12, V4L2_XFER_FUNC_BT1361 },
    { 13, V4L2_XFER_FUNC_SRGB },
    { 14, V4L2_XFER_FUNC_709 },
    { 16, V4L2_XFER_FUNC_SMPTE2084 },
    { 17, V4L2_XFER_FUNC_ST428 },
    { 18, V4L2_XFER_FUNC_HLG },
};
static const V4L2_ISO_MAP v4l2_ycbcr_table[]={
    { 1, V4L2_YCBCR_ENC_709 },
    { 4, V4L2_YCBCR_ENC_BT470_6M },
    { 5, V4L2_YCBCR_ENC_601 },
    { 7, V4L2_YCBCR_ENC_SMPTE240M },
    { 9, V4L2_YCBCR_ENC_BT2020 },
    { 10, V4L2_YCBCR_ENC_BT2020_CONST_LUM },
};
status_t V4l2Dev::GetColorAspectsInfo(struct v4l2_pix_format_mplane * pixel_fmt, VideoColorAspect * desc)
{

    if(pixel_fmt == NULL || desc == NULL)
        return UNKNOWN_ERROR;

    desc->colourPrimaries = 0;
    for( size_t i = 0; i < sizeof(v4l2_color_table)/sizeof(V4L2_ISO_MAP); i++){
        if (pixel_fmt->colorspace == v4l2_color_table[i].v4l2_value) {
            desc->colourPrimaries = v4l2_color_table[i].iso_value;
            break;
        }
    }

    desc->transferCharacteristics = 0;
    for( size_t i = 0; i < sizeof(v4l2_xfer_table)/sizeof(V4L2_ISO_MAP); i++){
        if (pixel_fmt->xfer_func == v4l2_xfer_table[i].v4l2_value) {
            desc->transferCharacteristics = v4l2_xfer_table[i].iso_value;
            break;
        }
    }

    //2, ColorAspects::MatrixUnspecified
    desc->matrixCoeffs = 2;
    for( size_t i = 0; i < sizeof(v4l2_ycbcr_table)/sizeof(V4L2_ISO_MAP); i++){
        if (pixel_fmt->ycbcr_enc == v4l2_ycbcr_table[i].v4l2_value) {
            desc->matrixCoeffs = v4l2_ycbcr_table[i].iso_value;
            break;
        }
    }

    desc->fullRange = (pixel_fmt->quantization == V4L2_QUANTIZATION_FULL_RANGE) ? 1:0;

    ALOGV("getColorAspectsInfo success, p=%d,t=%d,m=%d,r=%d\n",
        desc->colourPrimaries,desc->transferCharacteristics,desc->matrixCoeffs,desc->fullRange);

    return OK;
}
status_t V4l2Dev::SetColorAspectsInfo(VideoColorAspect * desc, struct v4l2_pix_format_mplane * pixel_fmt)
{
    if(pixel_fmt == NULL || desc == NULL)
        return UNKNOWN_ERROR;

    pixel_fmt->colorspace = V4L2_COLORSPACE_DEFAULT;
    for( size_t i = 0; i < sizeof(v4l2_color_table)/sizeof(V4L2_ISO_MAP); i++){
        if (desc->colourPrimaries == v4l2_color_table[i].iso_value) {
            pixel_fmt->colorspace = v4l2_color_table[i].v4l2_value;
            break;
        }
    }

    pixel_fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
    for( size_t i = 0; i < sizeof(v4l2_xfer_table)/sizeof(V4L2_ISO_MAP); i++){
        if (desc->transferCharacteristics == v4l2_xfer_table[i].iso_value) {
            pixel_fmt->xfer_func = v4l2_xfer_table[i].v4l2_value;
            break;
        }
    }

    pixel_fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
    for( size_t i = 0; i < sizeof(v4l2_ycbcr_table)/sizeof(V4L2_ISO_MAP); i++){
        if (desc->matrixCoeffs == v4l2_ycbcr_table[i].iso_value) {
            pixel_fmt->ycbcr_enc = v4l2_ycbcr_table[i].v4l2_value;
            break;
        }
    }

    pixel_fmt->quantization = (desc->fullRange)? V4L2_QUANTIZATION_FULL_RANGE:V4L2_QUANTIZATION_LIM_RANGE;
    ALOGV("SetColorAspectsInfo success, p=%d,t=%d,m=%d,r=%d\n",
        pixel_fmt->colorspace,pixel_fmt->xfer_func,pixel_fmt->ycbcr_enc,pixel_fmt->quantization);
    return OK;
}
status_t V4l2Dev::SetEncoderBitrate(int32_t mode, int32_t bitrate){
    int ret = 0;

    if(bitrate > 0){
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_BITRATE_MODE,mode);
        ret |= SetCtrl(V4L2_CID_MPEG_VIDEO_BITRATE,bitrate);
    }
    ALOGV("SetEncoderBitrate mode=%d,bitrate=%d ret=%x\n",mode, bitrate, ret);
    return ret;
}
}
