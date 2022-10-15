/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef IMX_C2_H
#define IMX_C2_H

#include <C2Component.h>

#include <memory>

namespace android {

enum decode_mode_t : uint32_t {
    DEC_STREAM_MODE,
    DEC_FILE_MODE,
};

#define LOGV(FMT, ...) do{if(this->logLevel) ALOGV("%s " FMT, __FUNCTION__, ##__VA_ARGS__);} while(0)
#define LOGD(FMT, ...) do{if(this->logLevel) ALOGD("%s " FMT, __FUNCTION__, ##__VA_ARGS__);} while(0)
#define LOGE(FMT, ...) ALOGE("%s " FMT, __FUNCTION__, ##__VA_ARGS__)
#define LOGI(FMT, ...) ALOGI("%s " FMT, __FUNCTION__, ##__VA_ARGS__)
#define LOGW(FMT, ...) ALOGW("%s " FMT, __FUNCTION__, ##__VA_ARGS__)

std::shared_ptr<C2ComponentStore> GetImxC2Store();

//add sync frame flag for C2FrameData::flags
#define FLAG_SYNC_FRAME (1 << 4)
#define FLAG_INTERLACED_FRAME (1 << 5)
#define FLAG_RES_CHANGE (1 << 6)
}

#endif

