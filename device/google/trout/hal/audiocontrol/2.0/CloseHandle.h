/*
 * Copyright (C) 2020 The Android Open Source Project
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

#pragma once

#include <android-base/macros.h>
#include <android/hardware/automotive/audiocontrol/2.0/ICloseHandle.h>

namespace android::hardware::automotive::audiocontrol::V2_0::implementation {

/** Generic ICloseHandle implementation ignoring double-close events. */
class CloseHandle : public ICloseHandle {
  public:
    using Callback = std::function<void()>;

    /**
     * Create a handle with a callback.
     *
     * The callback is guaranteed to be called exactly once.
     *
     * \param callback Called on the first close() call, or on destruction of the handle
     */
    explicit CloseHandle(Callback callback = nullptr);
    virtual ~CloseHandle();

    Return<void> close() override;

  private:
    const Callback mCallback;
    std::atomic<bool> mIsClosed = false;

    DISALLOW_COPY_AND_ASSIGN(CloseHandle);
};

}  // namespace android::hardware::automotive::audiocontrol::V2_0::implementation
