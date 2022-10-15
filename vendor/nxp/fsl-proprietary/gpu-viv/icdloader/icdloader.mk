# Viv OpenCL SDK files
ifneq ($(PREBUILT_FSL_IMX_GPU),true)
CL_ICDLOADER_DIR := $(FSL_PROPRIETARY_PATH)/fsl-proprietary/gpu-viv/icdloader

PRODUCT_COPY_FILES += \
    $(CL_ICDLOADER_DIR)/Vivante.icd:vendor/Khronos/OpenCL/vendors/Vivante.icd

endif


