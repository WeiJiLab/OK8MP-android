/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.car.companiondevicesupport.activity;

import android.app.Application;

import androidx.annotation.NonNull;
import androidx.lifecycle.ViewModel;
import androidx.lifecycle.ViewModelProvider;

import com.android.car.companiondevicesupport.api.internal.association.OobEligibleDevice;


public class OobAssociatedDeviceViewModelFactory extends ViewModelProvider.AndroidViewModelFactory {

    private final Application mApplication;
    private final OobEligibleDevice mOobEligibleDevice;

    public static OobAssociatedDeviceViewModelFactory getInstance(Application application,
            OobEligibleDevice oobEligibleDevice) {
        return new OobAssociatedDeviceViewModelFactory(application, oobEligibleDevice);
    }

    public OobAssociatedDeviceViewModelFactory(
            @NonNull Application application, OobEligibleDevice oobEligibleDevice) {
        super(application);
        mApplication = application;
        mOobEligibleDevice = oobEligibleDevice;
    }

    @NonNull
    @Override
    public <T extends ViewModel> T create(@NonNull Class<T> modelClass) {
        return modelClass.cast(new OobAssociatedDeviceViewModel(mApplication, mOobEligibleDevice));
    }
}
