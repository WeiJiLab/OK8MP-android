LOCAL_PATH := $(call my-dir)

#for media codec c2
include $(CLEAR_VARS)
ifeq ($(BOARD_SOC_TYPE),IMX8MQ)
ifeq ($(BOARD_TYPE),AIY)
SUB_PATH := imxaiy8mq
else
SUB_PATH := imx8mq
endif
endif
ifeq ($(BOARD_SOC_TYPE),IMX8Q)
SUB_PATH := imx8q
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MM)
SUB_PATH := imx8mm
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MN)
SUB_PATH := imx8mn
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MP)
SUB_PATH := imx8mp
endif

FILE_NAME := media_codecs_c2
LOCAL_SRC_FILES := $(SUB_PATH)/$(FILE_NAME)_temp.xml
include $(LOCAL_PATH)/check_restricted_codec.mk
LOCAL_MODULE := media_codecs_c2.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
LOCAL_POST_INSTALL_CMD := rm -f $(LOCAL_PATH)/$(SUB_PATH)/$(FILE_NAME)_temp.xml
include $(BUILD_PREBUILT)

ifeq ($(BOARD_SOC_TYPE),IMX8Q)
include $(CLEAR_VARS)
SUB_PATH := imx8q
FILE_NAME := media_codecs_8qxp
LOCAL_SRC_FILES := $(SUB_PATH)/$(FILE_NAME)_temp.xml
include $(LOCAL_PATH)/check_restricted_codec.mk
LOCAL_MODULE := $(FILE_NAME).xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/vendor_overlay_soc/imx8qxp/vendor/etc
LOCAL_MODULE_STEM := media_codecs_c2.xml
LOCAL_POST_INSTALL_CMD := rm -f $(LOCAL_PATH)/$(SUB_PATH)/$(FILE_NAME)_temp.xml
include $(BUILD_PREBUILT)
endif

ifeq ($(BOARD_SOC_TYPE),IMX8Q)
include $(CLEAR_VARS)
SUB_PATH := imx8q
FILE_NAME := media_codecs_8qm
LOCAL_SRC_FILES := $(SUB_PATH)/$(FILE_NAME)_temp.xml
include $(LOCAL_PATH)/check_restricted_codec.mk
LOCAL_MODULE := $(FILE_NAME).xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/vendor_overlay_soc/imx8qm/vendor/etc
LOCAL_MODULE_STEM := media_codecs_c2.xml
LOCAL_POST_INSTALL_CMD := rm -f $(LOCAL_PATH)/$(SUB_PATH)/$(FILE_NAME)_temp.xml
include $(BUILD_PREBUILT)
endif

include $(CLEAR_VARS)
LOCAL_SRC_FILES := media_codecs.xml
LOCAL_MODULE := media_codecs.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)

HAVE_AC3 = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ac3_dec/media_codecs_c2_ac3.xml && echo yes)
ifeq ($(HAVE_AC3), yes)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ac3_dec/media_codecs_c2_ac3.xml
LOCAL_MODULE := media_codecs_c2_ac3.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif

HAVE_DDP = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ddp_dec/media_codecs_c2_ddp.xml && echo yes)
ifeq ($(HAVE_DDP), yes)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ddp_dec/media_codecs_c2_ddp.xml
LOCAL_MODULE := media_codecs_c2_ddp.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif

HAVE_MS = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ms_codec/media_codecs_c2_ms.xml && echo yes)
ifeq ($(HAVE_MS), yes)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ms_codec/media_codecs_c2_ms.xml
LOCAL_MODULE := media_codecs_c2_ms.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif

HAVE_WMV9 = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ms_codec/media_codecs_c2_wmv9.xml && echo yes)
ifeq ($(HAVE_WMV9), yes)
ifneq ($(findstring $(BOARD_SOC_TYPE),  IMX8Q IMX8MQ),)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ms_codec/media_codecs_c2_wmv9.xml

ifneq ($(findstring $(BOARD_SOC_TYPE),  IMX8MQ),)
LOCAL_SRC_FILES := ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_ms_codec/media_codecs_c2_wmv9_hantro.xml
endif

LOCAL_MODULE := media_codecs_c2_wmv9.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif
endif

