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

package imxextractor

import (
        "android/soong/android"
        "android/soong/cc"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("imxextractor_defaults", imxextractorDefaultsFactory)
}

func imxextractorDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, imxextractorDefaults)
    return module
}

func imxextractorDefaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                }
        }
    }
    p := &props{}
    p.Target.Android.Enabled = proptools.BoolPtr(true)
    if ctx.Config().VendorConfig("IMXPLUGIN").Bool("BOARD_VPU_ONLY") {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }
    ctx.AppendProperties(p)
}
