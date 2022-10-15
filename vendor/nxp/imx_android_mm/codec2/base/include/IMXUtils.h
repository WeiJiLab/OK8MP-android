/**
 *  Copyright 2019-2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

#ifndef IMX_UUTILS_H_
#define IMX_UUTILS_H_

#include <media/stagefright/foundation/ColorUtils.h>

namespace android {


const char* Name2MimeType(const char* name);
int pxlfmt2bpp(int pxlfmt);

int GetSocId(char* socId, int size);

}

#endif // IMX_UUTILS_H_
