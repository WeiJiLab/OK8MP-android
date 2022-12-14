# Copyright (C) 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_USES_NANOHUB_SENSORHAL), true)

COMMON_CFLAGS := -Wall -Werror -Wextra

################################################################################
ifeq ($(NANOHUB_SENSORHAL_LEFTY_IMPL_ENABLED), true)

include $(CLEAR_VARS)

LOCAL_MODULE := vendor.google_clockwork.lefty@1.0-impl.nanohub

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := google
LOCAL_PROPRIETARY_MODULE := true

LOCAL_CFLAGS += $(COMMON_CFLAGS)

LOCAL_C_INCLUDES += \
    device/google/contexthub/firmware/os/inc \
    device/google/contexthub/sensorhal \
    device/google/contexthub/util/common \

LOCAL_SRC_FILES := \
    Lefty.cpp \

LOCAL_SHARED_LIBRARIES := \
    vendor.google_clockwork.lefty@1.0 \
    libbase \
    libcutils \
    libhidlbase \
    libhubconnection \
    liblog \
    libutils \

include $(BUILD_SHARED_LIBRARY)

endif
################################################################################
ifeq ($(NANOHUB_SENSORHAL_LEFTY_SERVICE_ENABLED), true)

include $(CLEAR_VARS)

LOCAL_MODULE := liblefty_service_nanohub

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := google
LOCAL_PROPRIETARY_MODULE := true

LOCAL_CFLAGS += $(COMMON_CFLAGS)

LOCAL_C_INCLUDES += \
    device/google/contexthub/firmware/os/inc \
    device/google/contexthub/sensorhal \
    device/google/contexthub/util/common \

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

LOCAL_SRC_FILES := \
    Lefty.cpp \
    lefty_service.cpp \

LOCAL_SHARED_LIBRARIES := \
    vendor.google_clockwork.lefty@1.0 \
    libbase \
    libcutils \
    libhidlbase \
    libhubconnection \
    liblog \
    libutils \

include $(BUILD_SHARED_LIBRARY)

endif
################################################################################

endif
