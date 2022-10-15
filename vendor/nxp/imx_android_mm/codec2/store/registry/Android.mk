LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := c2_component_register_8mm
ifeq ($(BOARD_SOC_TYPE),IMX8MM)
LOCAL_SRC_FILES := c2_component_register_8mm
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MN)
LOCAL_SRC_FILES := c2_component_register_8mn
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MP)
LOCAL_SRC_FILES := c2_component_register_8mp
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MQ)
LOCAL_SRC_FILES := c2_component_register_8mq
endif
ifeq ($(BOARD_SOC_TYPE),IMX8Q)
LOCAL_SRC_FILES := c2_component_register_8q
endif
ifeq ($(BOARD_SOC_TYPE),IMX7ULP)
LOCAL_SRC_FILES := c2_component_register_7ulp
endif

LOCAL_MODULE := c2_component_register
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
