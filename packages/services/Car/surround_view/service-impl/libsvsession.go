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
package libsvsession

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
)

func init() {
    android.RegisterModuleType("libsvsession_defaults", libsvsessionDefaultsFactory)
}

func libsvsessionDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, libsvsessionDefaults)
    return module
}

func libsvsessionDefaults(ctx android.LoadHookContext) {
    var Static_libs []string
    var cppflags []string
    type props struct {
        Target struct {
                Android struct {
                        Static_libs []string
                        Cflags []string
                }
        }
    }

    p := &props{}
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_PLATFORM")
    if strings.Contains(board, "imx") {
        Static_libs = append(Static_libs, "libimxsv_base")
        cppflags = append(cppflags, "-DENABLE_IMX_CORELIB");
        p.Target.Android.Static_libs = Static_libs
        p.Target.Android.Cflags = cppflags
        ctx.AppendProperties(p)
    }
    ctx.AppendProperties(p)
}
