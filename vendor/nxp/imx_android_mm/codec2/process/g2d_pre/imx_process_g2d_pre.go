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

package imx_process_g2d_pre

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("imx_c2_process_g2d_pre_defaults", process_g2d_preDefaultsFactory)
}


func process_g2d_preDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, process_g2d_preDefaults)
    return module
}

func process_g2d_preDefaults(ctx android.LoadHookContext) {
    var Cflags []string
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Cflags []string
                }
        }
    }
    p := &props{}
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE")
    if strings.Contains(board, "IMX8MM") || strings.Contains(board, "IMX8MP"){
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    } else {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }

    p.Target.Android.Cflags = Cflags
    ctx.AppendProperties(p)
}
