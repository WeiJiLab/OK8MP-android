ifeq ($(PREBUILT_FSL_IMX_SENSOR_FUSION),true)
LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_HAS_SENSOR), true)
include $(CLEAR_VARS)
LOCAL_MODULE := magd
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := magd
LOCAL_CHECK_ELF_FILES := false
include $(BUILD_PREBUILT)

endif

ifeq ($(BOARD_USE_SENSOR_FUSION), true)
include $(CLEAR_VARS)
LOCAL_MODULE := fsl_sensor_fusion
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_PROPRIETARY_MODULE := true
LOCAL_CHECK_ELF_FILES := false
ifdef TARGET_2ND_ARCH
LOCAL_MULTILIB := first
LOCAL_SRC_FILES_64 := fsl_sensor_fusion_64
LOCAL_SRC_FILES_32 := fsl_sensor_fusion
else
LOCAL_SRC_FILES := fsl_sensor_fusion
endif
include $(BUILD_PREBUILT)

endif

endif
