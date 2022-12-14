/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_ISTREAMSOURCE_H_

#define ANDROID_ISTREAMSOURCE_H_

#include <binder/IInterface.h>

namespace android {

struct AMessage;
class IMemory;
struct IStreamListener;

struct IStreamSource : public IInterface {
    DECLARE_META_INTERFACE(StreamSource);

    virtual void setListener(const sp<IStreamListener> &listener) = 0;
    virtual void setBuffers(const Vector<sp<IMemory> > &buffers) = 0;

    virtual void onBufferAvailable(size_t index) = 0;

    enum {
        // Video PES packets contain exactly one (aligned) access unit.
        kFlagAlignedVideoData = 1,

        // Timestamps are in ALooper::GetNowUs() units.
        kFlagIsRealTimeData   = 2,

        // Will discard data if buffered too much data to keep low latency.
        kFlagKeepLowLatency   = 4,
    };
    virtual uint32_t flags() const { return 0; }
};

struct IStreamListener : public IInterface {
    DECLARE_META_INTERFACE(StreamListener);

    enum Command {
        EOS,
        DISCONTINUITY,
    };

    virtual void queueBuffer(size_t index, size_t size) = 0;

    virtual void issueCommand(
            Command cmd, bool synchronous, const sp<AMessage> &msg = NULL) = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct BnStreamSource : public BnInterface<IStreamSource> {
    virtual status_t onTransact(
            uint32_t code, const Parcel &data, Parcel *reply,
            uint32_t flags = 0);
};

struct BnStreamListener : public BnInterface<IStreamListener> {
    virtual status_t onTransact(
            uint32_t code, const Parcel &data, Parcel *reply,
            uint32_t flags = 0);
};

}  // namespace android

#endif  // ANDROID_ISTREAMSOURCE_H_
