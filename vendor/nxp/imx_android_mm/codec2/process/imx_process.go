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

package imx_process_g2d_post

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("imx_c2_process_defaults", imx_processDefaultsFactory)
}

func imx_processDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, imx_processDefaults)
    return module
}

func imx_processDefaults(ctx android.LoadHookContext) {
    var Shared_libs []string
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Shared_libs []string
                }
        }
    }
    p := &props{}
    p.Target.Android.Enabled = proptools.BoolPtr(false)
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE")
    if strings.Contains(board, "IMX8MM") || strings.Contains(board, "IMX8MP") {
        Shared_libs = append(Shared_libs, "lib_imx_c2_process_g2d_pre")
        Shared_libs = append(Shared_libs, "lib_imx_c2_process_dummy_post")
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    }else if strings.Contains(board, "IMX8Q") {
        Shared_libs = append(Shared_libs, "lib_imx_c2_process_isi_pre")
		Shared_libs = append(Shared_libs, "lib_imx_c2_process_g2d_post")
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    }else if strings.Contains(board, "IMX8MQ") {
        Shared_libs = append(Shared_libs, "lib_imx_c2_process_dummy_post")
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    }
    p.Target.Android.Shared_libs = Shared_libs
    ctx.AppendProperties(p)
}
