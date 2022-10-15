// Copyright 2019 NXP
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package imx_vpu_hantro

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("IMX_VPU_DEC_DEFAULTS", IMX_VPU_DEC_DefaultsFactory)
    android.RegisterModuleType("IMX_VPU_ENC_DEFAULTS", IMX_VPU_ENC_DefaultsFactory)
}

func IMX_VPU_DEC_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, IMX_VPU_DEC_Defaults)
    return module
}

func IMX_VPU_DEC_Defaults(ctx android.LoadHookContext) {
    var Cflags []string
    var Include_dirs []string
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Cflags []string
                        Include_dirs []string
                }
        }
    }
    p := &props{}
    var vpu_type string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_VPU_TYPE")
    if strings.Contains(vpu_type, "hantro") {
        p.Target.Android.Enabled = proptools.BoolPtr(true)

        if ctx.AConfig().PlatformVersionName() == "10" {
            Include_dirs = append(Include_dirs, "system/core/libion/kernel-headers")
            Include_dirs = append(Include_dirs, "system/core/libion")
            Include_dirs = append(Include_dirs, "device/fsl/common/kernel-headers")
        } else {
            // from android 11, some include dir is changed.
            Include_dirs = append(Include_dirs, "system/memory/libion/kernel-headers/linux")
            Include_dirs = append(Include_dirs, "system/memory/libion/kernel-headers")
            Include_dirs = append(Include_dirs, "system/memory/libion")
            Include_dirs = append(Include_dirs, "device/nxp/common/kernel-headers")
        }
    } else {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("CFG_SECURE_DATA_PATH") == "y" {
        Cflags = append(Cflags, "-DCFG_SECURE_DATA_PATH")
    }
    p.Target.Android.Cflags = Cflags
    p.Target.Android.Include_dirs = Include_dirs
    ctx.AppendProperties(p)
}

func IMX_VPU_ENC_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, IMX_VPU_ENC_Defaults)
    return module
}

func IMX_VPU_ENC_Defaults(ctx android.LoadHookContext) {
    var Cflags []string
    var Include_dirs []string
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Cflags []string
                        Include_dirs []string
                }
        }
    }
    p := &props{}
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE")
    if strings.Contains(board, "IMX8MM") {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
        Cflags = append(Cflags, "-DENC_MODULE_PATH=\"/dev/mxc_hantro_h1\"")
        Cflags = append(Cflags, "-DMEMALLOC_MODULE_PATH=\"/dev/ion\"")

        if ctx.AConfig().PlatformVersionName() == "10" {
            Include_dirs = append(Include_dirs, "system/core/libion/kernel-headers")
            Include_dirs = append(Include_dirs, "system/core/libion")
            Include_dirs = append(Include_dirs, "device/fsl/common/kernel-headers")
        } else {
            // from android 11, some include dir is changed.
            Include_dirs = append(Include_dirs, "system/memory/libion/kernel-headers/linux")
            Include_dirs = append(Include_dirs, "system/memory/libion/kernel-headers")
            Include_dirs = append(Include_dirs, "system/memory/libion")
            Include_dirs = append(Include_dirs, "device/nxp/common/kernel-headers")
        }
    } else {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("CFG_SECURE_DATA_PATH") == "y" {
        Cflags = append(Cflags, "-DCFG_SECURE_DATA_PATH")
    }
    p.Target.Android.Cflags = Cflags
    p.Target.Android.Include_dirs = Include_dirs
    ctx.AppendProperties(p)
}

