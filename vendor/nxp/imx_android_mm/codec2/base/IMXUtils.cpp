/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#include <stdio.h>
#include <string.h>
#include <graphics.h>
#include <media/stagefright/MediaDefs.h>
#include <C2Config.h>
#include "Imx_ext.h"
#include "IMXUtils.h"
#include "graphics_ext.h"


namespace android {


typedef struct {
    const char* name;
    const char* mime;
} NameMime;

static NameMime NameMimeMap[] = {
        {"c2.imx.avc.decoder", MEDIA_MIMETYPE_VIDEO_AVC},
        {"c2.imx.avc.encoder", MEDIA_MIMETYPE_VIDEO_AVC},
        {"c2.imx.hevc.decoder", MEDIA_MIMETYPE_VIDEO_HEVC},
        {"c2.imx.hevc.encoder", MEDIA_MIMETYPE_VIDEO_HEVC},
        {"c2.imx.vp8.decoder", MEDIA_MIMETYPE_VIDEO_VP8},
        {"c2.imx.vp8.encoder", MEDIA_MIMETYPE_VIDEO_VP8},
        {"c2.imx.vp9.decoder", MEDIA_MIMETYPE_VIDEO_VP9},
        {"c2.imx.mpeg2.decoder", MEDIA_MIMETYPE_VIDEO_MPEG2},
        {"c2.imx.mpeg4.decoder", MEDIA_MIMETYPE_VIDEO_MPEG4},
        {"c2.imx.h263.decoder", MEDIA_MIMETYPE_VIDEO_H263},
        {"c2.imx.mjpeg.decoder", MEDIA_MIMETYPE_VIDEO_MJPEG},
        {"c2.imx.xvid.decoder", MEDIA_MIMETYPE_VIDEO_XVID},
        {"c2.imx.vc1.decoder", MEDIA_MIMETYPE_VIDEO_VC1},
        {"c2.imx.rv.decoder", MEDIA_MIMETYPE_VIDEO_REAL},
        {"c2.imx.sorenson.decoder", MEDIA_MIMETYPE_VIDEO_SORENSON},
};

const char* Name2MimeType(const char* name) {
    int i;
    for (i = 0; i < sizeof(NameMimeMap)/sizeof(NameMime); i++) {
        if (strcmp(NameMimeMap[i].name, name) == 0) {
            return NameMimeMap[i].mime;
        }
    }

    return nullptr;
}

int pxlfmt2bpp(int pxlfmt) {
    int bpp; // bit per pixel

    switch(pxlfmt) {
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCBCR_420_888:
        case HAL_PIXEL_FORMAT_NV12_TILED:
        case HAL_PIXEL_FORMAT_YV12:
          bpp = 12;
          break;
        case HAL_PIXEL_FORMAT_P010:
        case HAL_PIXEL_FORMAT_P010_TILED:
        case HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED:
          bpp = 15;
          break;
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_YCbCr_422_P:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCBCR_422_I:
            bpp = 16;
            break;

        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            bpp = 32;
            break;
        default:
          bpp = 0;
          break;
    }
    return bpp;
}

int GetSocId(char* socId, int size) {
    int ret = 1; // default set failed;

    if ((socId == NULL) || (size < 10))
        return false;

    memset(socId, 0, size);

    FILE *f = fopen("/sys/devices/soc0/soc_id", "r");
    if (f != NULL) {
        if (fgets(socId, size, f) != NULL) {
            ret = 0; // success
        }
        fclose(f);
    }

    return ret;
}

} // namespace android
