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

package vpuwrapper_enc

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("imx_c2_vpuwrapper_enc_defaults", vpuwrapper_encDefaultsFactory)
}

func vpuwrapper_encDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, vpuwrapper_encDefaults)
    return module
}

func vpuwrapper_encDefaults(ctx android.LoadHookContext) {
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
    p.Target.Android.Enabled = proptools.BoolPtr(false)
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE")
    if strings.Contains(board, "IMX8MM") {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
        Cflags = append(Cflags, "-DHANTRO_H1")
    } else if strings.Contains(board, "IMX8MP") {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
        Cflags = append(Cflags, "-DHANTRO_VC8000E")
    }
    if ctx.Config().VendorConfig("IMXPLUGIN").String("CFG_SECURE_DATA_PATH") == "y" {
        Cflags = append(Cflags, "-DALWAYS_ENABLE_SECURE_PLAYBACK")
    }
    p.Target.Android.Cflags = Cflags
    ctx.AppendProperties(p)
}
