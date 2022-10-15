// Copyright 2020 NXP
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

package codec2

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("imx_codec2_defaults", imx_codec2DefaultsFactory)
    android.RegisterModuleType("imx_mm_common_defaults", imx_mmDefaultsFactory)
}

func imx_codec2DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, imx_codec2Defaults)
    return module
}

func imx_mmDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, imx_mmDefaults)
    return module
}

func imx_codec2Defaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                }
        }
    }
    p := &props{}
    p.Target.Android.Enabled = proptools.BoolPtr(false)
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_PLATFORM")
    if strings.Contains(board, "imx") {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    }
    ctx.AppendProperties(p)
}

func imx_mmDefaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Cppflags []string
                }
        }
    }
    p := &props{}
    if ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION") == "v4" {
        p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DGRALLOC_VERSION=4")
    }
    ctx.AppendProperties(p)
}
