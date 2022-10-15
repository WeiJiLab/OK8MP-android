# Viv OpenCL SDK files
ifneq ($(PREBUILT_FSL_IMX_GPU),true)
CL_SDK_DIR := $(FSL_PROPRIETARY_PATH)/fsl-proprietary/include/CL

PRODUCT_COPY_FILES += \
    $(CL_SDK_DIR)/cl_d3d10.h:vendor/etc/viv_sdk/inc/CL/cl_d3d10.h \
    $(CL_SDK_DIR)/cl_d3d11.h:vendor/etc/viv_sdk/inc/CL/cl_d3d11.h \
    $(CL_SDK_DIR)/cl_dx9_media_sharing.h:vendor/etc/viv_sdk/inc/CL/cl_dx9_media_sharing.h \
    $(CL_SDK_DIR)/cl_egl.h:vendor/etc/viv_sdk/inc/CL/cl_egl.h \
    $(CL_SDK_DIR)/cl_ext.h:vendor/etc/viv_sdk/inc/CL/cl_ext.h \
    $(CL_SDK_DIR)/cl_gl_ext.h:vendor/etc/viv_sdk/inc/CL/cl_gl_ext.h \
    $(CL_SDK_DIR)/cl_gl.h:vendor/etc/viv_sdk/inc/CL/cl_gl.h \
    $(CL_SDK_DIR)/cl.h:vendor/etc/viv_sdk/inc/CL/cl.h \
    $(CL_SDK_DIR)/cl.hpp:vendor/etc/viv_sdk/inc/CL/cl.hpp \
    $(CL_SDK_DIR)/cl_platform.h:vendor/etc/viv_sdk/inc/CL/cl_platform.h \
    $(CL_SDK_DIR)/cl_viv_vx_ext.h:vendor/etc/viv_sdk/inc/CL/cl_viv_vx_ext.h \
    $(CL_SDK_DIR)/opencl.h:vendor/etc/viv_sdk/inc/CL/opencl.h

endif


