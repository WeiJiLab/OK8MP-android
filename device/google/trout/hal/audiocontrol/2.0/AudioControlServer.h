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

#include <functional>
#include <memory>
#include <string>

#include <android/hardware/automotive/audiocontrol/2.0/IFocusListener.h>

namespace android::hardware::automotive::audiocontrol::V2_0::implementation {

class AudioControlServer {
  public:
    using close_handle_func_t = std::function<void()>;

    virtual ~AudioControlServer() = default;

    virtual close_handle_func_t RegisterFocusListener(const sp<IFocusListener>& focusListener) = 0;

    virtual void Start() = 0;

    virtual void Join() = 0;
};

std::unique_ptr<AudioControlServer> MakeAudioControlServer(const std::string& addr);

}  // namespace android::hardware::automotive::audiocontrol::V2_0::implementation
