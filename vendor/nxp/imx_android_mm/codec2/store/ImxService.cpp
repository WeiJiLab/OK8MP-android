/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "ImxService"
#include <log/log.h>

#include <C2Component.h>
#include <codec2/hidl/1.0/ComponentStore.h>
#include <hidl/HidlTransportSupport.h>
#include <binder/ProcessState.h>
#include <minijail.h>

//#include <C2Component.h>
//#include <C2ComponentFactory.h>
//#include <C2Config.h>
//#include <util/C2InterfaceHelper.h>
//#include <C2AllocatorGralloc.h>
//#include <C2AllocatorIon.h>
//#include <dlfcn.h>

#include <C2_imx.h>
#include <media/stagefright/omx/1.0/OmxStore.h>

// This is created by module "codec2.vendor.base.policy". This can be modified.
static constexpr char kBaseSeccompPolicyPath[] =
        "/vendor/etc/seccomp_policy/codec2.vendor.base.policy";

// Additional device-specific seccomp permissions can be added in this file.
static constexpr char kExtSeccompPolicyPath[] =
        "/vendor/etc/seccomp_policy/codec2.vendor.ext.policy";

int main(int /* argc */, char** /* argv */) {
    ALOGD("android.hardware.media.c2@1.0-service starting...");

    signal(SIGPIPE, SIG_IGN);
    android::SetUpMinijail(kBaseSeccompPolicyPath, kExtSeccompPolicyPath);

    // vndbinder is needed by BufferQueue.
    android::ProcessState::initWithDriver("/dev/vndbinder");
    android::ProcessState::self()->startThreadPool();

    // Extra threads may be needed to handle a stacked IPC sequence that
    // contains alternating binder and hwbinder calls. (See b/35283480.)
    android::hardware::configureRpcThreadpool(16, true /* callerWillJoin */);

    // Create IComponentStore service.
    {
        //using namespace android::hardware::google::media::c2::V1_0;
        using ::android::hardware::media::c2::V1_0::IComponentStore;
        using namespace ::android::hardware::media::c2::V1_0;
        android::sp<IComponentStore> store;

        ALOGD("Instantiating ImxC2Store service...");
        store = new utils::ComponentStore(android::GetImxC2Store());
        if (store == nullptr) {
            ALOGE("Cannot create Codec2's IComponentStore service.");
        } else {
            if (store->registerAsService("default") != android::OK) {
                ALOGE("Cannot register Codec2's ImxC2Store service.");
            } else {
                ALOGI("Codec2's ImxC2Store service created.");
            }
        }
    }

    // Register IOmxStore service.
    {
        using namespace ::android::hardware::media::omx::V1_0;
        android::sp<IOmxStore> omxStore = new implementation::OmxStore();
        if (omxStore == nullptr) {
            ALOGE("Cannot create IOmxStore HAL service.");
        } else if (omxStore->registerAsService() != android::OK) {
            ALOGE("Cannot register IOmxStore HAL service.");
        }
    }
    ALOGD("Register ImxC2Store service success");
    android::hardware::joinRpcThreadpool();
    return 0;
}

