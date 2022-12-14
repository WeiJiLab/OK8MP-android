LOCAL_PATH:= $(call my-dir)

##################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    evs_app.cpp \
    EvsStateControl.cpp \
    RenderBase.cpp \
    RenderDirectView.cpp \
    RenderTopView.cpp \
    ConfigManager.cpp \
    glError.cpp \
    shader.cpp \
    TexWrapper.cpp \
    VideoTex.cpp \
    StreamHandler.cpp \
    FormatConvert.cpp \
    RenderPixelCopy.cpp

LOCAL_SHARED_LIBRARIES := \
    libcamera_metadata \
    libbinder \
    libcutils \
    liblog \
    libutils \
    libui \
    libhidlbase \
    libhidltransport \
    libEGL \
    libGLESv2 \
    libhardware \
    libpng \
    android.hardware.automotive.evs@1.0 \
    android.hardware.automotive.evs@1.1 \
    android.hardware.automotive.vehicle@2.0 \

LOCAL_STATIC_LIBRARIES := \
    libmath \
    libjsoncpp \

LOCAL_STRIP_MODULE := keep_symbols

# LOCAL_INIT_RC := evs_app.rc

LOCAL_MODULE:= imx_evs_app
LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -DLOG_TAG=\"EvsApp\"
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CFLAGS += -Wall -Werror -Wunused -Wunreachable-code

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := ImxConfig.json
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/automotive/evs
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := ImxCarFromTop.png
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/automotive/evs
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := ImxLabeledChecker.png
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/automotive/evs
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := imx_evs_app_default_resources
LOCAL_REQUIRED_MODULES := \
    ImxConfig.json \
    ImxCarFromTop.png \
    ImxLabeledChecker.png
include $(BUILD_PHONY_PACKAGE)
