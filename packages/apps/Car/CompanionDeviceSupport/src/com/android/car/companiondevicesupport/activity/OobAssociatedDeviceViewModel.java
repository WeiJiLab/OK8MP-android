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

import static com.android.car.connecteddevice.util.SafeLog.logd;
import static com.android.car.connecteddevice.util.SafeLog.loge;

import android.app.Application;
import android.bluetooth.BluetoothAdapter;
import android.os.RemoteException;

import com.android.car.companiondevicesupport.api.internal.association.IAssociatedDeviceManager;
import com.android.car.companiondevicesupport.api.internal.association.OobEligibleDevice;


public class OobAssociatedDeviceViewModel extends AssociatedDeviceViewModel {
    private static final String TAG = "OobAssociatedDeviceViewModel";

    private final OobEligibleDevice mOobEligibleDevice;

    public OobAssociatedDeviceViewModel(Application application,
            OobEligibleDevice oobEligibleDevice) {
        super(application);

        mOobEligibleDevice = oobEligibleDevice;
    }

    @Override
    protected void startAssociation() {
        IAssociatedDeviceManager manager = getAssociatedDeviceManager();
        if (manager == null) {
            return;
        }
        getAssociationState().postValue(AssociationState.PENDING);
        if (!BluetoothAdapter.getDefaultAdapter().isEnabled()) {
            return;
        }
        try {
            logd(TAG, "Starting association with " + mOobEligibleDevice.getDeviceAddress());
            manager.startOobAssociation(mOobEligibleDevice);

        } catch (RemoteException e) {
            loge(TAG, "Failed to start association .", e);
            getAssociationState().postValue(AssociationState.ERROR);
        }
        getAssociationState().postValue(AssociationState.STARTING);
    }
}
