/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */
#ifndef V4L2_DEV_H
#define V4L2_DEV_H
#include <stdint.h>
#include <sys/types.h>
#include <linux/videodev2.h>
#include <media/stagefright/foundation/AHandler.h>
#include <vector>

namespace android {

typedef enum{
V4L2_DEV_DECODER = 0,
V4L2_DEV_ENCODER,
V4L2_DEV_ISI,
}V4l2DEV_TYPE;


#define V4L2_DEV_POLL_NONE 0
#define V4L2_DEV_POLL_EVENT 1
#define V4L2_DEV_POLL_OUTPUT 2
#define V4L2_DEV_POLL_CAPTURE 4

#define IMX_V4L2_DEC_CMD_START         (0x09000000)
#define IMX_V4L2_DEC_CMD_RESET         (IMX_V4L2_DEC_CMD_START + 1)

#define V4L2_EVENT_DECODE_ERROR                (V4L2_EVENT_PRIVATE_START + 1)
#define V4L2_EVENT_SKIP                        (V4L2_EVENT_PRIVATE_START + 2)

#define MAX_DEV_NAME_LEN (16)

typedef struct {
    uint32_t colourPrimaries;
    uint32_t transferCharacteristics;
    uint32_t matrixCoeffs;
    uint32_t fullRange;
} VideoColorAspect;

typedef struct {
    int32_t nBitRate;/*unit: bps*/
    int32_t nBitRateMode;
    int32_t nGOPSize;
    int32_t nH264_i_qp;
    int32_t nH264_p_qp;
    int32_t nH264_min_qp;
    int32_t nH264_max_qp;
    int32_t nMpeg4_i_qp;
    int32_t nMpeg4_p_qp;
    int32_t nIntraFreshNum;//V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB
    int32_t nProfile;
    int32_t nLevel;
    int32_t nRotAngle;
} V4l2EncInputParam;
    
class V4l2Dev{
public:
    explicit V4l2Dev();
    int32_t Open(V4l2DEV_TYPE type);
    status_t Close();
    bool IsOutputFormatSupported(uint32_t format);
    bool IsCaptureFormatSupported(uint32_t format);
    status_t GetFormatFrameInfo(uint32_t format, struct v4l2_frmsizeenum * info);

    status_t GetStreamTypeByMime(const char * mime, uint32_t * format_type);
    status_t GetMimeByStreamType(uint32_t format_type, const char ** mime);

    status_t GetColorFormatByV4l2(uint32_t v4l2_format, uint32_t * color_format);
    status_t GetV4l2FormatByColor(uint32_t color_format, uint32_t * v4l2_format);
    
    uint32_t Poll();
    status_t SetPollInterrupt();
    status_t ClearPollInterrupt();
    status_t ResetDecoder();
    status_t StopDecoder();

    status_t GetColorAspectsInfo(struct v4l2_pix_format_mplane * pixel_fmt, VideoColorAspect * desc);
    status_t EnableLowLatencyDecoder(bool enabled);

    //encoder functions
    status_t StopEncoder();
    status_t SetEncoderParam(V4l2EncInputParam *param);
    status_t SetH264EncoderProfileAndLevel(uint32_t profile, uint32_t level);
    status_t SetFrameRate(uint32_t framerate);
    status_t SetForceKeyFrame();
    status_t SetColorAspectsInfo(VideoColorAspect * desc, struct v4l2_pix_format_mplane * pixel_fmt);
    status_t SetEncoderBitrate(int32_t mode, int32_t bitrate);
private:
    status_t SearchName(V4l2DEV_TYPE type);
    bool isV4lBufferTypeSupported(int32_t fd, V4l2DEV_TYPE dec_type, uint32_t v4l2_buf_type);
    status_t QueryFormats(uint32_t format_type);

    status_t SetCtrl(uint32_t id, int32_t value);

    char sDevName[MAX_DEV_NAME_LEN];
    int32_t nFd;
    int32_t nEventFd;
    std::vector<uint32_t> output_formats;
    std::vector<uint32_t> capture_formats;
};
}
#endif