HAVE_RA = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_real_dec/media_codecs_c2_ra.xml && echo yes)
ifeq ($(HAVE_RA), yes)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_real_dec/media_codecs_c2_ra.xml
LOCAL_MODULE := media_codecs_c2_ra.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif

HAVE_RV = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_real_dec/media_codecs_c2_rv.xml && echo yes)
ifeq ($(HAVE_RV), yes)
ifneq ($(findstring $(BOARD_SOC_TYPE),  IMX8MQ IMX8Q),)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/fsl_real_dec/media_codecs_c2_rv.xml
LOCAL_MODULE := media_codecs_c2_rv.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif
endif

ifneq ($(findstring $(BOARD_SOC_TYPE),  IMX8MP IMX8Q),)

HAVE_DSP_AACP = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/imx_dsp_aacp_dec/media_codecs_c2_dsp_aacp.xml && echo yes)
ifeq ($(HAVE_DSP_AACP), yes)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/imx_dsp_aacp_dec/media_codecs_c2_dsp_aacp.xml
LOCAL_MODULE := media_codecs_c2_dsp_aacp.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif

HAVE_DSP_WMA = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/imx_dsp_wma_dec/media_codecs_c2_dsp_wma.xml && echo yes)
ifeq ($(HAVE_DSP_WMA), yes)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/imx_dsp_wma_dec/media_codecs_c2_dsp_wma.xml
LOCAL_MODULE := media_codecs_c2_dsp_wma.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif

HAVE_DSP = $(shell test -f $(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/imx_dsp_codec/media_codecs_c2_dsp.xml && echo yes)
ifeq ($(HAVE_DSP), yes)
include $(CLEAR_VARS)
LOCAL_SRC_FILES = ../../../../$(FSL_RESTRICTED_CODEC_PATH)/fsl-restricted-codec/imx_dsp_codec/media_codecs_c2_dsp.xml
LOCAL_MODULE := media_codecs_c2_dsp.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
endif

endif

#for media codec performance setting
include $(CLEAR_VARS)
LOCAL_MODULE := media_codecs_performance.xml
LOCAL_SRC_FILES := media_codecs_performance.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)

#for media codec performance setting
include $(CLEAR_VARS)
ifeq ($(BOARD_SOC_TYPE),IMX7ULP)
LOCAL_SRC_FILES := imx7ulp/media_codecs_performance_c2.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MQ)
ifeq ($(BOARD_TYPE),AIY)
LOCAL_SRC_FILES := imxaiy8mq/media_codecs_performance_c2.xml
else
LOCAL_SRC_FILES := imx8mq/media_codecs_performance_c2.xml
endif
endif
ifeq ($(BOARD_SOC_TYPE),IMX8Q)
LOCAL_SRC_FILES := imx8q/media_codecs_performance_c2.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MM)
LOCAL_SRC_FILES := imx8mm/media_codecs_performance_c2.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MN)
LOCAL_SRC_FILES := imx8mn/media_codecs_performance_c2.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MP)
LOCAL_SRC_FILES := imx8mp/media_codecs_performance_c2.xml
endif
LOCAL_MODULE := media_codecs_performance_c2.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)

ifeq ($(BOARD_SOC_TYPE),IMX8Q)
include $(CLEAR_VARS)
SUB_PATH := imx8q
FILE_NAME := media_codecs_performance_c2_8qm
LOCAL_MODULE := $(FILE_NAME).xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := imx8q/$(FILE_NAME).xml
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/vendor_overlay_soc/imx8qm/vendor/etc
LOCAL_MODULE_STEM := media_codecs_performance_c2.xml
include $(BUILD_PREBUILT)
endif

ifeq ($(BOARD_SOC_TYPE),IMX8Q)
include $(CLEAR_VARS)
SUB_PATH := imx8q
FILE_NAME := media_codecs_performance_c2_8qxp
LOCAL_MODULE := $(FILE_NAME).xml
LOCAL_MODULE_CLASS := ETC
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := imx8q/$(FILE_NAME).xml
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/vendor_overlay_soc/imx8qxp/vendor/etc
LOCAL_MODULE_STEM := media_codecs_performance_c2.xml
include $(BUILD_PREBUILT)
endif
