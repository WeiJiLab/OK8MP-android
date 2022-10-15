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

package video_enc

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("imx_c2_video_enc_defaults", video_encDefaultsFactory)
}

func video_encDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, video_encDefaults)
    return module
}

func video_encDefaults(ctx android.LoadHookContext) {
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
    if strings.Contains(board, "IMX8MM") {
        Shared_libs = append(Shared_libs, "lib_imx_c2_vpuwrapper_enc")
		Shared_libs = append(Shared_libs, "lib_imx_c2_process_g2d_pre")
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    }else if strings.Contains(board, "IMX8MP") {
        Shared_libs = append(Shared_libs, "lib_imx_c2_vpuwrapper_enc")
        Shared_libs = append(Shared_libs, "lib_imx_c2_process_g2d_pre")
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    }else if strings.Contains(board, "IMX8Q") {
        Shared_libs = append(Shared_libs, "lib_imx_c2_v4l2_enc")
		Shared_libs = append(Shared_libs, "lib_imx_c2_process_isi_pre")
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    }
    p.Target.Android.Shared_libs = Shared_libs
    // TODO: enable it when imx8q v4l2enc is enabled.
    ctx.AppendProperties(p)
}
