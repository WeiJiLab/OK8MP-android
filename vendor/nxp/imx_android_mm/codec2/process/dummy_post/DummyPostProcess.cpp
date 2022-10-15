/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "DummyPostProcess"

#include <cutils/properties.h>
#include <utils/Log.h>
#include <dlfcn.h>

#include "DummyPostProcess.h"

namespace android {

status_t DummyPostProcess::onProcess() {
    return INVALID_OPERATION;
}

status_t DummyPostProcess::onVideoResChanged() {
    return INVALID_OPERATION;

}

status_t DummyPostProcess::onInit() {
    return INVALID_OPERATION;
}

status_t DummyPostProcess::onDestroy() {
    return INVALID_OPERATION;
}
status_t DummyPostProcess::onStart() {
    return INVALID_OPERATION;
}

status_t DummyPostProcess::onStop() {
    return INVALID_OPERATION;
}

status_t DummyPostProcess::DoSetConfig(ProcessConfig index, void* pConfig) {
    return INVALID_OPERATION; // not support any index yet
}

status_t DummyPostProcess::DoGetConfig(ProcessConfig index, void* pConfig) {
    return INVALID_OPERATION; // not support any index yet
}

ProcessBase * CreatePostProcessInstance() {
    return static_cast<ProcessBase *>(new DummyPostProcess());
}


} // namespcae android
