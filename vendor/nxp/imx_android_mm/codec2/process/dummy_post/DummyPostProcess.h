/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#ifndef DUMMY_POST_PROCESS_H
#define DUMMY_POST_PROCESS_H

#include "ProcessBase.h"

namespace android {

class DummyPostProcess : public ProcessBase {
public:

    status_t onInit() override;
    status_t onDestroy() override;
    status_t onStart() override;
    status_t onStop() override;
protected:
    status_t onProcess() override;
    status_t onVideoResChanged() override;
    status_t DoSetConfig(ProcessConfig index, void* pConfig) override;
    status_t DoGetConfig(ProcessConfig index, void* pConfig) override;

};

} // namespcae android

#endif // G2D_POST_PROCESS_BASE_H