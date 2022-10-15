/**
 *  Copyright (c) 2016, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */
#ifndef FSL_INSPECTOR_H_
#define FSL_INSPECTOR_H_

#include <media/MediaExtractorPluginHelper.h>

namespace android {

bool SniffIMX(
        DataSourceHelper *source, float *confidence,void **meta);

}
#endif
