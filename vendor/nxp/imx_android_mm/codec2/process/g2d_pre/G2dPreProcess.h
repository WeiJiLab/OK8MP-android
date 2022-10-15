/**
 *  Copyright 2019 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#ifndef G2D_PRE_PROCESS_BASE_H
#define G2D_PRE_PROCESS_BASE_H

#include <queue>

#include "ProcessBase.h"
#include "g2d.h"

namespace android {

typedef struct g2d_surface G2DSurface;

typedef struct {
    void * g2dHandle;
    G2DSurface sInput;
    G2DSurface sOutput;
} VpuEncoderGpuHandle;

typedef int (*g2d_func1)(void* handle);
typedef int (*g2d_func2)(void* handle, void* arg1, void* arg2);

typedef struct {
    void * hLibHandle;
    g2d_func1 mG2dOpen;
    g2d_func1 mG2dFinish;
    g2d_func1 mG2dClose;
    g2d_func2 mG2dBlit;
} G2D_MODULE;



class G2dPreProcess : public ProcessBase {
public:
    G2dPreProcess();
    virtual ~G2dPreProcess();

    status_t onInit() override;
    status_t onDestroy() override;
    status_t onStart() override;
    status_t onStop() override;
protected:
    status_t onProcess() override;
    status_t DoSetConfig(ProcessConfig index, void* pConfig) override;
    status_t DoGetConfig(ProcessConfig index, void* pConfig) override;

private:
    VpuEncoderGpuHandle* pPPHandle; // preprocess handle
    G2D_MODULE sG2dModule;
};

} // namespcae android

#endif // G2D_PRE_PROCESS_BASE_H